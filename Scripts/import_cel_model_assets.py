"""Import the scoped 20260917 cel model variants in an isolated editor worker."""
import unreal,json,traceback,sys
from pathlib import Path

ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/StylePass_20260917';MODELS=OUT/'Models'
sys.path.insert(0,str(ROOT/'Scripts'))
import import_tactical_handbuilt_models as legacy
LIB=unreal.EditorAssetLibrary;TOOLS=unreal.AssetToolsHelpers.get_asset_tools();EDIT=unreal.MaterialEditingLibrary
OWNER='GuLi.CelPass.20260917';ALLOWED=('/Game/Commander/Units/Tactical/Cel/','/Game/GuLiStrike/Ship/Stylized/')
REPORT={'success':False,'assets':[],'materials':[]}

def own(o):
    if o and LIB.get_metadata_tag(o,'GuLi.ModelProduction.Owner')!=OWNER:raise RuntimeError('Unowned asset '+o.get_path_name())
    return o

def save(o):
    assert o.get_path_name().startswith(ALLOWED)
    LIB.set_metadata_tag(o,'GuLi.ModelProduction.Owner',OWNER)
    assert LIB.save_loaded_asset(o,False)

def imported(file,path,skeletal=None):
    import hashlib
    old=own(unreal.load_asset(path));digest=hashlib.sha256(file.read_bytes()).hexdigest()
    if old and LIB.get_metadata_tag(old,'GuLi.ModelProduction.SourceSHA256')==digest:return old
    folder,name=path.rsplit('/',1);LIB.make_directory(folder);t=unreal.AssetImportTask()
    for k,v in dict(filename=str(file),destination_path=folder,destination_name=name,automated=True,async_=False,replace_existing=bool(old),replace_existing_settings=True,save=False).items():t.set_editor_property(k,v)
    if skeletal is not None:t.set_editor_property('options',legacy.options(skeletal));t.set_editor_property('factory',unreal.FbxFactory())
    TOOLS.import_asset_tasks([t]);o=unreal.load_asset(path);assert o,path
    LIB.set_metadata_tag(o,'GuLi.ModelProduction.SourceSHA256',digest);save(o);return o

def node(mat,cls,**props):
    o=EDIT.create_material_expression(mat,cls)
    for k,v in props.items():o.set_editor_property(k,v)
    return o

def color(mat,name,values):return node(mat,unreal.MaterialExpressionVectorParameter,parameter_name=name,default_value=unreal.LinearColor(*values))

