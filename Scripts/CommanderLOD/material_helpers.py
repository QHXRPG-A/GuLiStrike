"""Material and import helpers scoped to isolated commander review assets."""
import unreal,json,hashlib
from pathlib import Path
from common import ROOT,ART as COMMANDER_ART,REVIEW_PACKAGE
ART=COMMANDER_ART/'BiZhiMao'
META=json.loads((ART/'vertex_metadata.json').read_text(encoding='utf8'))
BASE=REVIEW_PACKAGE+'/BiZhiMao'
SOURCE='/Game/GuLiStrike/Mechs/ControlRigMech'
OWNER='GuLiStrike.CommanderLOD.3Tier.v1'
LIB=unreal.EditorAssetLibrary
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
EDIT=unreal.MaterialEditingLibrary
REPORT=dict(success=False,art_assets=[],actual_version_approval='pending',formal_assets_changed=False)
def owned(path):
    asset=unreal.load_asset(path) if LIB.does_asset_exist(path) else None
    assert not asset or LIB.get_metadata_tag(asset,'GuLi.Owner')==OWNER,('unowned derivative',path)
    return asset

def save(asset):
    assert asset and asset.get_path_name().startswith(BASE+'/'),asset
    LIB.set_metadata_tag(asset,'GuLi.Owner',OWNER);LIB.set_metadata_tag(asset,'GuLi.SourceVersion','ControlRig B_v4')
    LIB.set_metadata_tag(asset,'GuLi.BiZhiMaoVersion',META['version']+'; B pending')
    assert LIB.save_loaded_asset(asset,False),asset.get_path_name()
    if asset.get_path_name() not in REPORT['art_assets']:REPORT['art_assets'].append(asset.get_path_name())

def create(path,cls,factory):
    asset=owned(path)
    if asset is None:
        folder,name=path.rsplit('/',1);LIB.make_directory(folder);asset=TOOLS.create_asset(name,folder,cls,factory)
    assert asset;save(asset);return asset

def copy(source,target):
    asset=owned(target)
    if asset is None:LIB.make_directory(target.rsplit('/',1)[0]);asset=LIB.duplicate_asset(source,target)
    assert asset;LIB.set_metadata_tag(asset,'GuLi.SourceAsset',source);save(asset);return asset

def imported(filename,path,options=None,factory=None):
    existing=owned(path);digest=hashlib.sha256(Path(filename).read_bytes()).hexdigest()
    if existing and LIB.get_metadata_tag(existing,'GuLi.SourceSHA256')==digest:return existing
    folder,name=path.rsplit('/',1);LIB.make_directory(folder);task=unreal.AssetImportTask()
    for key,value in dict(filename=str(filename),destination_path=folder,destination_name=name,automated=True,
        async_=False,replace_existing=bool(existing),replace_existing_settings=True,save=False).items():task.set_editor_property(key,value)
    if options:task.set_editor_property('options',options)
    if factory:task.set_editor_property('factory',factory)
    TOOLS.import_asset_tasks([task]);asset=unreal.load_asset(path);assert asset,(path,list(task.imported_object_paths))
    LIB.set_metadata_tag(asset,'GuLi.SourceSHA256',digest);save(asset);return asset

def node(owner,cls,**props):
    result=EDIT.create_material_expression_in_function(owner,cls) if isinstance(owner,unreal.MaterialFunction) else EDIT.create_material_expression(owner,cls)
    for key,value in props.items():result.set_editor_property(key,value)
    return result

def link(a,b,pin='',output=''):assert EDIT.connect_material_expressions(a,output,b,pin),(a.get_name(),b.get_name(),pin,output)

def constant(owner,value):
    if isinstance(value,(int,float)):return node(owner,unreal.MaterialExpressionConstant,r=value)
    values=list(value)+[0]*max(0,4-len(value))
    return node(owner,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(*values[:4]))

def world(owner,value,output=''):
    result=node(owner,unreal.MaterialExpressionTransform,transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
        transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD);link(value,result,output=output);return result

def linear(hex_color):
    rgb=[int(hex_color[i:i+2],16)/255 for i in (0,2,4)]
    return [v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in rgb]

