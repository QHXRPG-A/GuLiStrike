"""Normal isolated Editor worker. Saves only the new approved Pioneer art root."""
import hashlib,json,traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
ART=ROOT/'ArtSource/Mechs/RSGMechStyle_20261003'
SOURCE=ART/'Delivery_UE_v1'
OUT=SOURCE/'Reports'
BASE='/Game/GuLiStrike/Robots/RSGMech'
OWNER='GuLiStrike.Pioneer.B-v1.Delivery_UE_v1'
LIB=unreal.EditorAssetLibrary
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
EDIT=unreal.MaterialEditingLibrary
META=json.loads((SOURCE/'vat_metadata.json').read_text(encoding='utf8'))
RESULT={'success':False,'assets':[]}

def write(name,value):
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/name).write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf8')

def save(asset):
    assert asset and asset.get_path_name().startswith(BASE+'/'), 'Unexpected package save'
    LIB.set_metadata_tag(asset,'GuLi.Owner',OWNER)
    LIB.set_metadata_tag(asset,'GuLi.ApprovedVersion','B-v1')
    assert LIB.save_loaded_asset(asset,False), asset.get_path_name()
    if asset.get_path_name() not in RESULT['assets']:RESULT['assets'].append(asset.get_path_name())

def owned(path):
    asset=unreal.load_asset(path) if LIB.does_asset_exist(path) else None
    assert not asset or LIB.get_metadata_tag(asset,'GuLi.Owner')==OWNER, path+' belongs to another task'
    return asset

def import_asset(filename,path,options=None,factory=None):
    existing=owned(path)
    digest=hashlib.sha256(Path(filename).read_bytes()).hexdigest()
    if existing and LIB.get_metadata_tag(existing,'GuLi.SourceSHA256')==digest:return existing
    folder,name=path.rsplit('/',1)
    LIB.make_directory(folder)
    task=unreal.AssetImportTask()
    for key,value in dict(filename=str(filename),destination_path=folder,destination_name=name,automated=True,
        async_=False,replace_existing=bool(existing),replace_existing_settings=True,save=False).items():task.set_editor_property(key,value)
    if options:task.set_editor_property('options',options)
    if factory:task.set_editor_property('factory',factory)
    TOOLS.import_asset_tasks([task])
    asset=unreal.load_asset(path)
    assert asset, (path,list(task.imported_object_paths))
    LIB.set_metadata_tag(asset,'GuLi.SourceSHA256',digest)
    save(asset)
    return asset

def create(path,cls,factory):
    asset=owned(path)
    if not asset:
        folder,name=path.rsplit('/',1);LIB.make_directory(folder)
        asset=TOOLS.create_asset(name,folder,cls,factory)
    assert asset,path
    save(asset)
    return asset

