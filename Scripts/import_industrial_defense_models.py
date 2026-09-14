"""Import the approved V3 set into its three new, owned UE asset roots.

Run from a normal UnrealEditor -ExecutePythonScript process with
-IndustrialDefenseImportWorker. Do not run this importer in an MCP callback.
It saves only its own packages and never changes the active project's maps.
"""
import unreal
import json,hashlib,traceback
from pathlib import Path

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
SOURCE=ROOT/'ArtSource/Buildings/IndustrialDefenseSet/delivery_hardsurface'
OUT=ROOT/'outputs/hardsurface-models-20260914'
OWNER='GuLiStrike.IndustrialDefenseSet.V3'
META='GuLi.ModelProduction.'
BASE='/Game/GuLiStrike/Buildings'
NAMES=['RedOreRefinery','ShieldGenerator','HeavyDefenseCannon']
LIB=unreal.EditorAssetLibrary
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()

def write(name,value):
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/name).write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')

def owned(asset):
    if asset and LIB.get_metadata_tag(asset,META+'Owner')!=OWNER:
        raise RuntimeError('Existing asset is not owned by this import: '+asset.get_path_name())
    return asset

def save(asset):
    if not asset.get_path_name().startswith(tuple(BASE+'/'+n+'/' for n in NAMES)):
        raise RuntimeError('Save outside approved asset roots: '+asset.get_path_name())
    LIB.set_metadata_tag(asset,META+'Owner',OWNER)
    LIB.set_metadata_tag(asset,META+'Style','Planar hard surfaces / narrow chamfers / regular markings')
    if not LIB.save_loaded_asset(asset,False):raise RuntimeError('Save failed '+asset.get_path_name())

def imported(filename,path,options=None,factory=None):
    filename=Path(filename)
    digest=hashlib.sha256(filename.read_bytes()).hexdigest()
    existing=owned(unreal.load_asset(path))
    if existing and LIB.get_metadata_tag(existing,META+'SourceSHA256')==digest:return existing
    folder,name=path.rsplit('/',1);LIB.make_directory(folder)
    task=unreal.AssetImportTask()
    for key,value in dict(filename=str(filename),destination_path=folder,destination_name=name,
                          automated=True,async_=False,replace_existing=bool(existing),
                          replace_existing_settings=True,save=False).items():task.set_editor_property(key,value)
    if options:task.set_editor_property('options',options)
    if factory:task.set_editor_property('factory',factory)
    TOOLS.import_asset_tasks([task])
    asset=unreal.load_asset(path)
    if not asset:raise RuntimeError('Import missing '+path+'; got '+str(task.imported_object_paths))
    LIB.set_metadata_tag(asset,META+'SourceSHA256',digest)
    LIB.set_metadata_tag(asset,META+'SourceFile',str(filename.relative_to(ROOT)))
    save(asset)
    return asset

