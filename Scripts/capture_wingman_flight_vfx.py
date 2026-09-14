"""Capture deterministic movement, ignition and fade with transient preview actors."""
import json
import math
import traceback
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/TestResults/WingmanFlightVFX')
SYSTEM = '/Game/GuLiStrike/FX/WingmanFlight/NS_WingmanFlightTrail'
MESH = '/Game/GuLiStrike/Wingman/SM_Wingman_Mass'
report = {'system': SYSTEM, 'frames': []}
actors = []

try:
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_editor_world()
    origin = unreal.Vector(0, 0, 50000)
    aircraft = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.StaticMeshActor, origin, transient=True)
    actors.append(aircraft)
    mesh = aircraft.static_mesh_component
    mesh.set_mobility(unreal.ComponentMobility.MOVABLE)
    mesh.set_static_mesh(unreal.load_asset(MESH))
    effect = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.NiagaraActor, origin, transient=True)
    actors.append(effect)
    component = effect.niagara_component
    component.set_auto_activate(False)
    component.set_asset(unreal.load_asset(SYSTEM), reset_existing_override_parameters=True)
    component.set_force_solo(True)
    component.set_variable_float('User.Throttle', 1.0)
    component.set_variable_float('User.Opacity', 1.0)
    camera = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.SceneCapture2D, origin, transient=True)
    actors.append(camera)
    capture = camera.capture_component2d
    capture.set_editor_property('capture_every_frame', False)
    capture.set_editor_property('capture_on_movement', False)
    capture.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    capture.set_editor_property('fov_angle', 48.0)
    target = unreal.RenderingLibrary.create_render_target2d(
        world, 1400, 900, unreal.TextureRenderTargetFormat.RTF_RGBA8, unreal.LinearColor(0.012,0.02,0.03,1), False, False)
    capture.set_editor_property('texture_target', target)
    capture.set_editor_property('primitive_render_mode', unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
    capture.show_only_actor_components(aircraft)
    capture.show_only_actor_components(effect)
    component.activate(True)
    samples = {4:'start', 45:'flight', 72:'turn', 84:'fade', 114:'finished'}
    for frame in range(115):
        t = frame / 60.0
        theta = t * 0.8
        forward = unreal.Vector(math.cos(theta), math.sin(theta), 0)
        center = origin + unreal.Vector(math.sin(theta)*11250, (1-math.cos(theta))*11250, 0)
        aircraft.set_actor_location_and_rotation(center, unreal.Rotator(yaw=math.degrees(theta)+180), False, True)
        nozzle = center - forward * 1258 + unreal.Vector(0,0,300)
        effect.set_actor_location_and_rotation(nozzle, unreal.Rotator(yaw=math.degrees(theta)), False, True)
        component.set_variable_vec3('User.Forward', forward)
        component.set_variable_vec3('User.Right', unreal.Vector(-forward.y, forward.x, 0))
        if frame == 73:
            component.deactivate()
        component.advance_simulation(1, 1.0/60.0)
        if frame not in samples:
            continue
        look = center - forward*1600 + unreal.Vector(0,0,120)
        side = unreal.Vector(-forward.y, forward.x, 0)
        camera_location = look + side*6800 - forward*1100 + unreal.Vector(0,0,4200)
        camera.set_actor_location_and_rotation(camera_location, unreal.MathLibrary.find_look_at_rotation(camera_location, look), False, True)
        capture.capture_scene()
        filename = samples[frame]+'.png'
        unreal.RenderingLibrary.export_render_target(world, target, str(OUT), filename)
        report['frames'].append({'name':samples[frame], 'time':t, 'path':str(OUT/filename),
                                 'active':component.is_active()})
    report['success'] = True
except Exception:
    report['success'] = False
    report['error'] = traceback.format_exc()
finally:
    for actor in reversed(actors):
        if actor:
            unreal.EditorLevelLibrary.destroy_actor(actor)
    report['transient_actors_removed'] = True
OUT.mkdir(parents=True, exist_ok=True)
(OUT/'temporal-preview.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report))
