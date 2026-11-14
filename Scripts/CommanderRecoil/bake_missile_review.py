"""Author the additional WM01 missile action from the current recoil source.

Blender authoring only: preserves meshes/materials and exports CPU/GPU samples.
The missile action has no local gun translation and no alternating yaw.
"""
import ast
import bpy
import hashlib
import json
import math
from pathlib import Path

import numpy as np
from mathutils import Matrix, Quaternion, Vector
from bpy_extras.object_utils import world_to_camera_view

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/CommanderRecoil_20261114'
FOLDER = OUT / 'WarMachine/Missile_v1'
FOLDER.mkdir(parents=True, exist_ok=True)
(FOLDER / 'Textures').mkdir(exist_ok=True)
SOURCE = OUT / 'WarMachine/WarMachine_Recoil_v1.blend'
META = json.loads((OUT / 'WarMachine/recoil.json').read_text(encoding='utf8'))
FORMAL = json.loads((OUT / 'Sources/formal-sources.json').read_text(encoding='utf8'))['WarMachine']
FPS = 60
ENTER = .1
HOLD = 1.2
RETURN = .5
DURATION = ENTER + HOLD + RETURN
PREVIEW_SECONDS = DURATION + .6
REFLECT = Matrix.Diagonal(Vector((1, -1, 1)))

# Use the same already-checked float texture writer as the basic recoil source.
tree = ast.parse((ROOT / 'Scripts/CommanderRecoil/bake_review.py').read_text(encoding='utf8'))
writer = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'save_texture')
exec(compile(ast.Module(body=[writer], type_ignores=[]), str(__file__), 'exec'))


def smooth(value):
    x = max(0., min(1., value))
    return x*x*(3.-2.*x)


def weight(t):
    if t <= 0 or t >= DURATION:
        return 0.
    if t < ENTER:
        return smooth(t/ENTER)
    if t <= ENTER+HOLD:
        return 1.
    return 1.-smooth((t-ENTER-HOLD)/RETURN)


def sample(t):
    w = weight(t)
    # Exactly the basic-fire peak pitch/backward travel, without the side twist.
    q = Quaternion((0, 1, 0), -math.radians(8.)*w)
    return {
        'upper_translation': [-META['peak_upper_translation_cm']*w, 0., 0.],
        'upper_rotation': [q.x, q.y, q.z, q.w],
        'left_gun_translation': [0., 0., 0.],
        'right_gun_translation': [0., 0., 0.],
    }


def mesh_digest(mesh):
    result = hashlib.sha256()
    for collection, field, size, dtype in (
        (mesh.vertices, 'co', 3, np.float32),
        (mesh.loops, 'vertex_index', 1, np.int32),
        (mesh.polygons, 'material_index', 1, np.int32),
    ):
        values = np.empty(len(collection)*size, dtype=dtype)
        collection.foreach_get(field, values)
        result.update(values.tobytes())
    for color in mesh.color_attributes:
        values = np.empty(len(color.data)*4, dtype=np.float32)
        color.data.foreach_get('color', values)
        result.update(values.tobytes())
    for uv in mesh.uv_layers:
        values = np.empty(len(uv.data)*2, dtype=np.float32)
        uv.data.foreach_get('uv', values)
        result.update(values.tobytes())
    result.update(json.dumps([m.name if m else None for m in mesh.materials]).encode())
    return result.hexdigest()


bpy.ops.wm.open_mainfile(filepath=str(SOURCE), load_ui=False)
scene = bpy.context.scene
scene.name = '05_重防号_导弹持续后倾'
scene.render.fps = FPS
scene.render.fps_base = 1.
scene.frame_start = 1
scene.frame_end = round(PREVIEW_SECONDS*FPS)
scene.timeline_markers.clear()
for title, seconds in (
    ('齐射开始', 0), ('保持后倾 · 枪管不回缩', ENTER),
    ('结束保持 · 平稳回正', ENTER+HOLD), ('回到中立', DURATION),
):
    scene.timeline_markers.new(title, frame=1+round(seconds*FPS))
scene['动作说明'] = '导弹齐射：0.10秒后倾至8度，保持1.20秒，0.50秒回正；枪管无独立回缩，无左右摇摆。'
scene['持续姿态合同'] = '连发只延长保持段，不能按每枚导弹重新播放进入/回位；该源尚未正式接入UE。'
scene['继承来源'] = str(SOURCE)
scene['美术审核'] = '用户动作B待确认；没有助手读图验收'
parts = []
original_hashes = {}
for obj in scene.objects:
    if obj.type != 'MESH':
        continue
    original_hashes[obj.name] = mesh_digest(obj.data)
    part = int(obj.name.split('_Part')[1].split('_')[0])
    obj.animation_data_clear()
    obj.rotation_mode = 'QUATERNION'
    parts.append((obj, part))

