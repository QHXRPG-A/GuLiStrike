"""Import RPF using a normal source-built Editor -ExecutePythonScript process.

Do not invoke synchronously from an MCP game-thread callback. This script only
writes the RPF content root, and hashes its inputs to make repeated imports cheap.
"""
import hashlib
import json
import traceback
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1')
SOURCE=ROOT/'ArtSource/Buildings/ResourceProcessingFactory'
OUT=ROOT/'outputs/resource-processing-factory-20260909'
BASE='/Game/GuLiStrike/Buildings/ResourceProcessingFactory'
OWNER='GuLiStrike.ResourceProcessingFactory.20260909'


def save(asset):
    unreal.EditorAssetLibrary.set_metadata_tag(asset,'GuLi.RPF.Owner',OWNER)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset,False):raise RuntimeError('Save failed: '+asset.get_path_name())


def task(filename,path,options=None,factory=None):
    digest=hashlib.sha256(Path(filename).read_bytes()).hexdigest()
    existing=unreal.load_asset(path)
    if existing and unreal.EditorAssetLibrary.get_metadata_tag(existing,'GuLi.RPF.SourceSHA256')==digest:return existing
    folder,name=path.rsplit('/',1);unreal.EditorAssetLibrary.make_directory(folder)
    t=unreal.AssetImportTask()
    for key,value in dict(filename=str(filename),destination_path=folder,destination_name=name,automated=True,async_=False,replace_existing=True,replace_existing_settings=True,save=False).items():t.set_editor_property(key,value)
    if options:t.set_editor_property('options',options)
    if factory:t.set_editor_property('factory',factory)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    asset=unreal.load_asset(path)
    if not asset:raise RuntimeError('Import failed '+path+' '+str(t.imported_object_paths))
    unreal.EditorAssetLibrary.set_metadata_tag(asset,'GuLi.RPF.SourceSHA256',digest)
    save(asset)
    return asset


def expression(mat,cls,x=0,y=0,**props):
    e=unreal.MaterialEditingLibrary.create_material_expression(mat,cls,x,y)
    for k,v in props.items():e.set_editor_property(k,v)
    return e


def connect(a,ao,b,bi):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(a,ao,b,bi):raise RuntimeError('Material link '+ao+' -> '+bi)