def fbx_options(skeletal=False,animation=False,skeleton=None):
    ui=unreal.FbxImportUI()
    kind=unreal.FBXImportType.FBXIT_ANIMATION if animation else unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else unreal.FBXImportType.FBXIT_STATIC_MESH
    for key,value in dict(import_materials=False,import_textures=False,import_mesh=not animation,
        import_as_skeletal=skeletal,import_animations=animation,create_physics_asset=False,
        automated_import_should_detect_type=False,mesh_type_to_import=kind,original_import_type=kind).items():ui.set_editor_property(key,value)
    if skeleton:ui.set_editor_property('skeleton',skeleton)
    data=ui.get_editor_property('anim_sequence_import_data' if animation else 'skeletal_mesh_import_data' if skeletal else 'static_mesh_import_data')
    for key,value in dict(convert_scene=True,convert_scene_unit=True,force_front_x_axis=False,import_uniform_scale=1.0).items():data.set_editor_property(key,value)
    if animation:
        data.set_editor_property('animation_length',unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        data.set_editor_property('use_default_sample_rate',False);data.set_editor_property('custom_sample_rate',30)
    else:
        data.set_editor_property('normal_import_method',unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
        if skeletal:
            data.set_editor_property('preserve_smoothing_groups',True)
            data.set_editor_property('update_skeleton_reference_pose',False);data.set_editor_property('use_t0_as_ref_pose',False)
        else:
            for key,value in dict(combine_meshes=True,auto_generate_collision=False,generate_lightmap_u_vs=False,
                build_nanite=False,transform_vertex_to_absolute=True,remove_degenerates=False).items():data.set_editor_property(key,value)
    return ui

def texture(filename,name,srgb=False,float_data=False):
    tex=import_asset(filename,BASE+'/Textures/'+name,factory=unreal.TextureFactory())
    tex.set_editor_property('srgb',srgb)
    tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_HDR_F32 if float_data else unreal.TextureCompressionSettings.TC_DEFAULT if srgb else unreal.TextureCompressionSettings.TC_MASKS)
    if float_data:
        tex.set_editor_property('mip_gen_settings',unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property('filter',unreal.TextureFilter.TF_NEAREST)
        tex.set_editor_property('address_x',unreal.TextureAddress.TA_CLAMP);tex.set_editor_property('address_y',unreal.TextureAddress.TA_CLAMP)
        tex.set_editor_property('never_stream',True)
    save(tex)
    return tex

def node(owner,cls,**props):
    value=EDIT.create_material_expression_in_function(owner,cls) if isinstance(owner,unreal.MaterialFunction) else EDIT.create_material_expression(owner,cls)
    for key,item in props.items():value.set_editor_property(key,item)
    return value

def link(a,b,pin='',output=''):
    assert EDIT.connect_material_expressions(a,output,b,pin),(a.get_name(),b.get_name(),pin,output)

def world_vector(owner,a,output=''):
    n=node(owner,unreal.MaterialExpressionTransform,transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
        transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    link(a,n,'',output);return n

def vat_function(position,rotation):
    fn=create(BASE+'/VAT/MF_Pioneer_BoneVAT',unreal.MaterialFunction,unreal.MaterialFunctionFactoryNew())
    if LIB.get_metadata_tag(fn,'GuLi.VAT')=='v1':
        custom=next(n for n in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if n.get_outer()==fn)
        custom.set_editor_property('code',(ROOT/'Scripts/Pioneer/PioneerBoneVAT.hlsl').read_text(encoding='utf8'))
        EDIT.update_material_function(fn);save(fn);return fn
    inputs={'Position':node(fn,unreal.MaterialExpressionPreSkinnedPosition),'LocalNormal':node(fn,unreal.MaterialExpressionPreSkinnedNormal),
        'BoneUV':node(fn,unreal.MaterialExpressionTextureCoordinate,coordinate_index=2),
        'BonePosition':node(fn,unreal.MaterialExpressionTextureObject,texture=position,sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR),
        'BoneRotation':node(fn,unreal.MaterialExpressionTextureObject,texture=rotation,sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)}
    pivots={'UpperPivot':META['upper_pivot_cm'],'LeftPivot':META['muzzles'][0]['pitch_pivot_cm'],'RightPivot':META['muzzles'][1]['pitch_pivot_cm']}
    for name,value in pivots.items():inputs[name]=node(fn,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(*value,0))
    for field,name in enumerate(['Frame','UpperYaw','LeftPitch','RightPitch']):
        current=node(fn,unreal.MaterialExpressionPerInstanceCustomData,data_index=51+field,const_default_value=0)
        previous=node(fn,unreal.MaterialExpressionPerInstanceCustomData,data_index=55+field,const_default_value=0)
        switch=node(fn,unreal.MaterialExpressionPreviousFrameSwitch)
        link(current,switch,'Current Frame');link(previous,switch,'Previous Frame');inputs[name]=switch
    custom=node(fn,unreal.MaterialExpressionCustom,code=(ROOT/'Scripts/Pioneer/PioneerBoneVAT.hlsl').read_text(encoding='utf8'),
        output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,desc='44-bone 30fps rigid VAT, animated normal and common muzzle pivots')
    pins=[]
    for name in inputs:
        pin=unreal.CustomInput();pin.set_editor_property('input_name',name);pins.append(pin)
    custom.set_editor_property('inputs',pins)
    normal=unreal.CustomOutput();normal.set_editor_property('output_name','AnimatedNormal');normal.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property('additional_outputs',[normal])
    for name,src in inputs.items():link(src,custom,name)
    for index,(name,pin) in enumerate([('Offset',''),('Normal','AnimatedNormal')]):
        output=node(fn,unreal.MaterialExpressionFunctionOutput,output_name=name,sort_priority=index)
        link(world_vector(fn,custom,pin),output)
    EDIT.update_material_function(fn);LIB.set_metadata_tag(fn,'GuLi.VAT','v1');save(fn)
    return fn

def attach_vat(mat,fn):
    call=node(mat,unreal.MaterialExpressionMaterialFunctionCall)
    assert call.set_material_function(fn)
    assert EDIT.connect_material_property(call,'Offset',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    normal=node(mat,unreal.MaterialExpressionVertexInterpolator);link(call,normal,'','Normal')
    mat.set_editor_property('used_with_instanced_static_meshes',True)
    mat.set_editor_property('max_world_position_offset_displacement',1500)
    return normal

def body_material(path,base,line,fn=None):
    mat=create(path,unreal.Material,unreal.MaterialFactoryNew())
    if LIB.get_metadata_tag(mat,'GuLi.Toon')=='v1':return mat
    mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('used_with_skeletal_mesh',fn is None)
    normal=attach_vat(mat,fn) if fn else node(mat,unreal.MaterialExpressionVertexNormalWS)
    uv0=node(mat,unreal.MaterialExpressionTextureCoordinate,coordinate_index=0)
    uv1=node(mat,unreal.MaterialExpressionTextureCoordinate,coordinate_index=1)
    useuv=node(mat,unreal.MaterialExpressionScalarParameter,parameter_name='UseColorUV1',default_value=1 if fn else 0)
    uv=node(mat,unreal.MaterialExpressionLinearInterpolate);link(uv0,uv,'A');link(uv1,uv,'B');link(useuv,uv,'Alpha')
    color=node(mat,unreal.MaterialExpressionTextureSample,texture=base,sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR);link(uv,color,'UVs')
    mask=node(mat,unreal.MaterialExpressionTextureSample,texture=line,sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS);link(uv0,mask,'UVs')
    strength=node(mat,unreal.MaterialExpressionScalarParameter,parameter_name='LineStrength',default_value=1)
    toon=node(mat,unreal.MaterialExpressionCustom,code='float light=dot(normalize(N),normalize(float3(.55,-.35,.76))); float band=light<.12?.42:light<.55?.74:1; return lerp(Color*band,float3(.01764,.013,.01444),saturate(Mask*Lines));',
        output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,desc='Approved three lighting bands and independent structural lines')
    pins=[]
    for name in ['N','Color','Mask','Lines']:
        pin=unreal.CustomInput();pin.set_editor_property('input_name',name);pins.append(pin)
    toon.set_editor_property('inputs',pins)
    for name,src,output in [('N',normal,''),('Color',color,'RGB'),('Mask',mask,'R'),('Lines',strength,'')]:link(src,toon,name,output)
    assert EDIT.connect_material_property(toon,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.recompile_material(mat);LIB.set_metadata_tag(mat,'GuLi.Toon','v1');save(mat)
    return mat

def contour_material(path,fn=None):
    mat=create(path,unreal.Material,unreal.MaterialFactoryNew())
    if LIB.get_metadata_tag(mat,'GuLi.Contour')=='v1':return mat
    mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided',False)
    mat.set_editor_property('used_with_skeletal_mesh',fn is None)
    if fn:attach_vat(mat,fn)
    ink=node(mat,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(.01764,.013,.01444,1))
    assert EDIT.connect_material_property(ink,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.recompile_material(mat);LIB.set_metadata_tag(mat,'GuLi.Contour','v1');save(mat)
    return mat

def material_instance(path,parent,lod,skin=False):
    mat=create(path,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    EDIT.set_material_instance_parent(mat,parent)
    EDIT.set_material_instance_scalar_parameter_value(mat,'LineStrength',[1,.85,.45,0][lod])
    EDIT.set_material_instance_scalar_parameter_value(mat,'UseColorUV1',0 if skin and lod==0 else 1)
    save(mat);return mat

def install():
    readback=json.loads((OUT/'fbx_readback.json').read_text(encoding='utf8'))
    assert readback['passed'] and readback['approved_artifacts_unchanged']==70
    if LIB.does_directory_exist(BASE):
        assets=unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(BASE,recursive=True)
        for item in assets:assert LIB.get_metadata_tag(item.get_asset(),'GuLi.Owner')==OWNER,item.package_name
    write('ue_import_status.json',{'state':'running','stage':'textures'})
    base=texture(ART/'Production_B_v1/Textures/T_RSGMech_BaseColor.png','T_Pioneer_BaseColor',True)
    line=texture(ART/'Production_B_v1/Textures/T_RSGMech_LineMask.png','T_Pioneer_LineMask')
    pos=texture(SOURCE/'Textures/T_Pioneer_BonePosition.exr','T_Pioneer_BonePosition',float_data=True)
    rot=texture(SOURCE/'Textures/T_Pioneer_BoneRotation.exr','T_Pioneer_BoneRotation',float_data=True)
    fn=vat_function(pos,rot)
    body=body_material(BASE+'/Materials/M_Pioneer_Toon3',base,line,fn)
    skin_body=body_material(BASE+'/Materials/M_Pioneer_SK_Toon3',base,line)
    outline=contour_material(BASE+'/Materials/M_Pioneer_Contour',fn)
    skin_outline=contour_material(BASE+'/Materials/M_Pioneer_SK_Contour')
    bodies=[material_instance(BASE+f'/Materials/MI_Pioneer_LOD{i}',body,i) for i in range(4)]
    skin_bodies=[material_instance(BASE+f'/Materials/MI_Pioneer_SK_LOD{i}',skin_body,i,True) for i in range(4)]
    write('ue_import_status.json',{'state':'running','stage':'mesh'})
    mesh=import_asset(SOURCE/'FBX/SM_Pioneer_VAT_LOD0.fbx',BASE+'/Meshes/SM_Pioneer_VAT',fbx_options(),unreal.FbxFactory())
    sub=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    for i in range(1,4):assert sub.import_lod(mesh,i,str(SOURCE/f'FBX/SM_Pioneer_VAT_LOD{i}.fbx'))==i
    materials=[unreal.StaticMaterial(material_interface=bodies[0],material_slot_name='Body'),unreal.StaticMaterial(material_interface=outline,material_slot_name='Contour')]
    materials.extend(unreal.StaticMaterial(material_interface=bodies[i],material_slot_name=f'Body_LOD{i}') for i in range(1,4))
    mesh.set_editor_property('static_materials',materials)
    for i in range(4):
        build=sub.get_lod_build_settings(mesh,i)
        for key,value in dict(use_full_precision_u_vs=True,recompute_normals=False,recompute_tangents=False,generate_lightmap_u_vs=False,remove_degenerates=False).items():build.set_editor_property(key,value)
        sub.set_lod_build_settings(mesh,i,build)
        sub.set_lod_material_slot(mesh,0 if i==0 else i+1,i,0)
        if i<3:sub.set_lod_material_slot(mesh,1,i,1)
    assert sub.set_lod_screen_sizes(mesh,META['screen_sizes'])
    bounds=mesh.get_bounding_box()
    size=(bounds.max-bounds.min).to_tuple()
    assert abs(size[1]-625)<.05,(size,'wrong UE orientation or scale')
    save(mesh)
    skeleton_path=BASE+'/Skeleton/SKEL_Pioneer'
    skeleton=owned(skeleton_path) or LIB.duplicate_asset('/Game/Assets/RSG_UnderWater_Pack/FPS/Models/Mech/SK_FPS_Mech_Skeleton',skeleton_path)
    save(skeleton)
    sk=import_asset(SOURCE/'FBX/SK_Pioneer_LOD0.fbx',BASE+'/Meshes/SK_Pioneer',fbx_options(True,False,skeleton),unreal.FbxFactory())
    sksub=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    for i in range(1,4):assert sksub.import_lod(sk,i,str(SOURCE/f'FBX/SK_Pioneer_LOD{i}.fbx'))==i
    skmats=list(sk.get_editor_property('materials'))
    for item in skmats:
        slot=str(item.get_editor_property('material_slot_name'))
        mat=skin_outline if 'Contour' in slot else skin_bodies[int(slot.rsplit('LOD',1)[1])]
        item.set_editor_property('material_interface',mat)
    sk.set_editor_property('materials',skmats)
    sources=list(sk.get_editor_property('source_models'))
    for i,source in enumerate(sources):
        source.set_editor_property('screen_size',unreal.PerPlatformFloat(default=META['screen_sizes'][i]))
    sk.set_editor_property('source_models',sources)
    physics_path=BASE+'/Physics/PHYS_Pioneer'
    physics=owned(physics_path) or LIB.duplicate_asset('/Game/Assets/RSG_UnderWater_Pack/FPS/Models/Mech/SK_FPS_Mech_PhysicsAsset',physics_path)
    sk.set_editor_property('physics_asset',physics)
    save(physics);save(sk);save(skeleton)
    bones=list(unreal.SkeletonService.list_bones(sk.get_path_name()))
    assert [str(b.bone_name) for b in bones]==[b['name'] for b in META['bones']],[(str(b.bone_name),str(b.parent_bone_name)) for b in bones]
    weights=unreal.SkinWeightModifier();assert weights.set_skeletal_mesh(sk)
    invalid=0
    for i in range(weights.get_num_vertices()):
        values=[float(v) for v in weights.get_vertex_weights(i).values() if v>1e-6]
        if len(values)!=1 or abs(values[0]-1)>1e-5:invalid+=1
    assert invalid==0,invalid
    animations=[]
    for c in META['clips']:
        path=BASE+'/Animations/'+c['source_action']
        animation=import_asset(SOURCE/('FBX/'+c['source_action']+'.fbx'),path,fbx_options(True,True,skeleton),unreal.FbxFactory())
        duration=animation.get_play_length()
        assert abs(duration-c['duration_seconds'])<.04,(path,duration,c)
        animations.append({'asset':path,'duration_seconds':duration})
    RESULT.update(success=True,static_mesh=mesh.get_path_name(),skeletal_mesh=sk.get_path_name(),
        skeleton=skeleton.get_path_name(),physics=physics.get_path_name(),bones=len(bones),invalid_rigid_vertices=invalid,
        width_cm=size[1],lod_screen_sizes=list(sub.get_lod_screen_sizes(mesh)),animations=animations,
        textures=[{'name':t.get_path_name(),'srgb':t.get_editor_property('srgb'),'compression':str(t.get_editor_property('compression_settings'))} for t in [base,line,pos,rot]])
    write('ue_art_import.json',RESULT)
    write('ue_import_status.json',{'state':'complete'})

def main():
    assert '-PioneerArtImportWorker' in unreal.SystemLibrary.get_command_line()
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    key='Interchange.FeatureFlags.Import.Enable'
    previous=unreal.SystemLibrary.get_console_variable_int_value(key)
    unreal.SystemLibrary.execute_console_command(world,key+' 0')
    try:install()
    except Exception:
        RESULT['error']=traceback.format_exc();write('ue_art_import.json',RESULT);write('ue_import_status.json',{'state':'failed','error':RESULT['error']});unreal.log_error(RESULT['error'])
    finally:
        unreal.SystemLibrary.execute_console_command(world,key+' '+str(previous))
        unreal.SystemLibrary.quit_editor()

if __name__=='__main__':main()
