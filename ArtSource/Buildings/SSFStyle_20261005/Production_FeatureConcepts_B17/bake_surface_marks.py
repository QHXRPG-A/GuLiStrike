"""Keep the A5 surface-only markings intact on their individual LOD0 hosts."""

import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

import bpy
import numpy as np


ROOT = Path(__file__).resolve().parent
B16 = ROOT.parent / 'Production_FeatureConcepts_B16'
DEST = ROOT / 'SSF_A5_SurfaceMarks_B17.blend'
sys.dont_write_bytecode = True
sys.path.insert(0, str(B16))
from bake_registered_detail import (B8, B13, B15, BASELINE, REFERENCES, REST, SCALE, STYLE_PROPERTY, UV_NAME,
                                    SurfaceRegistration, extract_surface_details, original_parts, pack_tiles,
                                    projected_ownership, read_json, reference_detail, reference_projection, save_texture)
from bake_surface_detail import paint_segment, register_zone, sample


# The two excluded annotations describe geometry absent from LOD0. The remaining
# eleven are marks on continuous surfaces. These placements name the actual host.
MARKS = {
    'AirBase': (
        (0, (5,), 'polymsh050', 'Ivory painted cladding', '近端冠板检修圆盘'),
        (0, (6,), 'polymsh046', 'Ivory painted cladding', '远端冠板检修圆盘'),
        (1, (5, 6, 8), 'polymsh049', 'Hangar guide rail', '近端导轨横纹'),
        (5, (1,), 'polymsh051', 'Faction primary coating', '机构罩检修圆盘'),
    ),
    'CloningCenter': ((7, (4,), 'MainBlock', '', '门框旁装甲拼缝'),),
    'CommandCenter': ((3, (2,), 'BlockA', 'Faction primary coating', '前部设备罩检修板'),),
    'MilitaryFactory': ((4, (2, 4, 5), 'MainBlock', 'Gray-green powder coat', '侧墙分板纹'),),
}


def source_stencil(stroke):
    """Crop original source pixels around the line and through closed plates."""
    points = np.asarray(stroke['xy'], dtype=float)
    low = np.floor(points.min(axis=0)).astype(int) - 8
    high = np.ceil(points.max(axis=0)).astype(int) + 8
    x0, y0 = low
    x1, y1 = high
    mask = np.zeros((y1 - y0, x1 - x0), dtype=np.float32)
    for start, end in zip(points, points[1:]):
        paint_segment(mask, start - low, end - low, stroke.get('width', 2.) + 6., 1.)
    if np.array_equal(points[0], points[-1]):
        x, y = np.meshgrid(np.arange(x0, x1) + .5, np.arange(y0, y1) + .5)
        inside = np.zeros_like(mask, dtype=bool)
        for (ax, ay), (bx, by) in zip(points, points[1:]):
            if ay != by:
                inside ^= ((ay > y) != (by > y)) & (x < ax + (y - ay) * (bx - ax) / (by - ay))
        mask[inside] = 1.
    return points, low, mask


def rail_mapping():
    """Use the B13 local calibration between the actual guide's two rails."""
    source = [[679, 485, 1], [755, 619, 1], [654, 494, 1], [725, 633, 1]]
    target = [[563.6, 370.2], [600.6, 452.0], [540.3, 378.8], [577.3, 460.6]]
    return np.linalg.lstsq(source, target, rcond=None)[0]


def forward(points, mapping):
    return points @ mapping[:2] + mapping[2]


def inside_host(points, allowed):
    xy = np.floor(points * SCALE).astype(int)
    x, y = xy.T
    valid = (x >= 0) & (x < allowed.shape[1]) & (y >= 0) & (y < allowed.shape[0])
    valid[valid] = allowed[y[valid], x[valid]]
    return valid


def fit_group(stencils, mapping, allowed):
    """Choose the smallest placement change that fits the whole source footprint."""
    source_points = []
    for stencil in stencils:
        yy, xx = np.nonzero(stencil['mask'] >= .5)
        source_points.append(np.column_stack((xx, yy)) + stencil['low'] + .5)
    points = forward(np.concatenate(source_points), mapping)
    initial = float(inside_host(points, allowed).mean())
    if initial == 1.:
        return mapping, initial, 0., 1.
    center = points.mean(axis=0)
    choices = []
    for scale in (1., .98, .96, .94, .92, .90, .88, .85):
        for dx in np.arange(-14., 14.1, .5):
            for dy in np.arange(-14., 14.1, .5):
                distance = float(np.hypot(dx, dy))
                if distance <= 14.:
                    choices.append((distance + 30 * (1 - scale), distance, scale, dx, dy))
    for _, distance, scale, dx, dy in sorted(choices):
        shifted = (points - center) * scale + center + (dx, dy)
        if inside_host(shifted, allowed).all():
            result = mapping * scale
            result[2] += center * (1 - scale) + (dx, dy)
            return result, initial, distance, scale
    raise ValueError('Source marking cannot fit this host within the bounded placement search')