pivot = Vector(META['upper_pivot'])
samples = [sample(i/FPS) for i in range(round(DURATION*FPS)+1)]
low = Vector((1.e20,)*3)
high = -low
max_rigid_error = 0.
for frame in range(1, scene.frame_end+2):
    s = sample((frame-1)/FPS)
    q = Quaternion((s['upper_rotation'][3], *s['upper_rotation'][:3]))
    r = q.to_matrix()
    translation = pivot-r@pivot+Vector(s['upper_translation'])
    for obj, part in parts:
        upper = part in META['upper_parts']
        obj.location = (REFLECT@translation)*.01 if upper else Vector((0, 0, 0))
        obj.rotation_quaternion = (REFLECT@r@REFLECT).to_quaternion() if upper else Quaternion()
        obj.scale = (.01, .01, .01)
        if upper:
            obj.keyframe_insert('location', frame=frame)
            obj.keyframe_insert('rotation_quaternion', frame=frame)
        # Full animated bounds in UE mesh centimeters, including all three LODs.
        for corner in obj.bound_box:
            p = REFLECT@Vector(corner)
            p = r@p+translation if upper else p
            low = Vector([min(low[i], p[i]) for i in range(3)])
            high = Vector([max(high[i], p[i]) for i in range(3)])
    delta = r.transposed()@r-Matrix.Identity(3)
    max_rigid_error = max(max_rigid_error, max(abs(x) for row in delta for x in row))

# Sampled keys use linear interpolation, so authored holds cannot acquire handles
# from the previous per-shot action or introduce Bezier overshoot.
for obj, part in parts:
    action = obj.animation_data.action if obj.animation_data else None
    if not action:
        continue
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                for curve in bag.fcurves:
                    for key in curve.keyframe_points:
                        key.interpolation = 'LINEAR'

assert original_hashes == {obj.name: mesh_digest(obj.data) for obj, _ in parts}
assert max_rigid_error < 1.e-5
assert samples[0] == samples[-1]
assert all(s['left_gun_translation'] == s['right_gun_translation'] == [0., 0., 0.] for s in samples)
held = samples[round(ENTER*FPS):round((ENTER+HOLD)*FPS)+1]
assert all(s == held[0] for s in held)
assert all(abs(s['upper_rotation'][0])+abs(s['upper_rotation'][2]) == 0. for s in samples)

textures = {
    'position': save_texture(FOLDER / 'Textures/T_MissilePosition.exr',
        [[[ *s[k], 1.] for k in ('upper_translation', 'left_gun_translation', 'right_gun_translation')] for s in samples]),
    'rotation': save_texture(FOLDER / 'Textures/T_MissileRotation.exr', [[s['upper_rotation']] for s in samples]),
}
reference_muzzles = [FORMAL['sockets'][f'FX_Missile_{side+1:02}']['position'] for side in range(2)]
muzzle_samples = []
for s in samples:
    q = Quaternion((s['upper_rotation'][3], *s['upper_rotation'][:3]))
    muzzle_samples.append([list(pivot+q@(Vector(p)-pivot)+Vector(s['upper_translation'])) for p in reference_muzzles])

# Frame the evaluated, visible LOD0 only, including both neutral and held pose.
points = []
for frame in (1, 1+round(ENTER*FPS)):
    scene.frame_set(frame)
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    for obj, _ in parts:
        if obj.visible_get(view_layer=bpy.context.view_layer):
            evaluated = obj.evaluated_get(dg)
            points.extend(evaluated.matrix_world@Vector(p) for p in evaluated.bound_box)
