"""Read the normal client selection acknowledgement and preview bounds."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008')


def run():
    sub = next(s for s in unreal.ObjectIterator(unreal.GuLiLocalTeamColorSubsystem)
               if s.get_world() and 'UEDPIE_2' in s.get_world().get_path_name())
    world = sub.get_world()
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    report = {'selection': json.loads(unreal.GuLiPioneerQALibrary.snapshot(pc)), 'bounds': []}
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        if not actor.actor_has_tag('GuLi.ModelRuntimePaintSample') or not actor.actor_has_tag('ModelId=2001'):
            continue
        c, e = actor.get_actor_bounds(False, True)
        component = actor.get_component_by_class(unreal.StaticMeshComponent)
        report['bounds'].append({'actor': actor.get_path_name(), 'center': list(c.to_tuple()), 'extent': list(e.to_tuple()),
                                 'scale': list(actor.get_actor_scale3d().to_tuple()),
                                 'mesh_box': str(component.static_mesh.get_bounding_box()),
                                 'wpo': component.get_editor_property('evaluate_world_position_offset')})
    report['captured'] = unreal.GuLiModelAuthoringLibrary.capture_runtime_viewport(pc, str(OUT / 'Images/selected_after_recovery.png'))
    (OUT / 'selection-after-recovery.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    return report


unreal.MCPythonHelper.submit_result(json.dumps(run()))
