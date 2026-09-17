"""Import owned tactical art from a separate normal editor startup process.

Use -TacticalModelImportWorker -TacticalUnit=Sweeper (or WarMachine).  This
imports meshes/materials only; it does not save maps or edit gameplay tables.
"""
import unreal
import json,hashlib,traceback,re
from pathlib import Path

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
SOURCE=ROOT/'ArtSource/TacticalStyle_20260916/Production_Handbuilt'
BASE='/Game/Commander/Units/Tactical'
OWNER='GuLiStrike.Tactical.Handbuilt.20260916'
META='GuLi.ModelProduction.'
LIB=unreal.EditorAssetLibrary
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
UNIT='Sweeper'

def owned(asset):
    if asset and LIB.get_metadata_tag(asset,META+'Owner')!=OWNER:
        raise RuntimeError('Refusing unrelated asset '+asset.get_path_name())
    return asset

def save(asset):
    if not asset.get_path_name().startswith(BASE+'/'+UNIT+'/'):
        raise RuntimeError('Save outside current unit root '+asset.get_path_name())
    LIB.set_metadata_tag(asset,META+'Owner',OWNER)
    if not LIB.save_loaded_asset(asset,False):raise RuntimeError('Save failed '+asset.get_path_name())

def imported(filename,path,options=None,factory=None):
    digest=hashlib.sha256(filename.read_bytes()).hexdigest()
    existing=owned(unreal.load_asset(path))
    if existing and LIB.get_metadata_tag(existing,META+'SourceSHA256')==digest:return existing
    folder,name=path.rsplit('/',1);LIB.make_directory(folder)
    task=unreal.AssetImportTask()
    for k,v in dict(filename=str(filename),destination_path=folder,destination_name=name,automated=True,async_=False,replace_existing=bool(existing),replace_existing_settings=True,save=False).items():task.set_editor_property(k,v)
    if options:task.set_editor_property('options',options)
    if factory:task.set_editor_property('factory',factory)
    TOOLS.import_asset_tasks([task]);asset=unreal.load_asset(path)
    if not asset:raise RuntimeError('Missing imported asset '+path+' '+str(task.imported_object_paths))
    LIB.set_metadata_tag(asset,META+'SourceSHA256',digest)
    LIB.set_metadata_tag(asset,META+'SourceFile',str(filename.relative_to(ROOT)))
    save(asset);return asset

def options(skeletal):
    ui=unreal.FbxImportUI();kind=unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else unreal.FBXImportType.FBXIT_STATIC_MESH
    for k,v in dict(import_materials=False,import_textures=False,import_mesh=True,import_as_skeletal=skeletal,import_animations=False,create_physics_asset=False,automated_import_should_detect_type=False,mesh_type_to_import=kind,original_import_type=kind).items():ui.set_editor_property(k,v)
    data=ui.get_editor_property('skeletal_mesh_import_data' if skeletal else 'static_mesh_import_data')
    for k,v in dict(convert_scene=True,convert_scene_unit=True,force_front_x_axis=False,import_uniform_scale=1.,normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS,vertex_color_import_option=unreal.VertexColorImportOption.REPLACE).items():data.set_editor_property(k,v)
    if skeletal:
        data.set_editor_property('preserve_smoothing_groups',True)
        data.set_editor_property('update_skeleton_reference_pose',False)
    else:
        for k,v in dict(combine_meshes=True,auto_generate_collision=False,generate_lightmap_u_vs=False,build_nanite=False,transform_vertex_to_absolute=True).items():data.set_editor_property(k,v)
    return ui

