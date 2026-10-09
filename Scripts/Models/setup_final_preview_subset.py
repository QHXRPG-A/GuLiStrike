"""Only the four models whose capture directions/framing need a follow-up."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008')


def run():
    sub = next(s for s in unreal.ObjectIterator(unreal.GuLiLocalTeamColorSubsystem)
               if s.get_world() and 'UEDPIE_2' in s.get_world().get_path_name())
    world = sub.get_world()
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    own = pc.player_state.get_team()
    enemy = unreal.GuLiTeam.RED if own == unreal.GuLiTeam.BLUE else unreal.GuLiTeam.BLUE
    report = {'samples': [], 'selection_request': False}
    for index, mid in enumerate((1001, 1002, 2001, 2006)):
        for relation, team in [('own', own), ('enemy', enemy)]:
            actor = unreal.GuLiModelAuthoringLibrary.spawn_runtime_paint_sample(
                world, mid, team, unreal.Vector(-45000 + index * 6000, 48000 if relation == 'own' else 42000, 15000))
            actor.tags = ['GuLi.ModelRuntimePaintSample', 'ModelId=' + str(mid), 'Relation=' + relation]
            center, extent = actor.get_actor_bounds(False, True)
            scale = 400 / max(extent.x, extent.y, extent.z, 1)
            actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
            for component in actor.get_components_by_class(unreal.StaticMeshComponent):
                component.set_editor_property('evaluate_world_position_offset', False)
            report['samples'].append({'id': mid, 'relation': relation, 'actor': actor.get_path_name(),
                                      'raw_center': list(center.to_tuple()), 'raw_extent': list(extent.to_tuple()), 'scale': scale})
    sub.apply_local_team_colors()
    report['selection_request'] = unreal.GuLiPioneerQALibrary.select(pc, 9, False)
    for context in unreal.ObjectIterator(unreal.World):
        if 'UEDPIE_' in context.get_path_name():
            unreal.GameplayStatics.set_global_time_dilation(context, .02)
    camera = unreal.GuLiTeleportQALibrary.create_capture(world)
    camera.tags = ['GuLi.ModelRuntimeCamera']
    capture = camera.capture_component2d
    capture.capture_every_frame = False
    capture.capture_on_movement = False
    center = unreal.Vector(-8500, 67000, 1075.2745)
    position = center + unreal.Vector(-1000, -3600, 4100)
    camera.set_actor_location_and_rotation(position, unreal.MathLibrary.find_look_at_rotation(position, center), False, True)
    pc.set_view_target_with_blend(camera, 0)
    (OUT / 'final-preview-subset.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    return report


unreal.MCPythonHelper.submit_result(json.dumps(run()))