def texture(mat,image,uvindex=0,mask=False):
    uv=node(mat,unreal.MaterialExpressionTextureCoordinate,coordinate_index=uvindex)
    tex=node(mat,unreal.MaterialExpressionTextureSample,texture=image,sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS if mask else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    assert EDIT.connect_material_expressions(uv,'',tex,'UVs');return tex

def material(path,base=None,mask=None,uvindex=0,solid=None,contour=False,glow=False):
    folder,name=path.rsplit('/',1);LIB.make_directory(folder);mat=own(unreal.load_asset(path))
    if not mat:mat=TOOLS.create_asset(name,folder,unreal.Material,unreal.MaterialFactoryNew())
    EDIT.delete_all_material_expressions(mat)
    for k,v in dict(shading_model=unreal.MaterialShadingModel.MSM_UNLIT,blend_mode=unreal.BlendMode.BLEND_OPAQUE,two_sided=False,used_with_skeletal_mesh=True,used_with_instanced_static_meshes=True).items():mat.set_editor_property(k,v)
    ink=color(mat,'InkColor',(.008,.026,.051,1))
    if contour:EDIT.connect_material_property(ink,'RGB',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        b=texture(mat,base) if base else color(mat,'ArmorColor',solid)
        n=node(mat,unreal.MaterialExpressionPixelNormalWS);light=color(mat,'ArtLightDirection',(.35,-.55,.76,0))
        h=node(mat,unreal.MaterialExpressionCustom,output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        inputs={'Base':(b,'RGB'),'N':(n,''),'L':(light,'RGB'),'Ink':(ink,'RGB')}
        if mask:inputs['Mask']=(texture(mat,mask,uvindex,True),'R')
        code='float d=dot(normalize(N),normalize(L)); float3 tones=d<.12?float3(.43,.52,.66):(d<.55?float3(.72,.78,.88):float3(1,1,1)); float3 paint=Base*tones; '
        if glow:code='float3 paint=Base*1.35; '
        code+='return lerp(paint,Ink,saturate(Mask));' if mask else 'return paint;'
        h.set_editor_property('code',code);defs=[]
        for name in inputs:
            v=unreal.CustomInput();v.set_editor_property('input_name',name);defs.append(v)
        h.set_editor_property('inputs',defs)
        for name,(source,pin) in inputs.items():assert EDIT.connect_material_expressions(source,pin,h,name)
        assert EDIT.connect_material_property(h,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.layout_material_expressions(mat);EDIT.recompile_material(mat);save(mat);REPORT['materials'].append(mat.get_path_name());return mat

def mesh_settings(mesh,materials,skeletal=False,no_ink=False):
    if skeletal:
        slots=list(mesh.get_editor_property('materials'))
        for slot in slots:
            name=str(slot.get_editor_property('material_slot_name'))
            mat=materials.get(name) or materials['Contour' if 'Contour' in name else 'Body']
            slot.set_editor_property('material_interface',mat)
        mesh.set_editor_property('materials',slots);save(mesh.get_editor_property('skeleton'))
        bounds=mesh.get_imported_bounds();info={'bones':[str(b.bone_name) for b in unreal.SkeletonService.list_bones(mesh.get_path_name())],'slots':[str(s.material_slot_name) for s in slots]}
    else:
        sub=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        b=sub.get_lod_build_settings(mesh,0)
        for k,v in dict(use_full_precision_u_vs=True,recompute_normals=False,recompute_tangents=False,build_scale3d=unreal.Vector(1,1,1)).items():b.set_editor_property(k,v)
        sub.set_lod_build_settings(mesh,0,b)
        if no_ink:
            # Legacy FBX reimport retains old slot names even after the shell is removed.
            assert all(mesh.get_num_sections(lod)==1 for lod in range(sub.get_lod_count(mesh)))
            for lod in range(sub.get_lod_count(mesh)):
                sub.set_lod_material_slot(mesh,0,lod,0)
            slot=unreal.StaticMaterial(material_interface=materials['Body'],material_slot_name='Body')
            mesh.set_editor_property('static_materials',[slot])
        for i,slot in enumerate(mesh.static_materials):
            name=str(slot.material_slot_name);mat=materials.get(name) or materials['Contour' if 'Contour' in name else 'Body'];mesh.set_material(i,mat)
            if 'Contour' in name and i<mesh.get_num_sections(0):sub.enable_section_cast_shadow(mesh,False,0,i)
        bounds=mesh.get_bounds();info={'triangles':mesh.get_num_triangles(0),'uv_channels':sub.get_num_uv_channels(mesh,0),'slots':[str(s.material_slot_name) for s in mesh.static_materials]}
    save(mesh);REPORT['assets'].append({'path':mesh.get_path_name(),'dimensions_cm':list((bounds.box_extent*2).to_tuple()),**info})

def run():
    for unit in ('Sweeper','WarMachine'):
        base='/Game/Commander/Units/Tactical/Cel/'+unit
        atlas=imported(ROOT/'ArtSource/TacticalStyle_20260916/Production_Handbuilt'/('T_'+unit+'_BaseColor.png'),base+'/Textures/T_'+unit+'_BaseColor')
        no_ink=unit=='Sweeper'
        mask=None if no_ink else imported(MODELS/('T_'+unit+'_LineMask.png'),base+'/Textures/T_'+unit+'_LineMask')
        atlas.set_editor_property('srgb',True);save(atlas)
        if mask:
            mask.set_editor_property('srgb',False);mask.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS);save(mask)
        body=material(base+'/Materials/M_'+unit+'_Cel',base=atlas,mask=mask)
        contour=None if no_ink else material(base+'/Materials/M_'+unit+'_Contour',contour=True)
        for skeletal in (False,True):
            name=('SK_' if skeletal else 'SM_')+unit+'_Cel'
            source=ROOT/'ArtSource/StyleAdjust_20260917/Models'/((('SK_' if skeletal else 'SM_')+'Sweeper_NoInk.fbx')) if no_ink else MODELS/(name+'.fbx')
            mesh=imported(source,base+'/Meshes/'+name,skeletal)
            mesh_settings(mesh,{'Body':body,'Contour':body if no_ink else contour},skeletal,no_ink=no_ink)
    base='/Game/GuLiStrike/Ship/Stylized';source=ROOT/'ArtSource/Ships/ShipStylizedStudy_20260916/Production'
    mask=imported(source.parent/'Textures/T_Ship_Internal_LineMask_4K.png',base+'/Textures/T_Ship_Internal_LineMask_4K')
    mask.set_editor_property('srgb',False);mask.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS);save(mask)
    colors={'SS_Armor_Pearl':(.665387,.783538,.775822,1),'SS_Armor_Blue':(.033105,.212231,.323143,1),'SS_Structure_Slate':(.064803,.132868,.191202,1),'SS_Recess_Navy':(.011612,.033105,.064803,1),'SS_Safety_Amber':(.822786,.401978,.076185,1),'SS_Engine_Cyan':(.254152,.887923,.830770,1)}
    mats={n:material(base+'/Materials/M_'+n,mask=mask if n!='SS_Engine_Cyan' else None,uvindex=3,solid=c,glow=n=='SS_Engine_Cyan') for n,c in colors.items()}
    mats['Body']=mats['SS_Armor_Pearl'];mats['Contour']=material(base+'/Materials/M_Ship_Contour',contour=True)
    mats['SS_Ink_Outer_Navy']=mats['Contour']
    for name in ('SM_Ship_AnimeHull','SM_Ship_AnimeContour'):
        mesh=imported(source/(name+'.fbx'),base+'/Meshes/'+name,False);mesh_settings(mesh,mats)
    # Reimport must preserve the authored distance LODs and gameplay attachment aliases.
    scope={'__name__':'cel_model_finalize'}
    finalizer=ROOT/'Scripts/finalize_cel_model_assets.py'
    exec(compile(finalizer.read_text(encoding='utf8'),str(finalizer),'exec'),scope)
    REPORT['finalization']=scope['finish_models']()
    REPORT['success']=True

if __name__=='__main__':
    if '-CelModelImportWorker' not in unreal.SystemLibrary.get_command_line():raise RuntimeError('Isolated worker only')
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world();var='Interchange.FeatureFlags.Import.Enable';old=unreal.SystemLibrary.get_console_variable_int_value(var)
    unreal.SystemLibrary.execute_console_command(world,var+' 0')
    try:run()
    except Exception:REPORT['error']=traceback.format_exc();unreal.log_error(REPORT['error'])
    finally:
        unreal.SystemLibrary.execute_console_command(world,var+' '+str(old))
        (OUT/'model_import.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
        unreal.SystemLibrary.quit_editor()