def material(texture):
    folder=BASE+'/'+UNIT+'/Materials';name='M_'+UNIT+'_Armor'
    mat=owned(unreal.load_asset(folder+'/'+name))
    if not mat:
        LIB.make_directory(folder);mat=TOOLS.create_asset(name,folder,unreal.Material,unreal.MaterialFactoryNew())
    mat.set_editor_property('used_with_skeletal_mesh',True)
    mat.set_editor_property('used_with_instanced_static_meshes',True)
    edit=unreal.MaterialEditingLibrary;edit.delete_all_material_expressions(mat)
    tex=edit.create_material_expression(mat,unreal.MaterialExpressionTextureSample,-800,0)
    tex.set_editor_property('texture',texture);tex.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    # Keep authored palette and line work independent of studio lighting.
    edit.connect_material_property(tex,'RGB',unreal.MaterialProperty.MP_BASE_COLOR)
    for name,value,prop,y in [('Roughness',.78,unreal.MaterialProperty.MP_ROUGHNESS,180),('Metallic',0.,unreal.MaterialProperty.MP_METALLIC,300),('Specular',.22,unreal.MaterialProperty.MP_SPECULAR,420)]:
        n=edit.create_material_expression(mat,unreal.MaterialExpressionScalarParameter,-400,y)
        n.set_editor_property('parameter_name',name);n.set_editor_property('default_value',value);edit.connect_material_property(n,'',prop)
    lift=edit.create_material_expression(mat,unreal.MaterialExpressionScalarParameter,-800,-220)
    lift.set_editor_property('parameter_name','ShadowColorLift');lift.set_editor_property('default_value',.10)
    mul=edit.create_material_expression(mat,unreal.MaterialExpressionMultiply,-400,-100)
    edit.connect_material_expressions(tex,'RGB',mul,'A');edit.connect_material_expressions(lift,'',mul,'B')
    edit.connect_material_property(mul,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.recompile_material(mat);save(mat);return mat

def run():
    report=json.loads((SOURCE/(UNIT+'_delivery.json')).read_text(encoding='utf8'))
    folder=BASE+'/'+UNIT
    tex=imported(SOURCE/('T_'+UNIT+'_BaseColor.png'),folder+'/Textures/T_'+UNIT+'_BaseColor')
    tex.set_editor_property('srgb',True);tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_DEFAULT);save(tex)
    mat=material(tex);results=[]
    for skeletal in (False,True):
        name='SK_'+UNIT if skeletal else 'SM_'+UNIT+'_Crowd'
        mesh=imported(SOURCE/(name+'.fbx'),folder+'/Meshes/'+name,options(skeletal),unreal.FbxFactory())
        if skeletal:
            slots=list(mesh.get_editor_property('materials'))
            if len(slots)!=1:raise RuntimeError('Unexpected material count')
            slots[0].set_editor_property('material_interface',mat);mesh.set_editor_property('materials',slots)
            skeleton=mesh.get_editor_property('skeleton');save(skeleton)
            bones=[str(b.bone_name) for b in unreal.SkeletonService.list_bones(mesh.get_path_name())]
            required=set(report['bone_names']);extra_roots=set(bones)-required
            if not required.issubset(bones) or extra_roots not in (set(),{'Rig_'+UNIT}):raise RuntimeError('Skeleton mismatch '+str(bones))
            bounds=mesh.get_imported_bounds();extra={'bones':bones,'skeleton':skeleton.get_path_name(),'fbx_container_root':list(extra_roots)}
        else:
            mesh.set_material(0,mat);sub=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
            build=sub.get_lod_build_settings(mesh,0);build.set_editor_property('use_full_precision_u_vs',True)
            build.set_editor_property('recompute_normals',False);build.set_editor_property('recompute_tangents',False)
            build.set_editor_property('build_scale3d',unreal.Vector(1,1,1));sub.set_lod_build_settings(mesh,0,build)
            bounds=mesh.get_bounds();extra={'lod_count':sub.get_lod_count(mesh),'triangles':mesh.get_num_triangles(0),'uv_channels':sub.get_num_uv_channels(mesh,0)}
        size=list((bounds.box_extent*2).to_tuple());expected=[(report['bounds_m'][1][i]-report['bounds_m'][0][i])*100 for i in range(3)]
        # FBX forward-axis conversion must be measured; never disguise it with
        # component scaling. Rotation compensation is handled during import.
        error=max(abs(a-b) for a,b in zip(size,expected))
        extra.update(dimensions_cm=size,expected_dimensions_cm=expected,max_dimension_error_cm=error)
        if error>.2:raise RuntimeError('Imported axis/size mismatch '+json.dumps(extra))
        save(mesh);results.append({'path':mesh.get_path_name(),'class':mesh.get_class().get_name(),**extra})
    return {'success':True,'unit':UNIT,'assets':results,'material':mat.get_path_name(),'texture':tex.get_path_name(),'engine':unreal.SystemLibrary.get_engine_version(),'stage':'Imported art assets; gameplay bindings unchanged'}

def main():
    global UNIT
    command=unreal.SystemLibrary.get_command_line()
    if '-TacticalModelImportWorker' not in command:raise RuntimeError('Use the isolated import worker')
    match=re.search(r'-TacticalUnit=(Sweeper|WarMachine)',command)
    if not match:raise RuntimeError('Specify one owned tactical unit')
    UNIT=match.group(1)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    cvar='Interchange.FeatureFlags.Import.Enable';previous=unreal.SystemLibrary.get_console_variable_int_value(cvar)
    unreal.SystemLibrary.execute_console_command(world,cvar+' 0')
    try:result=run()
    except Exception:result={'success':False,'unit':UNIT,'traceback':traceback.format_exc()};unreal.log_error(result['traceback'])
    finally:unreal.SystemLibrary.execute_console_command(world,cvar+' '+str(previous))
    (SOURCE/(UNIT+'_ue_import.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    unreal.log('TACTICAL_IMPORT_RESULT '+json.dumps(result,ensure_ascii=False))
    unreal.SystemLibrary.quit_editor()

if __name__=='__main__':main()
