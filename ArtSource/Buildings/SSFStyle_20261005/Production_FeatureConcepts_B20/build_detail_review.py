"""Render B20 at the A5 model camera and build all feature review regions."""

import hashlib
import json
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Euler, Vector


ROOT = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT))
from bake_hardware_details import B8, B13, BASELINE, DEST, REFERENCES, REST, read_json, reference_projection
from bake_surface_marks import B16
from pack_isolated_atlases import B17
from render_review_views import REFERENCE_SIZE, render, world_vertices
from bake_surface_detail import register_zone


SEPARATION = {'AirBase': 886, 'CloningCenter': 754, 'CommandCenter': 754,
              'MilitaryFactory': 886, 'Reactor': 864, 'StrategyCenter': 779}


def bounds(points, size, padding):
    low = np.maximum(np.floor(points.min(axis=0) - padding).astype(int), 0)
    high = np.minimum(np.ceil(points.max(axis=0) + padding).astype(int), size)
    assert np.all(high > low), (low, high)
    return np.r_[low, high].tolist()


def shifted_rect(rect, x):
    x0, y0, x1, y1 = rect
    return [x0 + x, y0, x1 + x, y1]


def edge_segments(model):
    segments = {}
    for id_, item in model['source_edges'].items():
        a, b = (Vector([component / 10000 for component in point]) for point in item['corners_1e4m'])
        start, stop = item['extent']
        segments[int(id_)] = reference_projection([a.lerp(b, start), a.lerp(b, stop)], model['reference_camera'])[:, :2]
    return segments


def close_segments(edges, extent):
    """Keep the locator focused on edge portions near this A5 feature."""
    low = np.asarray(extent[:2]) - 15
    high = np.asarray(extent[2:]) + 15
    return [edge for edge in edges if np.all(edge.max(axis=0) >= low) and np.all(edge.min(axis=0) <= high)]


def features(key, model, source_zones, source_ref, mark_rows, focus):
    zones = []
    first_size = (source_ref['size_px'][0] - SEPARATION[key], source_ref['size_px'][1])
    segments = edge_segments(model)
    for zone, edge_zone in zip(source_zones, model['zones'], strict=True):
        assert zone['feature'] == edge_zone['feature']
        marks = {mark['stroke']: (group, mark) for group in mark_rows if group['zone'] == zone['feature']
                 for mark in group['marks']}
        mapping, _, _ = register_zone(zone)
        points = [np.asarray(stroke['xy']) for stroke in zone['strokes']]
        flat = np.concatenate(points)
        source_extent = bounds(flat, first_size, 17)
        model_guide = flat @ mapping[:2] + mapping[2]
        base_extent = bounds(model_guide, REFERENCE_SIZE, 12)
        target_points = [model_guide]
        guides = []
        differences = []
        attached = 0
        for stroke, correspondence, source_points in zip(zone['strokes'], edge_zone['paths'], points, strict=True):
            index = correspondence['stroke']
            model_edges = close_segments([segments[id_] for id_ in correspondence['edge_ids']], base_extent)
            if model_edges:
                target_points.extend(model_edges)
            texture_points = []
            if index in marks:
                placement, mark = marks[index]
                texture_points = [((np.asarray(probe['screen_sample']) + .5) / 2).tolist() for probe in mark['atlas_probes']]
                target_points.append(np.asarray(texture_points))
                attached += 1
            elif correspondence['mapping'] == 'no_confirmed_edge':
                differences.append(correspondence['note'])
            guides.append({'source': source_points.tolist(), 'edges': np.round(model_edges, 2).tolist(),
                           'texture': np.round(texture_points, 2).tolist()})
        model_extent = bounds(np.concatenate(target_points), REFERENCE_SIZE, 11)
        if key == 'Reactor' and zone['feature'] == '径向散热口框与实际三片前缘':
            differences.append('A5 正面散热组画了四片。B20 将青色前缘贴在 LOD0 的三片实体散热片上。')
        zones.append({'name': zone['feature'], 'source_crop': source_extent, 'model_crop': model_extent,
                      'source_red_crop': shifted_rect(source_extent, SEPARATION[key]),
                      'guides': guides, 'surface_marks': attached, 'differences': differences})
    name, source_crop, model_crop = focus[key]
    assert source_crop[2] <= first_size[0]
    zones.insert(0, {'name': name, 'source_crop': source_crop, 'model_crop': model_crop,
                     'source_red_crop': shifted_rect(source_crop, SEPARATION[key]),
                     'guides': [], 'surface_marks': 0, 'differences': []})
    return zones