bottom = Vector([min(p[i] for p in points) for i in range(3)])
top = Vector([max(p[i] for p in points) for i in range(3)])
center = (bottom+top)*.5
span = max(top-bottom)
camera = bpy.data.cameras.new('MissileActionReviewCamera')
camera.type = 'ORTHO'
camera.clip_start = .01
camera.clip_end = max(1000., span*10.)
cam = bpy.data.objects.new('MissileActionReviewCamera', camera)
scene.collection.objects.link(cam)
cam.location = center+Vector((1.5, -2.2, 1.2))*span
cam.rotation_euler = (center-cam.location).to_track_quat('-Z', 'Y').to_euler()
scene.camera = cam
scene.render.resolution_x, scene.render.resolution_y = 1600, 1000
bpy.context.view_layer.update()
local = [cam.matrix_world.inverted()@p for p in points]
camera.ortho_scale = max(max(abs(p.x) for p in local)*2., max(abs(p.y) for p in local)*3.2)*1.18
projected = [world_to_camera_view(scene, cam, p) for p in points]
assert all(.03<p.x<.97 and .03<p.y<.97 and camera.clip_start<p.z<camera.clip_end for p in projected)
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type == 'VIEW_3D':
            view = area.spaces.active
            view.use_local_camera = False
            view.camera = cam
            view.clip_end = 10000.
            view.shading.type = 'MATERIAL'
            view.region_3d.view_camera_zoom = 0
            view.region_3d.view_perspective = 'CAMERA'
scene.frame_set(1)
bpy.context.preferences.filepaths.save_version = 0
destination = FOLDER / 'WarMachine_Missile_v1.blend'
bpy.ops.wm.save_as_mainfile(filepath=str(destination), check_existing=False, compress=True)
report = {
    'unit': 'WarMachine', 'unit_type_id': 2, 'weapon_slot': 'MissileLauncher',
    'source_blend': str(SOURCE), 'source_blend_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
    'source_mesh': META['source_mesh'], 'source_sha256': META['source_sha256'], 'blend': str(destination),
    'sample_fps': FPS, 'duration_seconds': DURATION, 'entry_seconds': ENTER,
    'hold_seconds': HOLD, 'return_seconds': RETURN, 'preview_seconds': PREVIEW_SECONDS,
    'hold_start_frame_zero_based': round(ENTER*FPS), 'hold_end_frame_zero_based': round((ENTER+HOLD)*FPS),
    'sustained_playback': 'Enter once; hold while firing; extend hold rather than restart for each missile; release once.',
    'peak_pitch_degrees': 8., 'peak_upper_translation_cm': META['peak_upper_translation_cm'],
    'upper_pivot': META['upper_pivot'], 'upper_parts': META['upper_parts'],
    'gun_translation_cm': [0., 0.], 'yaw_degrees': 0., 'samples': samples,
    'reference_muzzles': reference_muzzles, 'muzzle_samples': muzzle_samples,
    'render_bounds': {'min': list(low-Vector((10,)*3)), 'max': list(high+Vector((10,)*3))},
    'lods': META['lods'], 'texture_readback': textures, 'mesh_and_materials_unchanged': True,
    'maximum_rigid_orthogonality_error': max_rigid_error,
    'visual_review': 'Pending user B; no assistant image inspection', 'ue_imported': False,
}
(FOLDER / 'missile-recoil.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
combined_folder = OUT / 'WarMachine/Combined_v2'
(combined_folder / 'Textures').mkdir(parents=True, exist_ok=True)
combined = dict(META)
combined['samples'] = META['samples'] + samples
combined['missile_frames'] = len(samples)
combined['missile_enter_seconds'] = ENTER
combined['missile_hold_seconds'] = HOLD
combined['missile_return_seconds'] = RETURN
combined['missile_peak_pitch_degrees'] = 8.
combined['missile_source'] = str(FOLDER / 'missile-recoil.json')
combined['render_bounds'] = {
    'min': [min(META['render_bounds']['min'][i], report['render_bounds']['min'][i]) for i in range(3)],
    'max': [max(META['render_bounds']['max'][i], report['render_bounds']['max'][i]) for i in range(3)],
}
combined['texture_readback'] = {
    'position': save_texture(combined_folder / 'Textures/T_RecoilPosition.exr',
        [[[ *s[k], 1.] for k in ('upper_translation', 'left_gun_translation', 'right_gun_translation')] for s in combined['samples']]),
    'rotation': save_texture(combined_folder / 'Textures/T_RecoilRotation.exr', [[s['upper_rotation']] for s in combined['samples']]),
}
(combined_folder / 'recoil.json').write_text(json.dumps(combined, ensure_ascii=False, indent=2), encoding='utf8')
print('MISSILE_ACTION_SAVED', json.dumps({k: report[k] for k in ('blend', 'entry_seconds', 'hold_seconds', 'return_seconds', 'mesh_and_materials_unchanged', 'texture_readback')}, ensure_ascii=True), flush=True)