class MarkRegistration(SurfaceRegistration):
    def __init__(self, model, owner, face_parts, groups):
        super().__init__(model, owner, face_parts)
        self.groups = groups

    def __call__(self, zone, edge_zone, mapping, points, face_ids):
        aligned = super().__call__(zone, edge_zone, mapping, points, face_ids)
        for group in self.groups:
            if group['zone'] != zone['feature']:
                continue
            source_xy = (aligned - mapping[2]) @ np.linalg.inv(mapping[:2])
            for stencil in group['stencils']:
                xy = source_xy - stencil['low']
                support = sample(stencil['mask'][..., None], *xy.T)[:, 0] >= .5
                host = group['host_faces'][face_ids]
                stencil['b16_support'] = int(support.sum())
                stencil['b16_support_on_host'] = int(np.count_nonzero(support & host))
        return aligned


def prepare_marks(key, zones, detail, faces, finishes, owner):
    cleared = detail.copy()
    groups = []
    for zone_index, indices, part, finish, name in MARKS[key]:
        zone = zones[zone_index]
        stencils = []
        for index in indices:
            points, low, mask = source_stencil(zone['strokes'][index-1])
            x0, y0 = low
            h, w = mask.shape
            tone = detail[y0:y0+h, x0:x0+w] * mask[..., None]
            assert np.count_nonzero(tone.max(axis=2) > .09) > 20
            cleared[y0:y0+h, x0:x0+w] *= (1 - mask[..., None])
            stencils.append({'index': index, 'low': low, 'mask': mask, 'tone': tone, 'points': points})
        host = np.asarray([face == part and coat.startswith(finish) for face, coat in zip(faces, finishes, strict=True)] + [False])
        assert host.any(), (key, part, finish)
        mapping = rail_mapping() if part == 'polymsh049' else register_zone(zone)[0]
        placed, initial, movement, scale = fit_group(stencils, mapping, host[owner])
        groups.append({'zone': zone['feature'], 'name': name, 'part': part, 'finish': finish,
                       'stencils': stencils, 'host_faces': host, 'mapping': placed,
                       'initial_host_fraction': initial, 'movement_px': movement, 'uniform_scale': scale})
    return cleared, groups


def stamp_marks(data, groups, owner):
    reports = []
    candidates = []
    for group in groups:
        mapping = group['mapping']
        inverse = np.linalg.inv(mapping[:2])
        marks = []
        for stencil in group['stencils']:
            x0, y0 = stencil['low']
            height, width = stencil['mask'].shape
            corners = [[x0, y0], [x0 + width, y0], [x0, y0 + height], [x0 + width, y0 + height]]
            projected = forward(corners, mapping) * SCALE
            lo = np.maximum(np.floor(projected.min(axis=0)).astype(int), 0)
            hi = np.minimum(np.ceil(projected.max(axis=0)).astype(int), owner.shape[::-1])
            u0, v0 = lo
            u1, v1 = hi
            xx, yy = np.meshgrid(np.arange(u0, u1), np.arange(v0, v1))
            pixels = np.column_stack((xx.ravel(), yy.ravel()))
            screen_xy = (pixels + .5) / SCALE
            source_xy = (screen_xy - mapping[2]) @ inverse - stencil['low']
            original = sample(stencil['tone'], *source_xy.T)
            region = owner[yy.ravel(), xx.ravel()]
            belongs = group['host_faces'][region]
            footprint = sample(stencil['mask'][..., None], *source_xy.T)[:, 0] >= .5
            fit = float(np.count_nonzero(footprint & belongs) / np.count_nonzero(footprint))
            assert fit >= .985, (group['name'], stencil['index'], fit)
            host = belongs & (original.max(axis=1) > 2 / 255)
            x, y = pixels[host].T
            data[y, x] = np.maximum(data[y, x], original[host])
            visible = np.flatnonzero(host & (original.max(axis=1) > .12))
            assert len(visible) >= 12, (group['name'], stencil['index'])
            spread = visible[np.linspace(0, len(visible) - 1, 12).round().astype(int)]
            candidates.append((pixels[spread], region[spread]))
            marks.append({'stroke': stencil['index'], 'source_outline_px': stencil['points'].tolist(),
                          'b16_host_samples': stencil['b16_support_on_host'], 'b16_footprint_samples': stencil['b16_support'],
                          'b17_host_fraction': round(fit, 5), 'texture_samples': int(host.sum())})
        reports.append({'feature': group['name'], 'zone': group['zone'], 'host': group['part'], 'finish': group['finish'],
                        'affine_host_fraction': round(group['initial_host_fraction'], 5),
                        'translation_px': round(group['movement_px'], 3), 'uniform_scale': group['uniform_scale'], 'marks': marks})
    for mark, (pixels, faces) in zip((mark for row in reports for mark in row['marks']), candidates, strict=True):
        mark['atlas_probes'] = [{'screen_sample': [int(x), int(y)], 'face': int(face),
                                'rg': np.round(data[y, x], 6).tolist()}
                               for (x, y), face in zip(pixels, faces, strict=True)]
    return reports


