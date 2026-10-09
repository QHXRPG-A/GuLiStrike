"""Framing diagnosis only. Uses the unchanged source asset and transient capture."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008/Images/AAProjectionDiagnostic')
OUT.mkdir(parents=True, exist_ok=True)


def run():
    world = next(s.get_world() for s in unreal.ObjectIterator(unreal.GuLiLocalTeamColorSubsystem)
                 if s.get_world() and 'UEDPIE_2' in s.get_world().get_path_name())
    actor = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
                 if a.actor_has_tag('GuLi.ModelRuntimePaintSample') and a.actor_has_tag('ModelId=2001') and a.actor_has_tag('Relation=own'))
    camera = unreal.GuLiTeleportQALibrary.create_capture(world)
    capture = camera.capture_component2d
    capture.capture_every_frame = False
    capture.capture_on_movement = False
    capture.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
    capture.primitive_render_mode = unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST
    capture.show_only_actors = [actor]
    target = unreal.RenderingLibrary.create_render_target2d(world, 900, 800, unreal.TextureRenderTargetFormat.RTF_RGBA8, unreal.LinearColor(.1,.13,.15,1), False, False)
    target.target_gamma = 2.2
    capture.texture_target = target
    center, extent = actor.get_actor_bounds(False, True)
    radius = max(extent.x, extent.y, extent.z)
    for mode, multiple in [('perspective', 1), ('orthographic', 10), ('orthographic', 100)]:
        position = center + unreal.Vector(1,1,.8) * radius * 5 * multiple
        camera.set_actor_location_and_rotation(position, unreal.MathLibrary.find_look_at_rotation(position, center), False, True)
        capture.projection_type = unreal.CameraProjectionMode.PERSPECTIVE if mode == 'perspective' else unreal.CameraProjectionMode.ORTHOGRAPHIC
        capture.fov_angle = 35
        capture.ortho_width = radius * 3.25 * multiple
        capture.capture_scene()
        unreal.RenderingLibrary.export_render_target(world, target, str(OUT), mode + '_' + str(multiple) + '.png')
    camera.destroy_actor()
    return {'captured': True}


unreal.MCPythonHelper.submit_result(json.dumps(run()))