def material(path,fn,contour=False):
    mat=create(path,unreal.Material,unreal.MaterialFactoryNew());EDIT.delete_all_material_expressions(mat)
    for key,value in dict(shading_model=unreal.MaterialShadingModel.MSM_UNLIT,two_sided=False,
        used_with_instanced_static_meshes=True,max_world_position_offset_displacement=6400.).items():mat.set_editor_property(key,value)
    call=node(mat,unreal.MaterialExpressionMaterialFunctionCall);assert call.set_material_function(fn)
    assert EDIT.connect_material_property(call,'Offset',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    animated=node(mat,unreal.MaterialExpressionVertexInterpolator);link(call,animated,output='Normal')
    if contour:
        ink=constant(mat,linear('1B2422'));assert EDIT.connect_material_property(ink,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        palette=node(mat,unreal.MaterialExpressionVertexColor)
        d1=node(mat,unreal.MaterialExpressionTextureCoordinate,coordinate_index=1);d2=node(mat,unreal.MaterialExpressionTextureCoordinate,coordinate_index=2)
        mask=node(mat,unreal.MaterialExpressionTextureSample,texture=unreal.load_asset(BASE+'/Textures/T_BiZhiMao_InternalLineMask_2K'),sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        strength=node(mat,unreal.MaterialExpressionScalarParameter,parameter_name='InternalLineStrength',default_value=.30)
        width=node(mat,unreal.MaterialExpressionScalarParameter,parameter_name='InternalLineHalfWidthMeters',default_value=.006)
        tint=node(mat,unreal.MaterialExpressionVectorParameter,parameter_name='StatusTint',default_value=unreal.LinearColor(1,1,1,1))
        tint_strength=node(mat,unreal.MaterialExpressionScalarParameter,parameter_name='StatusTintStrength',default_value=0)
        toon=node(mat,unreal.MaterialExpressionCustom,output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,
            code='float3 s=round(saturate(Palette.rgb)*255)/255; float3 c=lerp(s/12.92,pow((s+.055)/1.055,2.4),step(.04045,s)); float d=dot(normalize(N),normalize(float3(.35,.55,.76))); float band=d<.12?.42:(d<.55?.74:1); float dist=min(min(D1.x,1-D1.y),D2.x); float edge=1-smoothstep(Width-.0015,Width+.0015,dist); float ink=max(edge,Mask*.35); float3 color=lerp(c*band,float3('+','.join(str(v) for v in linear('1B2422'))+'),saturate(ink*Strength));return lerp(color,color*Tint,TintStrength);')
        inputs={'Palette':(palette,''),'N':(animated,''),'D1':(d1,''),'D2':(d2,''),'Mask':(mask,'R'),
            'Strength':(strength,''),'Width':(width,''),'Tint':(tint,'RGB'),'TintStrength':(tint_strength,'')}
        pins=[]
        for name in inputs:
            pin=unreal.CustomInput();pin.set_editor_property('input_name',name);pins.append(pin)
        toon.set_editor_property('inputs',pins)
        for name,(value,output) in inputs.items():link(value,toon,name,output)
        assert EDIT.connect_material_property(toon,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.recompile_material(mat);save(mat);return mat

def fbx_options():
    ui=unreal.FbxImportUI();kind=unreal.FBXImportType.FBXIT_STATIC_MESH
    for key,value in dict(import_materials=False,import_textures=False,import_mesh=True,import_as_skeletal=False,
        import_animations=False,automated_import_should_detect_type=False,mesh_type_to_import=kind,original_import_type=kind).items():ui.set_editor_property(key,value)
    data=ui.get_editor_property('static_mesh_import_data')
    for key,value in dict(combine_meshes=True,auto_generate_collision=False,generate_lightmap_u_vs=False,build_nanite=False,
        transform_vertex_to_absolute=True,remove_degenerates=False,convert_scene=True,convert_scene_unit=True,
        normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS,
        vertex_color_import_option=unreal.VertexColorImportOption.REPLACE).items():data.set_editor_property(key,value)
    return ui
