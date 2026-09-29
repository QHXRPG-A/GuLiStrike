"""Capture current 重防号 mesh references; temporary actors only, no asset/map save."""
import json
import time
import traceback
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/ArtSource/UI/WM01MissileCards/Review_v1/References')
OUT.mkdir(parents=True, exist_ok=True)
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
ASSET = '/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Rigid'
mesh = unreal.load_asset(ASSET)
assert mesh
WORLD = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ORIGIN = unreal.Vector(8000, 8000, 0)
SPAWNED = []

def spawn(cls, name):
    actor = ACTORS.spawn_actor_from_class(cls, ORIGIN)
    actor.set_actor_label('Temp_WM01MissileCardReference_' + name)
    SPAWNED.append(actor)
    return actor

BODY = spawn(unreal.StaticMeshActor, 'Body')
BODY.static_mesh_component.set_static_mesh(mesh)
BODY.static_mesh_component.set_forced_lod_model(1)
BODY.set_actor_scale3d(unreal.Vector(.02, .02, .02))
BODY.static_mesh_component.set_cast_shadow(False)
BG_MAT = unreal.new_object(unreal.Material)
BG_MAT.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
BG_MAT.set_editor_property('two_sided', True)
color = unreal.MaterialEditingLibrary.create_material_expression(BG_MAT, unreal.MaterialExpressionConstant3Vector)
color.set_editor_property('constant', unreal.LinearColor(.12, .16, .20, 1))
unreal.MaterialEditingLibrary.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
unreal.MaterialEditingLibrary.recompile_material(BG_MAT)
BG = spawn(unreal.StaticMeshActor, 'Backdrop')
BG.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Plane'))
BG.static_mesh_component.set_material(0, BG_MAT)
BG.set_actor_scale3d(unreal.Vector(12, 12, 12))
BG.static_mesh_component.set_cast_shadow(False)
CAP_ACTOR = spawn(unreal.SceneCapture2D, 'Capture')
CAP = CAP_ACTOR.get_component_by_class(unreal.SceneCaptureComponent2D)
CAP.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
CAP.set_editor_property('capture_every_frame', False)
CAP.set_editor_property('always_persist_rendering_state', True)
CAP.set_editor_property('primitive_render_mode', unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
CAP.show_only_actor_components(BODY, False)
CAP.show_only_actor_components(BG, False)
pp = unreal.PostProcessSettings()
for key, value in {'override_auto_exposure_method': True, 'auto_exposure_method': unreal.AutoExposureMethod.AEM_MANUAL,
    'override_auto_exposure_apply_physical_camera_exposure': True, 'auto_exposure_apply_physical_camera_exposure': False,
    'override_auto_exposure_bias': True, 'auto_exposure_bias': 0., 'override_motion_blur_amount': True,
    'motion_blur_amount': 0., 'override_bloom_intensity': True, 'bloom_intensity': 0.}.items():
    pp.set_editor_property(key, value)
CAP.set_editor_property('post_process_settings', pp)
TARGET = unreal.RenderingLibrary.create_render_target2d(WORLD, 1536, 1536,
    unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB, unreal.LinearColor(.12, .16, .20, 1), False)
TARGET.set_editor_property('target_gamma', 1.)
CAP.set_editor_property('texture_target', TARGET)
VIEWS = [('FullModel', (155, 180, 125), (0, 0, 31), 43),
         ('MissilePods', (100, 60, 35), (-18, 0, 60), 35)]
REPORT = {'success': False, 'source_asset': ASSET, 'source_model_version': 'MechanicalAnimation_20260929/WarMachine',
    'source_materials': [s.material_interface.get_path_name() for s in mesh.static_materials],
    'scale': .02, 'world': WORLD.get_path_name(), 'images': [], 'formal_assets_modified': False}
STATE = {'i': 0, 'phase': 0, 'wait': 8., 'start': time.monotonic()}

def cleanup():
    unreal.unregister_slate_post_tick_callback(HANDLE)
    for actor in reversed(SPAWNED):
        ACTORS.destroy_actor(actor)
    REPORT['temporary_actors_removed'] = True
    (OUT / 'capture-manifest.json').write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding='utf-8')

def tick(delta):
    try:
        if time.monotonic() - STATE['start'] > 90:
            raise RuntimeError('Reference capture timed out')
        STATE['wait'] -= delta
        if STATE['wait'] > 0:
            return
        if STATE['i'] >= len(VIEWS):
            REPORT['success'] = True
            cleanup()
            return
        name, camera, target, fov = VIEWS[STATE['i']]
        pos, aim = ORIGIN + unreal.Vector(*camera), ORIGIN + unreal.Vector(*target)
        if STATE['phase'] == 0:
            CAP_ACTOR.set_actor_location(pos, False, False)
            CAP_ACTOR.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(pos, aim), False)
            CAP.set_editor_property('fov_angle', float(fov))
            direction = (aim - pos).normal()
            BG.set_actor_location(aim + direction * 150, False, False)
            BG.set_actor_rotation(unreal.MathLibrary.make_rot_from_z(direction), False)
            CAP.capture_scene()
            STATE.update(phase=1, wait=.6)
            return
        CAP.capture_scene()
        unreal.RenderingLibrary.export_render_target(WORLD, TARGET, str(OUT), name + '.png')
        REPORT['images'].append({'file': name + '.png', 'camera': camera, 'target': target, 'horizontal_fov': fov})
        STATE.update(i=STATE['i'] + 1, phase=0, wait=.25)
    except Exception:
        REPORT['error'] = traceback.format_exc()
        cleanup()

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
HANDLE = unreal.register_slate_post_tick_callback(tick)
print(json.dumps({'capture_started': True, 'output': str(OUT)}))