def build():
    assert Path(bpy.data.filepath) == DEST
    project_hash = hashlib.sha256(DEST.read_bytes()).hexdigest()
    output = ROOT / 'DetailReview'
    output.mkdir(exist_ok=True)
    source = read_json(BASELINE / 'B6_Material_Manifest.json')['models']
    edge_models = read_json(B13 / 'B13_Edge_Ink_Manifest.json')['models']
    references = {row['key']: row for row in read_json(REFERENCES / 'Reference_A5_Manifest.json')['deliverables']}
    surface_marks = read_json(B17 / 'B17_Surface_Marks_Manifest.json')['models']
    registered = read_json(B16 / 'B16_Registered_Detail_Manifest.json')['models']
    source_zones = read_json(B8 / 'reference_strokes.json')
    focus = read_json(ROOT / 'ReviewFocus.json')
    models = []
    for key, row in source.items():
        scene = bpy.data.scenes[row['scene']]
        bpy.context.window.scene = scene
        bpy.context.view_layer.update()
        model = edge_models[key]
        ref = references[key]
        ref_path = REFERENCES / ref['file']
        assert hashlib.sha256(ref_path.read_bytes()).hexdigest() == ref['sha256']
        assert [zone['feature'] for zone in registered[key]['surface_details']] == [zone['feature'] for zone in source_zones[key]]
        assert all(zone['samples'] > 0 for zone in registered[key]['surface_details'])
        blue, red = (bpy.data.objects[name] for name in row['objects'])
        assert blue.data == red.data and len(blue.data.polygons) == row['triangles']
        depsgraph = bpy.context.evaluated_depsgraph_get()
        rest = np.asarray([attr.vector.copy() for attr in blue.data.attributes[REST].data], dtype=np.float64)
        offsets = [world_vertices(obj, depsgraph) - rest for obj in (blue, red)]
        shifts = [offset.mean(axis=0) for offset in offsets]
        location, euler, width = model['reference_camera']
        error = max(float(np.linalg.norm(offset - shift, axis=1).max()) * REFERENCE_SIZE[0] / width
                    for offset, shift in zip(offsets, shifts, strict=True))
        assert error < .12, (key, error)
        lights = {obj: obj.location.copy() for obj in scene.objects if obj.type == 'LIGHT'}
        scene.render.resolution_percentage = 100
        scene.render.image_settings.color_mode = 'RGBA'
        scene.render.film_transparent = True
        images = {}
        for team, obj, shift in zip(('Blue', 'Red'), (blue, red), shifts, strict=True):
            for other in scene.objects:
                if other.type == 'MESH':
                    other.hide_render = other != obj
            for light, position in lights.items():
                light.location = position + Vector(shift - shifts[0])
            path = output / f'{key}_{team}_B20.png'
            render(scene, path, tuple(2 * value for value in REFERENCE_SIZE), Euler(euler), Vector(location) + Vector(shift), width)
            images[team] = {'file': path.name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
        selected = features(key, model, source_zones[key], ref, surface_marks.get(key, {}).get('marks', []), focus)
        models.append({'key': key, 'name': ref['name'], 'id': row['model_id'], 'triangles': row['triangles'],
                       'source': ref['file'], 'source_sha256': ref['sha256'], 'source_size': ref['size_px'],
                       'model_images': images, 'camera': model['reference_camera'], 'camera_error_px': round(error, 6),
                       'red_source_offset': SEPARATION[key], 'features': selected})
        print(key, len(selected) - 1, 'registered regions', round(error, 5), flush=True)
    assert len(models) == 6 and sum(len(row['features']) - 1 for row in models) == 37
    assert sum(len(zone['guides']) for row in models for zone in row['features']) == 170
    assert sum(zone['surface_marks'] for row in models for zone in row['features']) == 11
    assert hashlib.sha256(DEST.read_bytes()).hexdigest() == project_hash
    manifest = {'project': DEST.name, 'project_sha256': project_hash, 'reference': 'A5', 'model_size': list(REFERENCE_SIZE),
                'registered_zones': 37, 'source_guides': 170, 'surface_marks': 11, 'models': models}
    (output / 'B20_Detail_Review_Manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n',
                                                         encoding='utf-8')
    (output / 'review-data.js').write_text('const REVIEW_DATA = ' + json.dumps(manifest, ensure_ascii=False) + ';\n',
                                         encoding='utf-8')


if __name__ == '__main__':
    build()
