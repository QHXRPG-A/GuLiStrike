"""Capture the real PIE world and existing VAT instances without changing a model pose."""
import json
from pathlib import Path
import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc = unreal.GameplayStatics.get_player_controller(world, 0)
assert world and pc and pc.can_issue_commander_orders()
state = json.loads(unreal.GuLiPioneerQALibrary.snapshot(pc))
unit = next(row for row in state['units'] if row['id'] == PIONEER_CAPTURE_ID)
origin = unreal.Vector(*unit['position'])
capture = unreal.GuLiTeleportQALibrary.create_capture(world)
component = capture.capture_component2d
for key, value in dict(capture_every_frame=False, capture_on_movement=False,
                       always_persist_rendering_state=True,
                       capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,
                       fov_angle=45).items():
    component.set_editor_property(key, value)
settings = component.get_editor_property('post_process_settings')
for key, value in dict(auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
                       auto_exposure_bias=0, auto_exposure_apply_physical_camera_exposure=False,
                       motion_blur_amount=0, bloom_intensity=0).items():
    settings.set_editor_property('override_' + key, True)
    settings.set_editor_property(key, value)
component.set_editor_property('post_process_settings', settings)
component.set_editor_property('post_process_blend_weight', 1)
render_target = unreal.RenderingLibrary.create_render_target2d(
    world, 2048, 2048, unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB,
    unreal.LinearColor(0, 0, 0, 1), False, False)
component.set_editor_property('texture_target', render_target)
if globals().get('PIONEER_CAPTURE_MODELS_ONLY', False):
    component.set_editor_property('primitive_render_mode', unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GuLiCommanderPresentationActor):
        for instance_component in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
            mesh = instance_component.get_editor_property('static_mesh')
            if mesh and 'SM_Pioneer_VAT' in mesh.get_path_name():
                component.show_only_component(instance_component)
target = origin + unreal.Vector(0, 0, 230)
position = target + unreal.Vector(1100, -1400, 1100)
capture.set_actor_location_and_rotation(position, unreal.MathLibrary.find_look_at_rotation(position, target), False, True)
output = Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Delivery_UE_v1/ReviewImages')
capture_job = {'ticks': 0, 'name': PIONEER_CAPTURE_NAME, 'done': False}

def capture_tick(delta):
    component.capture_scene()
    capture_job['ticks'] += 1
    if capture_job['ticks'] >= 8:
        unreal.RenderingLibrary.export_render_target(world, render_target, str(output), capture_job['name'] + '.png')
        capture_job['done'] = True
        unreal.unregister_slate_post_tick_callback(capture_job['handle'])
        capture.destroy_actor()

capture_job['handle'] = unreal.register_slate_post_tick_callback(capture_tick)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'capture_queued': PIONEER_CAPTURE_NAME,
                                              'real_unit': PIONEER_CAPTURE_ID, 'native_position': unit['position'],
                                              'world_time': state['world_time']}))
