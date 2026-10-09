"""Render the saved ring references in the editor; never enter gameplay or save capture actors."""
import json
import time
import traceback
from pathlib import Path
import unreal

out = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / 'Artifacts/MassRingWidth20cm20261007'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_editor_world()
assert editor.get_game_world() is None
api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rings = [actor for actor in api.get_all_level_actors()
         if 'MassRingWidth20cmReview20261007' in [str(tag) for tag in actor.tags]]
assert len(rings) == 4
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
capture = api.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, 0, 0), transient=True)
component = capture.capture_component2d
for key, value in dict(capture_every_frame=False, capture_on_movement=False,
                       always_persist_rendering_state=True,
                       capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,
                       projection_type=unreal.CameraProjectionMode.ORTHOGRAPHIC,
                       primitive_render_mode=unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST).items():
    component.set_editor_property(key, value)
target = unreal.RenderingLibrary.create_render_target2d(
    world, 1024, 1024, unreal.TextureRenderTargetFormat.RTF_RGBA8,
    unreal.LinearColor(0, 0, 0, 1), False, False)
component.set_editor_property('texture_target', target)
pp = component.get_editor_property('post_process_settings')
for key, value in dict(auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
                       auto_exposure_apply_physical_camera_exposure=False,
                       auto_exposure_bias=0.0, bloom_intensity=0.0, motion_blur_amount=0.0).items():
    pp.set_editor_property('override_' + key, True)
    pp.set_editor_property(key, value)
component.set_editor_property('post_process_settings', pp)
component.set_editor_property('post_process_blend_weight', 1.0)
component.set_editor_property('show_flag_settings', [
    unreal.EngineShowFlagsSetting(show_flag_name='Fog', enabled=False),
    unreal.EngineShowFlagsSetting(show_flag_name='Atmosphere', enabled=False)])
fixture = api.spawn_actor_from_class(unreal.GuLiCommanderPresentationActor, unreal.Vector(0, 0, 0), transient=True)
instances = next(value for value in fixture.get_components_by_class(unreal.InstancedStaticMeshComponent)
                 if value.get_name() == 'RingInstances')
instances.set_static_mesh(unreal.load_asset('/Game/Commander/Units/SM_CommanderUnitRing'))
instances.set_material(0, unreal.load_asset('/Game/Commander/UI/M_CommanderUnitRing'))
instances.set_visibility(True)
instances.set_hidden_in_game(False)
component.clear_show_only_components()
component.show_only_component(instances)
rows = []
state = {'job': 0, 'ticks': 0, 'started': time.time()}
material = unreal.load_asset('/Game/Commander/UI/M_CommanderUnitRing')
environment = {}
for key in ('shading_model', 'blend_mode', 'disable_depth_test', 'use_translucency_vertex_fog',
            'apply_cloud_fogging', 'compute_fog_per_pixel', 'translucency_pass', 'used_with_instanced_static_meshes'):
    environment[key] = str(material.get_editor_property(key))
(out / 'environment-readback.json').write_text(json.dumps(environment, indent=2), encoding='utf-8')

def apply_actor(actor):
    location = actor.get_actor_location()
    radius = actor.get_actor_scale3d().x * 48.0
    instances.clear_instances()
    index = instances.add_instance(unreal.Transform(
        location=location, scale=unreal.Vector(radius / 48.0, radius / 48.0, 0.004)), True)
    for data_index, value in enumerate((1.0, 0.04, 0.03, 0.72)):
        instances.set_custom_data_value(index, data_index, value, data_index == 3)
    instances.set_visibility(True)
    component.set_editor_property('ortho_width', radius * 2.4)
    eye = location + unreal.Vector(0, 0, 5000)
    capture.set_actor_location(eye, False, False)
    capture.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, location), False)
    component.set_editor_property('camera_cut_this_frame', True)

def finish(error=None):
    if state.get('handle'):
        unreal.unregister_slate_post_tick_callback(state['handle'])
    stats = unreal.MaterialEditingLibrary.get_statistics(material)
    report = {'captured': error is None, 'error': error, 'gameplay_started': False, 'frames': rows,
              'capture_fog_disabled': True, 'material_environment': environment,
              'shader_statistics': {key: stats.get_editor_property(key) for key in
                  ('num_vertex_shader_instructions', 'num_pixel_shader_instructions', 'num_samplers')}}
    (out / 'render-readback.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    api.destroy_actor(capture)
    api.destroy_actor(fixture)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)

def tick(dt):
    try:
        if time.time() - state['started'] > 180:
            raise RuntimeError('Editor capture frame timeout')
        if state['ticks'] < 6:
            component.capture_scene()
            state['ticks'] += 1
            return
        actor = rings[state['job']]
        radius = actor.get_actor_scale3d().x * 48.0
        filename = actor.get_actor_label() + '.png'
        unreal.RenderingLibrary.export_render_target(world, target, str(out), filename)
        rows.append({'actor': actor.get_actor_label(), 'radius_cm': radius,
                     'ortho_width_cm': radius * 2.4, 'image': str(out / filename),
                     'camera_rotation': list(capture.get_actor_rotation().to_tuple()),
                     'instance_rgba': [1.0, 0.04, 0.03, 0.72]})
        state['job'] += 1
        state['ticks'] = 0
        if state['job'] == len(rings):
            finish()
        else:
            apply_actor(rings[state['job']])
    except Exception:
        finish(traceback.format_exc())

apply_actor(rings[0])
state['handle'] = unreal.register_slate_post_tick_callback(tick)
unreal._mass_ring_capture = state
