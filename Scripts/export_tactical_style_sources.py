"""Export the two approved vehicle assemblies without saving the active level."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/ArtSource/TacticalStyle_20260916')
OUT.mkdir(parents=True, exist_ok=True)
(OUT/'Source').mkdir(exist_ok=True)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
selected = actors.get_selected_level_actors()
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
report = {'units': {}, 'meshes': {}, 'engine': unreal.SystemLibrary.get_engine_version()}

def vector(v): return [v.x, v.y, v.z]
def transform(t):
    return {'translation': vector(t.translation), 'rotation': [t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w], 'scale': vector(t.scale3d)}

for key, bpname, old in [('Sweeper','Wheeled_Bug_Blueprint','SM_CommanderFourFRobot_Crowd'),('WarMachine','Quad_Lvl3_Blueprint','SM_WM01_Crowd')]:
    bp = unreal.load_asset('/Game/MC_Vehicle_Constructor/Blueprints/'+bpname)
    assert bp
    actor = actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector(0,0,-1000000),transient=True)
    try:
        row = {'source_blueprint': bp.get_path_name(), 'components': []}
        for comp in actor.get_components_by_class(unreal.MeshComponent):
            mesh = comp.get_skeletal_mesh_asset() if isinstance(comp,unreal.SkeletalMeshComponent) else comp.get_editor_property('static_mesh') if isinstance(comp,unreal.StaticMeshComponent) else None
            if not mesh: continue
            path = mesh.get_path_name()
            item = {'name': comp.get_name(), 'mesh':path, 'transform':transform(unreal.MathLibrary.make_relative_transform(comp.get_world_transform(),actor.get_actor_transform())), 'materials':[m.get_path_name() if m else None for m in comp.get_materials()]}
            if isinstance(comp,unreal.SkeletalMeshComponent):
                item['bones'] = {str(comp.get_bone_name(i)):transform(comp.get_socket_transform(comp.get_bone_name(i),unreal.RelativeTransformSpace.RTS_COMPONENT)) for i in range(comp.get_num_bones())}
            row['components'].append(item)
            if path not in report['meshes']:
                dest=OUT/'Source'/(mesh.get_name()+'.fbx')
                task=unreal.AssetExportTask();task.object=mesh;task.filename=str(dest);task.automated=True;task.prompt=False;task.replace_identical=True
                task.exporter=unreal.SkeletalMeshExporterFBX() if isinstance(mesh,unreal.SkeletalMesh) else unreal.StaticMeshExporterFBX()
                options=unreal.FbxExportOption();options.ascii=False;options.collision=False;options.level_of_detail=False;task.options=options
                assert unreal.Exporter.run_asset_export_task(task), path
                report['meshes'][path]={'file':str(dest),'skeletal':isinstance(mesh,unreal.SkeletalMesh)}
        oldmesh=unreal.load_asset('/Game/Commander/Units/'+old)
        b=oldmesh.get_bounds();row['old_bounds']={'origin':vector(b.origin),'extent':vector(b.box_extent)}
        row['old_triangles']=[oldmesh.get_num_triangles(i) for i in range(oldmesh.get_num_lods())]
        report['units'][key]=row
    finally:
        actors.destroy_actor(actor)
actors.set_selected_level_actors(selected)
(OUT/'source_manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'units':{k:{'components':len(v['components']),'old_bounds':v['old_bounds'],'old_triangles':v['old_triangles']} for k,v in report['units'].items()},'meshes':len(report['meshes'])}))