def build():
    assert Path(bpy.data.filepath) == B16 / 'SSF_A5_RegisteredDetail_B16.blend'
    source = read_json(BASELINE / 'B6_Material_Manifest.json')['models']
    baseline = read_json(B16 / 'B16_Registered_Detail_Manifest.json')['models']
    reference = {row['key']: row for row in read_json(REFERENCES / 'Reference_A5_Manifest.json')['deliverables']}
    edges = read_json(B13 / 'B13_Edge_Ink_Manifest.json')['models']
    zones = read_json(B8 / 'reference_strokes.json')
    textures = ROOT / 'Textures'
    textures.mkdir(exist_ok=True)
    (ROOT / 'Previews').mkdir(exist_ok=True)
    reports = {}
    for key in MARKS:
        blue, red = (bpy.data.objects[name] for name in source[key]['objects'])
        mesh = blue.data
        assert red.data == mesh
        rest = [value.vector.copy() for value in mesh.attributes[REST].data]
        vertex_parts = original_parts(blue)
        parts = [Counter(vertex_parts[i] for i in face.vertices).most_common(1)[0][0] for face in mesh.polygons]
        finishes = [blue.material_slots[face.material_index].material.get('Finish', '') for face in mesh.polygons]
        classes = [0 if item.value == 0 else 2 for item in mesh.attributes['SSF Original Material Index'].data]
        model = edges[key]
        points = reference_projection(rest, model['reference_camera'])
        owner = projected_ownership(points, mesh.polygons, classes)
        ref = REFERENCES / reference[key]['file']
        assert hashlib.sha256(ref.read_bytes()).hexdigest() == reference[key]['sha256'] == baseline[key]['reference_sha256']
        detail, groups = prepare_marks(key, zones[key], reference_detail(ref), parts, finishes, owner)
        registration = MarkRegistration(model, owner, parts, groups)
        data, _ = extract_surface_details(key, detail, zones[key], model, owner, parts, registration)
        assert registration.reports == baseline[key]['registrations']
        marks = stamp_marks(data, groups, owner)
        assert not data[owner < 0].any()
        texels, uv, faces = pack_tiles(mesh, points, owner, data)
        mesh.uv_layers[UV_NAME].uv.foreach_set('vector', uv.ravel())
        node = next(node for node in bpy.data.node_groups[f'{key} | A5 Surface Detail'].nodes if node.type == 'TEX_IMAGE')
        previous = node.image
        node.image = save_texture(key, texels, textures)
        if previous.users == 0:
            bpy.data.images.remove(previous)
        digest = hashlib.sha256(node.image.packed_file.data).hexdigest()
        assert digest != baseline[key]['atlas_sha256']
        reports[key] = {'reference_sha256': reference[key]['sha256'], 'atlas': node.image.filepath_raw[2:],
                        'atlas_sha256': digest, 'atlas_size_px': list(node.image.size), 'mapped_faces': len(faces), 'marks': marks}
        for group in marks:
            print(key, group['feature'], group['host'], group['translation_px'], group['uniform_scale'],
                  [(mark['b16_host_samples'], mark['b16_footprint_samples'], mark['b17_host_fraction']) for mark in group['marks']], flush=True)
    for key in set(source) - set(MARKS):
        node = next(node for node in bpy.data.node_groups[f'{key} | A5 Surface Detail'].nodes if node.type == 'TEX_IMAGE')
        assert hashlib.sha256(node.image.packed_file.data).hexdigest() == baseline[key]['atlas_sha256']
        node.image.filepath_raw = '//../Production_FeatureConcepts_B16/' + baseline[key]['atlas']
    for scene in bpy.data.scenes:
        key = next((key for key, item in source.items() if item['scene'] == scene.name), 'SSF_Overview')
        scene.render.filepath = f'//Previews/{key}_B17.png'
        assert scene[STYLE_PROPERTY] == 1.
    bpy.context.window.scene = bpy.data.scenes[source['CommandCenter']['scene']]
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(DEST), compress=True, relative_remap=False)
    manifest = {'base': 'B16', 'reference': 'A5', 'models': reports, 'visual_review': 'Pending user review in Blender'}
    (ROOT / 'B17_Surface_Marks_Manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n',
                                                        encoding='utf-8')


if __name__ == '__main__':
    build()
