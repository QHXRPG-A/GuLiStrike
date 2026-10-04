"""Import the two approved static meshes; preserve production sockets/materials/collision.

Run through Scripts/ue_exec.py in the live editor. No skeletal asset is imported.
"""
import json, sys, traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT/'ArtSource/MechanicalAnimation_20260929'
LIB = unreal.EditorAssetLibrary
SUB = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
sys.path.insert(0, str(ROOT/'Scripts'))
from import_tactical_handbuilt_models import options
REPORT = {'success': False, 'assets': []}
WORLD = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
VAR = 'Interchange.FeatureFlags.Import.Enable'
OLD = unreal.SystemLibrary.get_console_variable_int_value(VAR)

def run():
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    for unit in globals().get('GULI_RIGID_UNITS',['WarMachine','Sweeper']):
        spec=json.loads((OUT/unit/'manifest.json').read_text(encoding='utf-8'))
        folder='/Game/Commander/Units/Tactical/Cel/'+unit+'/Meshes'
        source_path=folder+'/SM_'+unit+'_Cel'
        name='SM_'+unit+'_Rigid'; path=folder+'/'+name
        # The accepted source may have unsaved work from another task. Read it, but
        # give the runtime WPO derivative its own identity instead of overwriting it.
        mesh=unreal.load_asset(path) if LIB.does_asset_exist(path) else LIB.duplicate_asset(source_path,path)
        assert isinstance(mesh,unreal.StaticMesh)
        # Reimport must preserve the current rigid derivative's materials/sockets/settings.
        # The historical Cel asset can be older than later hover and missile work.
        component=unreal.new_object(unreal.StaticMeshComponent);component.set_static_mesh(mesh)
        sockets=[]
        for n in component.get_all_socket_names():
            s=mesh.find_socket(n)
            sockets.append((str(n), s.relative_location, s.relative_rotation, s.relative_scale))
        materials=list(mesh.static_materials)
        setup=mesh.get_editor_property('body_setup')
        collision=setup.get_editor_property('agg_geom');trace=setup.get_editor_property('collision_trace_flag')
        before=list((mesh.get_bounds().box_extent*2).to_tuple())
        screens=list(SUB.get_lod_screen_sizes(mesh));triangles=[mesh.get_num_triangles(i) for i in range(SUB.get_lod_count(mesh))]
        reductions=[SUB.get_lod_reduction_settings(mesh,i) for i in range(SUB.get_lod_count(mesh))]
        task=unreal.AssetImportTask()
        for k,v in dict(filename=spec['files']['fbx'],destination_path=folder,destination_name=name,
            automated=True,async_=False,replace_existing=True,replace_existing_settings=False,save=False).items():task.set_editor_property(k,v)
        ui=options(False);ui.set_editor_property('reset_to_fbx_on_material_conflict',False)
        data=ui.get_editor_property('static_mesh_import_data');data.set_editor_property('reorder_material_to_fbx_order',False)
        data.set_editor_property('generate_lightmap_u_vs',False)
        task.set_editor_property('options',ui);task.set_editor_property('factory',unreal.FbxFactory())
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]);assert task.imported_object_paths
        mesh=unreal.load_asset(path);assert isinstance(mesh,unreal.StaticMesh)
        mesh.set_editor_property('static_materials',materials)
        setup=mesh.get_editor_property('body_setup');setup.set_editor_property('agg_geom',collision);setup.set_editor_property('collision_trace_flag',trace)
        for n,loc,rot,scale in sockets:
            s=mesh.find_socket(n)
            if not s:s=unreal.new_object(unreal.StaticMeshSocket,outer=mesh);s.set_editor_property('socket_name',n);mesh.add_socket(s)
            for k,v in [('relative_location',loc),('relative_rotation',rot),('relative_scale',scale)]:s.set_editor_property(k,v)
        for n,loc in spec['sockets_cm'].items():
            s=mesh.find_socket(n)
            if not s:s=unreal.new_object(unreal.StaticMeshSocket,outer=mesh);s.set_editor_property('socket_name',n);mesh.add_socket(s)
            s.set_editor_property('relative_location',unreal.Vector(*loc))
            s.set_editor_property('relative_rotation',unreal.Rotator())
            radius=spec['wheel_radii_cm'][['FL','FR','RL','RR'].index(n.removeprefix('Rigid_Wheel_'))] if n.startswith('Rigid_Wheel_') else 1
            s.set_editor_property('relative_scale',unreal.Vector(radius,1,1))
        assert SUB.get_lod_count(mesh)==len(reductions)
        for i,reduction in enumerate(reductions):
            # The former ~300/70-triangle far LODs erase every support arm.
            # Retain enough geometry for all rigid joints; screen thresholds stay unchanged.
            if spec.get('rigid_version')==4 and i in (2,3):
                reduction.set_editor_property('percent_triangles',max(reduction.percent_triangles,.04 if i==2 else .02))
                reduction.set_editor_property('base_lod_model',0)
            SUB.set_lod_reduction_settings(mesh,i,reduction)
        for i in range(SUB.get_lod_count(mesh)):
            build=SUB.get_lod_build_settings(mesh,i)
            for k,v in dict(use_full_precision_u_vs=True,generate_lightmap_u_vs=False,recompute_normals=False,recompute_tangents=False,build_scale3d=unreal.Vector(1,1,1)).items():build.set_editor_property(k,v)
            SUB.set_lod_build_settings(mesh,i,build)
        # Includes full upper yaw, extreme gun pitch, recoil and all disc/wheel rotations.
        mesh.set_editor_property('positive_bounds_extension',unreal.Vector(850,500,1200) if unit=='WarMachine' else unreal.Vector(100,50,400))
        mesh.set_editor_property('negative_bounds_extension',unreal.Vector(850,500,1200) if unit=='WarMachine' else unreal.Vector(100,50,400))
        LIB.set_metadata_tag(mesh,'GuLi.Animation','RigidWPO.v1; UV1 pivotXY; UV2 pivotZ,part; no skeleton')
        if unit=='WarMachine':
            hover_file=ROOT/'ArtSource/WarMachineHover_20260929/nozzles.json'
            if hover_file.exists():
                for item in json.loads(hover_file.read_text(encoding='utf-8'))['nozzles']:
                    s=mesh.find_socket(item['name'])
                    if not s:s=unreal.new_object(unreal.StaticMeshSocket,outer=mesh);s.set_editor_property('socket_name',item['name']);mesh.add_socket(s)
                    s.set_editor_property('relative_location',unreal.Vector(*item['location_cm']))
                    s.set_editor_property('relative_rotation',unreal.Rotator(-90,0,0))
                    s.set_editor_property('relative_scale',unreal.Vector(item['diameter_cm'],1,1))
                mesh.set_editor_property('positive_bounds_extension',unreal.Vector(950,600,1900))
                mesh.set_editor_property('negative_bounds_extension',unreal.Vector(950,600,1300))
                LIB.set_metadata_tag(mesh,'GuLi.Animation','RigidWPO.v2; four disc-bottom nozzles; no skeleton')
        if spec.get('missile_pod_parts'):
            LIB.set_metadata_tag(mesh,'GuLi.Animation','RigidWPO.v3; 31 floats; missile pods part10/11; four disc-bottom nozzles; no skeleton')
        for joint in spec.get('leg_joints_cm',[]):
            socket=mesh.find_socket('Rigid_LegRoot_'+joint['label'])
            socket.set_editor_property('relative_rotation',unreal.MathLibrary.make_rot_from_x(unreal.Vector(*joint['axis'])))
        if spec.get('rigid_version')==4:
            LIB.set_metadata_tag(mesh,'GuLi.Animation','RigidWPO.v4; 51 floats; legs part12..19; missile pods10/11; no skeleton')
        LIB.set_metadata_tag(mesh,'GuLi.ModelProduction.SourceFile',str(Path(spec['files']['fbx']).relative_to(ROOT)))
        assert SUB.get_num_uv_channels(mesh,0)==3
        assert LIB.save_loaded_asset(mesh,False)
        REPORT['assets'].append({'path':path,'class':mesh.get_class().get_name(),'old_dimensions_cm':before,
            'lod_triangles':[mesh.get_num_triangles(i) for i in range(SUB.get_lod_count(mesh))],
            'uv_channels':[SUB.get_num_uv_channels(mesh,i) for i in range(SUB.get_lod_count(mesh))],
            'rigid_sockets':spec['sockets_cm'],'preserved_sockets':[x[0] for x in sockets]})
    REPORT['success']=True

try:
    unreal.SystemLibrary.execute_console_command(WORLD,VAR+' 0')
    run()
except: REPORT['error']=traceback.format_exc()
finally:
    unreal.SystemLibrary.execute_console_command(WORLD,VAR+' '+str(OLD))
    (ROOT/'ArtSource/WarMachineTurn_20260930/ue-import.json').write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(REPORT))