def build_material(label):
    path=BASE+'/Materials/M_RPF_'+label
    mat=unreal.load_asset(path)
    if mat is None:mat=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_RPF_'+label,BASE+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(mat)
    v=expression(mat,unreal.MaterialExpressionVertexColor,-700,-200)
    def prop(e,output,p):
        if not unreal.MaterialEditingLibrary.connect_material_property(e,output,p):raise RuntimeError('Material property '+str(p))
    if label=='Details':
        prop(v,'',unreal.MaterialProperty.MP_BASE_COLOR)
        m=expression(mat,unreal.MaterialExpressionMultiply,-300,140,const_b=5)
        connect(v,'A',m,'A')
        color=expression(mat,unreal.MaterialExpressionMultiply,-100,80)
        connect(v,'',color,'A');connect(m,'',color,'B');prop(color,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        rough=expression(mat,unreal.MaterialExpressionConstant,-300,-10,r=.4);prop(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
        metal=expression(mat,unreal.MaterialExpressionConstant,-300,-90,r=.45);prop(metal,'',unreal.MaterialProperty.MP_METALLIC)
    else:
        tex={}
        for suffix in ['BaseColor','NormalDX','ORM','EmissiveMask']:
            name='T_RPF_'+label+'_'+suffix
            t=task(SOURCE/'Textures'/(name+'.png'),BASE+'/Textures/'+name)
            t.set_editor_property('srgb',suffix=='BaseColor')
            t.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP if suffix=='NormalDX' else unreal.TextureCompressionSettings.TC_MASKS if suffix in ['ORM','EmissiveMask'] else unreal.TextureCompressionSettings.TC_DEFAULT)
            if suffix=='NormalDX':t.set_editor_property('flip_green_channel',False)
            save(t)
            sampler=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if suffix=='NormalDX' else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if suffix=='BaseColor' else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
            tex[suffix]=expression(mat,unreal.MaterialExpressionTextureSample,-650,100+len(tex)*190,texture=t,sampler_type=sampler)
        mul=expression(mat,unreal.MaterialExpressionMultiply,-270,-200)
        if label=='Armor':
            paint=expression(mat,unreal.MaterialExpressionLinearInterpolate,-430,-70)
            flat=expression(mat,unreal.MaterialExpressionConstant3Vector,-650,-380,constant=unreal.LinearColor(.27,.286,.306,1))
            connect(flat,'',paint,'A');connect(tex['BaseColor'],'RGB',paint,'B');connect(v,'A',paint,'Alpha');connect(paint,'',mul,'A')
        else:connect(tex['BaseColor'],'RGB',mul,'A')
        connect(v,'',mul,'B');prop(mul,'',unreal.MaterialProperty.MP_BASE_COLOR)
        prop(tex['NormalDX'],'RGB',unreal.MaterialProperty.MP_NORMAL)
        prop(tex['ORM'],'R',unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
        prop(tex['ORM'],'G',unreal.MaterialProperty.MP_ROUGHNESS)
        prop(tex['ORM'],'B',unreal.MaterialProperty.MP_METALLIC)
        prop(tex['EmissiveMask'],'RGB',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mat.set_editor_property('used_with_skeletal_mesh',True)
    mat.set_editor_property('used_with_nanite',True)
    unreal.MaterialEditingLibrary.recompile_material(mat);save(mat)
    return mat


def fbx_options(skeletal=False,anim=False,skeleton=None):
    opts=unreal.FbxImportUI()
    for k,v in dict(import_materials=False,import_textures=False,import_as_skeletal=skeletal,import_mesh=not anim,import_animations=anim,create_physics_asset=False,automated_import_should_detect_type=False).items():opts.set_editor_property(k,v)
    kind=unreal.FBXImportType.FBXIT_ANIMATION if anim else unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else unreal.FBXImportType.FBXIT_STATIC_MESH
    opts.set_editor_property('mesh_type_to_import',kind)
    if skeleton:opts.set_editor_property('skeleton',skeleton)
    data=opts.get_editor_property('anim_sequence_import_data' if anim else 'skeletal_mesh_import_data' if skeletal else 'static_mesh_import_data')
    for k,v in dict(convert_scene=True,convert_scene_unit=True,force_front_x_axis=False,import_uniform_scale=1.0).items():data.set_editor_property(k,v)
    if anim:
        data.set_editor_property('animation_length',unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        data.set_editor_property('use_default_sample_rate',False)
        data.set_editor_property('custom_sample_rate',30)
    else:
        data.set_editor_property('normal_import_method',unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
        data.set_editor_property('vertex_color_import_option',unreal.VertexColorImportOption.REPLACE)
    if not skeletal and not anim:
        for k,v in dict(combine_meshes=True,auto_generate_collision=False,one_convex_hull_per_ucx=True,build_nanite=True,generate_lightmap_u_vs=True,transform_vertex_to_absolute=True).items():data.set_editor_property(k,v)
    return opts


def install():
    OUT.mkdir(parents=True,exist_ok=True)
    mats={n:build_material(n) for n in ['Armor','Interior','Details']}
    body=task(SOURCE/'Exports/SM_RPF_Body.fbx',BASE+'/Meshes/SM_RPF_Body',fbx_options(),unreal.FbxFactory())
    sub=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    slots=list(body.get_editor_property('static_materials'))
    for i,slot in enumerate(slots):
        name=str(slot.get_editor_property('material_slot_name'))
        body.set_material(i,mats[name.removeprefix('M_RPF_')])
    nanite=body.get_editor_property('nanite_settings');nanite.set_editor_property('enabled',True);body.set_editor_property('nanite_settings',nanite)
    settings=sub.get_lod_build_settings(body,0);settings.set_editor_property('use_full_precision_u_vs',True);sub.set_lod_build_settings(body,0,settings)
    body.get_editor_property('body_setup').set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AND_COMPLEX)
    save(body)
    door=task(SOURCE/'Exports/SK_RPF_Door.fbx',BASE+'/Meshes/SK_RPF_Door',fbx_options(True),unreal.FbxFactory())
    skmats=list(door.get_editor_property('materials'))
    for slot in skmats:
        name=str(slot.get_editor_property('material_slot_name'));slot.set_editor_property('material_interface',mats[name.removeprefix('M_RPF_')])
    door.set_editor_property('materials',skmats)
    skeleton=door.get_editor_property('skeleton');save(skeleton)
    sksub=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    if not sksub.regenerate_lod(door,3,False,False):raise RuntimeError('Door LOD generation failed')
    save(door)
    animations={}
    for name in ['Door_Open','Door_Close']:
        a=task(SOURCE/'Exports'/('A_RPF_'+name+'.fbx'),BASE+'/Animations/A_RPF_'+name,fbx_options(True,True,skeleton),unreal.FbxFactory())
        animations[name]={'path':a.get_path_name(),'seconds':a.get_play_length()}
        save(a)
    report={'success':True,'body':body.get_path_name(),'body_dimensions_cm':list((body.get_bounds().box_extent*2).to_tuple()),'door':door.get_path_name(),'door_lods':sksub.get_lod_count(door),'skeleton':skeleton.get_path_name(),'animations':animations,'body_collision_count':sub.get_convex_collision_count(body),'materials':{n:m.get_path_name() for n,m in mats.items()},'engine':unreal.SystemLibrary.get_engine_version()}
    phys=door.get_editor_property('physics_asset')
    if phys:
        report['physics_asset']=phys.get_path_name()
        try:
            setups=phys.get_editor_property('skeletal_body_setups')
            report['physics_bodies']=[{'bone':str(s.get_editor_property('bone_name')),'class':s.get_class().get_name(),'geometry':str(s.get_editor_property('agg_geom'))} for s in setups]
        except Exception as ex:report['physics_inspection']=str(ex)
        save(phys)
    (OUT/'ue_import_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return report


def main():
    cvar='Interchange.FeatureFlags.Import.Enable'
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    previous=unreal.SystemLibrary.get_console_variable_int_value(cvar)
    unreal.SystemLibrary.execute_console_command(world,cvar+' 0')
    try:
        print(json.dumps(install(),indent=2))
        (OUT/'ue_import_error.txt').unlink(missing_ok=True)
    except Exception:
        (OUT/'ue_import_error.txt').write_text(traceback.format_exc(),encoding='utf-8')
        unreal.log_error(traceback.format_exc())
    finally:
        unreal.SystemLibrary.execute_console_command(world,cvar+' '+str(previous))
        unreal.SystemLibrary.quit_editor()


if __name__=='__main__':main()