def options(skeletal=False,animation=False,skeleton=None):
    ui=unreal.FbxImportUI()
    kind=unreal.FBXImportType.FBXIT_ANIMATION if animation else unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else unreal.FBXImportType.FBXIT_STATIC_MESH
    for key,value in dict(import_materials=False,import_textures=False,import_mesh=not animation,
                          import_as_skeletal=skeletal,import_animations=animation,
                          create_physics_asset=False,automated_import_should_detect_type=False,
                          mesh_type_to_import=kind,original_import_type=kind).items():ui.set_editor_property(key,value)
    if skeleton:ui.set_editor_property('skeleton',skeleton)
    data=ui.get_editor_property('anim_sequence_import_data' if animation else 'skeletal_mesh_import_data' if skeletal else 'static_mesh_import_data')
    for key,value in dict(convert_scene=True,convert_scene_unit=True,force_front_x_axis=False,import_uniform_scale=1.0).items():data.set_editor_property(key,value)
    if animation:
        data.set_editor_property('animation_length',unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        data.set_editor_property('use_default_sample_rate',False);data.set_editor_property('custom_sample_rate',30)
    else:
        data.set_editor_property('normal_import_method',unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
        data.set_editor_property('vertex_color_import_option',unreal.VertexColorImportOption.REPLACE)
        if skeletal:
            data.set_editor_property('preserve_smoothing_groups',True)
            data.set_editor_property('update_skeleton_reference_pose',False)
            data.set_editor_property('use_t0_as_ref_pose',False)
        else:
            for key,value in dict(combine_meshes=True,auto_generate_collision=False,generate_lightmap_u_vs=True,
                                  build_nanite=False,transform_vertex_to_absolute=True).items():data.set_editor_property(key,value)
    return ui

def make_material(name):
    folder=BASE+'/'+name
    path=folder+'/Materials/M_'+name+'_HardSurface'
    mat=owned(unreal.load_asset(path))
    if not mat:
        LIB.make_directory(folder+'/Materials')
        mat=TOOLS.create_asset('M_'+name+'_HardSurface',folder+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
        if not mat:raise RuntimeError('Material creation failed '+path)
        save(mat)
    edit=unreal.MaterialEditingLibrary
    edit.delete_all_material_expressions(mat)
    maps={}
    for idx,role in enumerate(['BaseColor','ORM','NormalDX','Emissive']):
        texname='T_'+name+'_'+role
        tex=imported(SOURCE/name/'Textures'/(texname+'.png'),folder+'/Textures/'+texname)
        srgb=role in ['BaseColor','Emissive']
        compression=unreal.TextureCompressionSettings.TC_NORMALMAP if role=='NormalDX' else unreal.TextureCompressionSettings.TC_MASKS if role=='ORM' else unreal.TextureCompressionSettings.TC_DEFAULT
        tex.set_editor_property('srgb',srgb);tex.set_editor_property('compression_settings',compression)
        if role=='NormalDX':tex.set_editor_property('flip_green_channel',False)
        save(tex)
        node=edit.create_material_expression(mat,unreal.MaterialExpressionTextureSample,-600,idx*230)
        node.set_editor_property('texture',tex)
        sampler=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if role=='NormalDX' else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS if role=='ORM' else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
        node.set_editor_property('sampler_type',sampler)
        links={'BaseColor':[('RGB',unreal.MaterialProperty.MP_BASE_COLOR)],
               'ORM':[('R',unreal.MaterialProperty.MP_AMBIENT_OCCLUSION),('G',unreal.MaterialProperty.MP_ROUGHNESS),('B',unreal.MaterialProperty.MP_METALLIC)],
               'NormalDX':[('RGB',unreal.MaterialProperty.MP_NORMAL)],
               'Emissive':[('RGB',unreal.MaterialProperty.MP_EMISSIVE_COLOR)]}[role]
        for channel,prop in links:
            if not edit.connect_material_property(node,channel,prop):raise RuntimeError('Material connection failed '+role)
        maps[role]={'path':tex.get_path_name(),'srgb':srgb,'compression':str(compression)}
    mat.set_editor_property('used_with_skeletal_mesh',name=='HeavyDefenseCannon')
    edit.recompile_material(mat);save(mat)
    return mat,maps

def install():
    source_report=json.loads((SOURCE/'Validation_Report.json').read_text(encoding='utf-8'))
    if not source_report['validation']['passed']:raise RuntimeError('Source validation has not passed')
    results={}
    for name in NAMES:
        write('import_status.json',{'state':'running','stage':name,'completed':list(results)})
        mat,textures=make_material(name)
        skeletal=name=='HeavyDefenseCannon';prefix='SK_' if skeletal else 'SM_'
        path=BASE+'/'+name+'/Meshes/'+prefix+name
        mesh=imported(SOURCE/name/(prefix+name+'.fbx'),path,options(skeletal),unreal.FbxFactory())
        if skeletal:
            materials=list(mesh.get_editor_property('materials'))
            if len(materials)!=1:raise RuntimeError('Unexpected skeletal material slots '+str(len(materials)))
            materials[0].set_editor_property('material_interface',mat);mesh.set_editor_property('materials',materials)
            skeleton=mesh.get_editor_property('skeleton')
            if not skeleton:raise RuntimeError('No cannon skeleton')
            # The FBX importer creates this child asset as part of the owned mesh import.
            save(skeleton)
            skeleton_path=BASE+'/'+name+'/Meshes/SKEL_'+name
            if skeleton.get_path_name().split('.')[0]!=skeleton_path:
                if unreal.load_asset(skeleton_path):raise RuntimeError('Skeleton destination already exists')
                if not LIB.rename_asset(skeleton.get_path_name(),skeleton_path):raise RuntimeError('Skeleton rename failed')
            save(skeleton);save(mesh)
            animation=imported(SOURCE/name/('SK_'+name+'_AimDemo.fbx'),BASE+'/'+name+'/Animations/A_'+name+'_AimDemo',options(True,True,skeleton),unreal.FbxFactory())
            import runpy
            animation_units=runpy.run_path(str(ROOT/'Scripts/normalize_industrial_defense_animation.py'))['normalize_animation'](animation,mesh)
            bounds=mesh.get_imported_bounds()
            bones=list(unreal.SkeletonService.list_bones(path))
            bone_names=[str(b.bone_name) for b in bones]
            if bone_names!=['root','base_yaw','barrel_pitch']:raise RuntimeError('Incorrect cannon bones '+str(bone_names))
            modifier=unreal.SkinWeightModifier()
            if not modifier.set_skeletal_mesh(mesh):raise RuntimeError('Cannot read imported skin weights')
            weighted={n:0 for n in bone_names};invalid=0
            for idx in range(modifier.get_num_vertices()):
                weights={str(k):float(v) for k,v in modifier.get_vertex_weights(idx).items() if v>1e-6}
                if len(weights)!=1 or abs(sum(weights.values())-1)>1e-5:invalid+=1
                else:weighted[next(iter(weights))]+=1
            if invalid or not all(weighted.values()):raise RuntimeError('Invalid rigid weights '+str(weighted)+' invalid '+str(invalid))
            extra={'skeleton':skeleton.get_path_name(),'bones':bone_names,
                   'bone_parents':{str(b.bone_name):str(b.parent_bone_name) for b in bones},
                   'rigid_vertices':weighted,'invalid_weights':invalid,
                   'animation':animation.get_path_name(),'animation_seconds':animation.get_play_length(),'animation_units':animation_units}
            if abs(animation.get_play_length()-6.0)>.05:raise RuntimeError('Unexpected animation duration')
        else:
            mesh.set_material(0,mat)
            sub=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
            build=sub.get_lod_build_settings(mesh,0)
            build.set_editor_property('use_full_precision_u_vs',True)
            build.set_editor_property('recompute_normals',False);build.set_editor_property('recompute_tangents',False)
            sub.set_lod_build_settings(mesh,0,build)
            bounds=mesh.get_bounds();extra={}
        save(mesh)
        size=list((bounds.box_extent*2).to_tuple())
        expected=[v*100 for v in source_report['assets'][name]['dimensions_m']]
        error=max(abs(a-b) for a,b in zip(size,expected))
        if error>.15:raise RuntimeError('Unexpected imported size '+name+' '+str(size)+' vs '+str(expected))
        results[name]={'mesh':mesh.get_path_name(),'class':mesh.get_class().get_name(),'dimensions_cm':size,
                       'max_dimension_error_cm':error,'material':mat.get_path_name(),'textures':textures,**extra}
        write('import_progress.json',results)
    report={'success':True,'owner':OWNER,'engine':unreal.SystemLibrary.get_engine_version(),'assets':results}
    write('ue_import_report.json',report);write('import_status.json',{'state':'complete','completed':NAMES})
    return report

def main():
    if '-IndustrialDefenseImportWorker' not in unreal.SystemLibrary.get_command_line():
        raise RuntimeError('Use a separate -IndustrialDefenseImportWorker editor process')
    OUT.mkdir(parents=True,exist_ok=True)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    cvar='Interchange.FeatureFlags.Import.Enable';previous=unreal.SystemLibrary.get_console_variable_int_value(cvar)
    unreal.SystemLibrary.execute_console_command(world,cvar+' 0')
    try:
        print(json.dumps(install(),ensure_ascii=False))
        (OUT/'ue_import_error.txt').unlink(missing_ok=True)
    except Exception:
        error=traceback.format_exc();(OUT/'ue_import_error.txt').write_text(error,encoding='utf-8')
        write('import_status.json',{'state':'failed','error':error});unreal.log_error(error)
    finally:
        unreal.SystemLibrary.execute_console_command(world,cvar+' '+str(previous))
        unreal.SystemLibrary.quit_editor()

if __name__=='__main__':main()
