"""Capture the ground-warning art candidate in UE with transient preview actors.

This is an art playback tool, not a gameplay test. No level or combat asset is saved.
"""
import json
import traceback
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/ArtSource/VFX/GroundWarning/CandidateB1')
OUT.mkdir(parents=True, exist_ok=True)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
old_camera = editor.get_level_viewport_camera_info()
world = editor.get_editor_world()
origin = unreal.Vector(0, 0, 80000)
preview = {'actors': [], 'frame': -25, 'frames': [], 'fps': 30, 'period': .8, 'radius_cm': 800}

def spawn(cls, location, rotation=unreal.Rotator()):
    actor = actors.spawn_actor_from_class(cls, location, rotation, transient=True)
    actor.set_actor_location_and_rotation(location, rotation, False, True)
    preview['actors'].append(actor)
    return actor

floor = spawn(unreal.StaticMeshActor, origin-unreal.Vector(0, 0, 25))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(120, 120, .5))
floor.static_mesh_component.set_material(0, unreal.load_asset('/Game/Commander/Units/Tactical/Preview/Sweeper/M_ReviewFloor'))
floor.static_mesh_component.set_cast_shadow(False)
light = spawn(unreal.DirectionalLight, origin+unreal.Vector(0, 0, 1500), unreal.Rotator(pitch=-65, yaw=-30))
light.light_component.set_intensity(4.)
decal = spawn(unreal.DecalActor, origin, unreal.Rotator(pitch=-90))
component = decal.decal
component.set_editor_property('decal_size', unreal.Vector(400, 800/.96, 800/.96))
component.set_editor_property('fade_screen_size', 0)
component.set_decal_material(unreal.load_asset('/Game/GuLiStrike/FX/GroundWarning/M_GroundWarning_Circle'))
material = component.create_dynamic_material_instance()
material.set_scalar_parameter_value('Age', 0)
material.set_scalar_parameter_value('WavePeriod', .8)
material.set_scalar_parameter_value('RingWidth', .025)
material.set_scalar_parameter_value('Opacity', .85)
material.set_vector_parameter_value('Tint', unreal.LinearColor(1, .025, .015, 1))
component.set_visibility(False)
capture = spawn(unreal.SceneCapture2D, origin)
cap = capture.capture_component2d
pos = origin + unreal.Vector(0, -2600, 2600)
rot = unreal.MathLibrary.find_look_at_rotation(pos, origin)
capture.set_actor_location_and_rotation(pos, rot, False, True)
editor.set_level_viewport_camera_info(pos, rot)
for name, value in [('capture_every_frame', False), ('capture_on_movement', False),
                    ('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR), ('fov_angle', 45.)]:
    cap.set_editor_property(name, value)
pp = cap.get_editor_property('post_process_settings')
for name, value in [('override_auto_exposure_method', True), ('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL),
                    ('override_auto_exposure_bias', True), ('auto_exposure_bias', 0.),
                    ('override_auto_exposure_apply_physical_camera_exposure', True), ('auto_exposure_apply_physical_camera_exposure', False),
                    ('override_motion_blur_amount', True), ('motion_blur_amount', 0.),
                    ('override_bloom_intensity', True), ('bloom_intensity', .15)]:
    pp.set_editor_property(name, value)
cap.set_editor_property('post_process_settings', pp)
cap.set_editor_property('post_process_blend_weight', 1.)
rt = unreal.RenderingLibrary.create_render_target2d(world, 1280, 800, unreal.TextureRenderTargetFormat.RTF_RGBA8,
                                                    unreal.LinearColor(0, 0, 0, 1), False, False)
cap.set_editor_property('texture_target', rt)

def finish(error=None):
    unreal.unregister_slate_post_tick_callback(preview['handle'])
    for actor in reversed(preview['actors']):
        actors.destroy_actor(actor)
    editor.set_level_viewport_camera_info(*old_camera)
    report = {k: v for k, v in preview.items() if k not in ['actors', 'handle']}
    report['success'] = error is None
    report['error'] = error
    (OUT/'capture.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

def tick(delta):
    try:
        frame = preview['frame']
        if 0 <= frame-1 < 84:
            filename = 'frame_%03d.png' % (frame-1)
            unreal.RenderingLibrary.export_render_target(world, rt, str(OUT), filename)
            preview['frames'].append({'file': filename, 'time': (frame-1)/30})
        if frame >= 84:
            finish()
            return
        component.set_visibility(6 <= frame < 78)
        material.set_scalar_parameter_value('Age', max(0., (frame-6)/30))
        cap.capture_scene()
        preview['frame'] += 1
    except Exception:
        finish(traceback.format_exc())

preview['handle'] = unreal.register_slate_post_tick_callback(tick)
unreal.MCPythonHelper.submit_result(json.dumps({'started': True, 'output': str(OUT), 'frames': 84}))
