"""Actual render evidence and saved-file readback for the static third batch."""
import importlib.util
import json
import sys
from pathlib import Path
import bpy
import numpy as np

PROJECT = Path('D:/UE5.7/test1')
def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

batch = module('batch03_materials', PROJECT / 'Scripts/Blender/style_ship_component_batch03_materials.py')
review = module('existing_material_review', PROJECT / 'Scripts/Blender/finish_ship_component_material_review.py')
s = batch.s
review.s = s


def exterior_marking_views():
    key = 'Drone_LaunchBay'
    target = s.OUT / f'{key}_OriginalMesh_MaterialCandidate.blend'
    bpy.ops.wm.open_mainfile(filepath=str(target))
    body = next(o for o in bpy.context.scene.objects if o.get('source_file'))
    points = [body.matrix_world @ v.co for v in body.data.vertices]
    for name, direction in [('exterior_hero', (-1.1, -1.7, 1.65)), ('left', (-1, 0, 0))]:
        s.aim(bpy.context.scene.camera, points, direction)
        review.save_still(s.PRE / f'{key}_{name}.png')
    s.aim(bpy.context.scene.camera, points, (1.1, -1.7, 1.65))
    bpy.ops.wm.save_as_mainfile(filepath=str(target))
    path = s.OUT / f'{key}_material_report.json'
    report = json.loads(path.read_text(encoding='utf-8'))
    report['blend_sha256'] = s.filehash(target)
    report['launch_marker_location'] = 'left thin-wall exterior; source seed 0, x=-7.568m, normal -X'
    report['launch_direction_blender'] = [0, 1, 0]
    report['marking_preview_views'] = ['exterior_hero', 'left']
    s.dump(path, report)


def prepare_and_overview():
    rows = {}
    for number, key in enumerate(batch.KEYS, 1):
        original, _, _, _ = batch.load_original(key)
        source_hash = s.ink_bake.geometry_hash(original.data)
        source_matrix = np.array(original.matrix_world)
        source_normals = np.array([n.vector[:] for n in original.data.corner_normals])
        source_uv = {u.name: np.array([v.uv[:] for v in u.data]) for u in original.data.uv_layers}
        target = s.OUT / f'{key}_OriginalMesh_MaterialCandidate.blend'
        bpy.ops.wm.open_mainfile(filepath=str(target))
        scene = bpy.context.scene
        scene.frame_set(0)
        scene.name = f'Batch03_{number:02}_{key}'
        scene['approval_A'] = 'approved; approval_A_20260917.json; user: 开始实施'
        scene['reference_version'] = batch.APPROVAL['parts'][key]['reference_version']
        scene['candidate_version'] = batch.VERSION
        body = next(o for o in scene.objects if o.type == 'MESH' and o.get('source_file'))
        report_path = s.OUT / f'{key}_material_report.json'
        report = json.loads(report_path.read_text(encoding='utf-8'))
        assert source_hash == s.ink_bake.geometry_hash(body.data) == report['original_geometry_sha256']
        assert np.array_equal(source_matrix, np.array(body.matrix_world))
        assert np.array_equal(source_normals, np.array([n.vector[:] for n in body.data.corner_normals]))
        for name, data in source_uv.items():
            assert np.array_equal(data, np.array([v.uv[:] for v in body.data.uv_layers[name].data]))
        assert len(body.modifiers) == 0 and not any(o.type == 'ARMATURE' for o in scene.objects)
        assert len(body.data.materials[0].node_tree.nodes['Three_Tone_Lighting'].color_ramp.elements) == 3
        packed = [im.name for im in bpy.data.images if im.packed_file and list(im.size) == [2048, 2048]]
        assert len(packed) == 3
        for area in bpy.context.screen.areas if bpy.context.screen else []:
            if area.type == 'VIEW_3D':
                area.spaces.active.region_3d.view_perspective = 'CAMERA'
                area.spaces.active.shading.type = 'RENDERED'
                area.spaces.active.shading.use_scene_world = True
                area.spaces.active.shading.use_scene_lights = True
                area.spaces.active.overlay.show_overlays = False
        bpy.context.preferences.filepaths.save_version = 0
        bpy.ops.wm.save_as_mainfile(filepath=str(target), compress=True)
        report.update({'blend_sha256': s.filehash(target), 'saved_file_geometry_readback': 'passed',
            'original_object_matrix_unchanged': True, 'original_UV_channels_unchanged': True,
            'original_corner_normals_unchanged': True, 'mechanical_type': 'static', 'motion': {'status': 'not_applicable_static'}})
        s.dump(report_path, report)
        rows[key] = {'geometry_matches_original': True, 'object_matrix_matches_original': True,
            'original_normals_match': True, 'original_UV_match': True, 'neutral_frame': 0,
            'three_tone_steps': 3, 'packed_2K_textures': len(packed), 'blend_sha256': report['blend_sha256'],
            'reference_version': report['reference_version'], 'body_triangles': report['body_triangles'],
            'bones': [], 'sockets': report['sockets'], 'motion': 'not_applicable_static', 'B': 'pending'}
    s.dump(s.OUT / 'saved_blender_readback.json', rows)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    empty = bpy.context.scene
    scenes = []
    for key in batch.KEYS:
        with bpy.data.libraries.load(str(s.OUT / f'{key}_OriginalMesh_MaterialCandidate.blend'), link=False) as (src, dst):
            dst.scenes = src.scenes
        scenes.extend(dst.scenes)
    bpy.context.window.scene = scenes[0]
    bpy.data.scenes.remove(empty)
    for scene in scenes:
        scene['overview_note'] = 'Select the component using Scene selector. Original dimensions/origin remain; render staging is separate.'
    bpy.ops.wm.save_as_mainfile(filepath=str(s.OUT / 'ShipComponentStyle_Batch03_Overview.blend'), compress=True)
    print('BATCH03_SAVED_SOURCE_READBACK_COMPLETE', batch.VERSION, flush=True)


if __name__ == '__main__':
    args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    keys = [key for key in args if key in batch.KEYS] or list(batch.KEYS)
    if '--prepare-only' not in args:
        for key in keys:
            review.run(key)
        if 'Drone_LaunchBay' in keys:
            exterior_marking_views()
    prepare_and_overview()
    print('BATCH03_REVIEW_MEDIA_COMPLETE', batch.VERSION, flush=True)
