"""Preview-only Nanite fallback comparison and actual selected-unit camera."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008')


def run():
    world = next(s.get_world() for s in unreal.ObjectIterator(unreal.GuLiLocalTeamColorSubsystem)
                 if s.get_world() and 'UEDPIE_2' in s.get_world().get_path_name())
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
    flags = []
    for actor in actors:
        if actor.actor_has_tag('GuLi.ModelRuntimePaintSample') and actor.actor_has_tag('ModelId=2001'):
            component = actor.get_component_by_class(unreal.StaticMeshComponent)
            try:
                component.set_editor_property('disallow_nanite', True)
                flags.append({'actor': actor.get_path_name(), 'fallback_only_preview': component.get_editor_property('disallow_nanite')})
            except Exception as error:
                flags.append({'error': str(error)})
    camera = unreal.GuLiTeleportQALibrary.create_capture(world)
    camera.tags = ['GuLi.ModelRuntimeCamera']
    capture = camera.capture_component2d
    capture.capture_every_frame = False
    capture.capture_on_movement = False
    center = unreal.Vector(-8500, 67000, 1075.2745)
    position = center + unreal.Vector(-1000, -3600, 4100)
    camera.set_actor_location_and_rotation(position, unreal.MathLibrary.find_look_at_rotation(position, center), False, True)
    pc.set_view_target_with_blend(camera, 0)
    wm = next(a for a in actors if a.actor_has_tag('GuLi.ModelRuntimePaintSample') and a.actor_has_tag('ModelId=1002') and a.actor_has_tag('Relation=own'))
    component = wm.get_component_by_class(unreal.StaticMeshComponent)
    registry = next(s for s in unreal.ObjectIterator(unreal.GuLiModelRegistrySubsystem) if s.get_world() == world)
    before = registry.get_vector_parameter(component, 1002, 'Root', '*', 'TeamPrimary')
    component.set_custom_primitive_data_vector4(8, unreal.Vector4(0, 0, 0, 0))
    reset = registry.get_vector_parameter(component, 1002, 'Root', '*', 'TeamPrimary')
    report = {'preview_flags': flags, 'camera_center': list(center.to_tuple()), 'cpd_reset_observed': list(reset.to_tuple()), 'expected': list(before.to_tuple())}
    (OUT / 'aa-capture-diagnosis.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    return report


unreal.MCPythonHelper.submit_result(json.dumps(run()))
