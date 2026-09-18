"""Approved Batch03 surface treatment; preserve original static mesh data."""
import collections
import importlib.util
import json
import math
import os
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector
from mathutils.kdtree import KDTree

PROJECT = Path('D:/UE5.7/test1')
ROOT = PROJECT / 'ArtSource/Ships/ShipComponentStyle_20260917/Batch03'
VERSION = os.environ.get('SHIP_BATCH03_VERSION', 'v3')
KEYS = ('Drone_LaunchBay', 'Electronic_JammingDevice', 'Shield_Generator')
spec = importlib.util.spec_from_file_location('original_surface_pipeline', PROJECT / 'Scripts/Blender/style_ship_component_original_materials.py')
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)
s.ROOT = ROOT
s.OUT = ROOT / 'Production' / VERSION
s.TEX = s.OUT / 'Textures'
s.PRE = ROOT / 'Previews' / VERSION
s.SNAP = json.loads((ROOT / 'Source/source_snapshot_v1.json').read_text(encoding='utf-8'))
s.SOURCE_FILES = {key: ROOT / f'Source/FBX_static_v1/SM_SC_{key}.fbx' for key in KEYS}
s.PALETTE.update({'Yellow': 'E0BC49', 'Green': '65955D'})
for folder in (s.OUT, s.TEX, s.PRE):
    folder.mkdir(parents=True, exist_ok=True)
APPROVAL = json.loads((ROOT / 'approval_A_20260917.json').read_text(encoding='utf-8-sig'))
assert APPROVAL['stage'] == 'A' and APPROVAL['decision'] == 'approved'
for key, part in APPROVAL['parts'].items():
    for field in ('reference_sheet', 'original_static_fbx', 'original_inspection_blend', 'original_geometry_snapshot'):
        record = part[field]
        assert s.filehash(ROOT / record['path']) == record['sha256'], (key, field, 'approved input changed')


def load_original(key):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.name = key + '_OriginalMesh_Surface'
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 1
    # Load the exact already-inspected mesh and its original import transform.
    inspection = ROOT / APPROVAL['parts'][key]['original_inspection_blend']['path']
    with bpy.data.libraries.load(str(inspection), link=False) as (src, dst):
        dst.objects = [name for name in src.objects if name != 'OriginalReferenceCamera']
    objects = [obj for obj in dst.objects if obj and obj.type == 'MESH']
    assert len(objects) == 1
    body = objects[0]
    collection = bpy.data.collections.new('ORIGINAL_BODY_UNCHANGED')
    scene.collection.children.link(collection)
    collection.objects.link(body)
    assert len(body.modifiers) == 0 and body.parent is None
    body.hide_set(False)
    body.hide_render = False
    body['source_file'] = str(s.SOURCE_FILES[key].relative_to(ROOT))
    body['production_scope'] = 'Original local vertices, topology, normals and object transform retained; UV/material changes only'
    bpy.context.view_layer.update()
    src = json.loads((ROOT / f'Source/{key}_original_geometry.json').read_text(encoding='utf-8'))
    seed_by_id = {i: c['seed'] for c in src['components'] for i in c['ids']}
    kd = KDTree(len(src['geometry']['points']))
    for i, p in enumerate(src['geometry']['points']):
        kd.insert(s.point(p), i)
    kd.balance()
    seeds = []
    error = 0
    for vertex in body.data.vertices:
        _, i, distance = kd.find(body.matrix_world @ vertex.co)
        seeds.append(seed_by_id[i])
        error = max(error, distance)
    assert error < .0002, (key, error)
    regions = [{'seed': seed, 'vertices': count, 'bone': None} for seed, count in sorted(collections.Counter(seeds).items())]
    body['material_region_recovery'] = json.dumps(regions)
    body['source_world_vertex_match_max_error_m'] = error
    body['source_object_matrix'] = json.dumps([list(row) for row in body.matrix_world])
    print('STATIC_ORIGINAL_LOADED', key, len(seeds), error, flush=True)
    return body, None, src, seeds


def coherent_planes(mesh, seeds, labels):
    """Use whole connected coplanar surfaces to avoid triangular paint artifacts."""
    parent = list(range(len(mesh.polygons)))
    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    edges = collections.defaultdict(list)
    for p in mesh.polygons:
        ids = list(p.vertices)
        for a, b in zip(ids, ids[1:] + ids[:1]):
            edge = tuple(sorted(tuple(round(x, 4) for x in mesh.vertices[v].co) for v in (a, b)))
            edges[edge].append(p.index)
    for faces in edges.values():
        for a in faces:
            for b in faces:
                if a < b and seeds[mesh.polygons[a].vertices[0]] == seeds[mesh.polygons[b].vertices[0]] and mesh.polygons[a].normal.dot(mesh.polygons[b].normal) > .999995:
                    parent[root(b)] = root(a)
    groups = collections.defaultdict(list)
    for p in mesh.polygons:
        groups[root(p.index)].append(p.index)
    for faces in groups.values():
        votes = collections.defaultdict(float)
        for i in faces:
            votes[labels[i]] += mesh.polygons[i].area
        label = max(votes, key=votes.get)
        for i in faces:
            labels[i] = label
    return labels


def material_labels(key, mesh, seeds):
    labels = []
    for p in mesh.polygons:
        seed = collections.Counter(seeds[v] for v in p.vertices).most_common(1)[0][0]
        c, n = p.center * .01, p.normal
        label = 'Pearl'
        if key == 'Electronic_JammingDevice':
            if seed in (48, 62, 98, 177, 286, 335):
                label = 'Steel'
            elif seed in (262, 323):
                label = 'Yellow'
            elif seed in (191, 315):
                label = 'Yellow' if n.z > -.2 else 'Navy'
            elif seed == 76:
                label = 'Yellow' if n.z > .7 and c.y < 0 else ('Pearl' if n.z > .4 else 'Navy')
            elif seed == 112:
                label = 'Pearl' if n.z > .1 or c.z > -1.75 else 'Navy'
            elif seed in (0, 214):
                if abs(c.x) < 1.4:
                    label = 'Yellow' if abs(n.y) > .7 or n.z > .9 else 'Slate'
                elif n.y > .9 or (n.y < -.9 and n.dot(c) > -1.7):
                    label = 'Navy'
        elif key == 'Shield_Generator':
            if seed in (0, 65, 102, 182, 47, 174):
                label = 'Steel'
            elif seed in (55, 192):
                label = 'Green' if n.y < .5 and n.z > -.5 else 'Navy'
            elif seed == 75:
                label = 'Green' if n.y < -.99 or n.z > .99 else 'Navy'
            elif seed == 10:
                label = 'Pearl' if n.z > .1 else 'Navy'
            elif seed in (112, 148):
                label = 'Pearl'
            elif seed == 130:
                label = 'Pearl' if n.z > -.5 else 'Navy'
        else:
            if seed == 0:
                label = 'Navy' if abs(n.x) > .7 else 'Pearl'
            elif seed == 228:
                label = 'Navy' if abs(n.x) > .8 and abs(c.z) < 5.8 else 'Pearl'
                if abs(n.y) > .99 and (abs(c.y - 23.9608) < .01 or abs(c.y + 25.095) < .01):
                    label = 'Ochre'
            elif seed in (12, 34, 56, 78, 296, 318, 340, 362):
                label = 'Pearl'
            elif seed in (384, 410, 788, 796, 804, 812, 212, 468, 508, 548, 588, 628, 668, 820):
                label = 'Steel'
            elif seed in (100, 140, 732, 772, 436, 116, 156, 708, 748):
                label = 'Slate'
            else:
                label = 'Ochre'
        labels.append(label)
    labels = coherent_planes(mesh, seeds, labels)
    names = list(s.PALETTE)
    index = mesh.attributes.new('SC_PaintPaletteIndex', 'INT', 'FACE')
    colors = mesh.color_attributes.new(name='SC_EditablePaintColor', type='FLOAT_COLOR', domain='CORNER')
    for p, label in zip(mesh.polygons, labels):
        index.data[p.index].value = names.index(label)
        for loop in p.loop_indices:
            colors.data[loop].color = s.linear(s.PALETTE[label])
    s.dump(s.OUT / f'{key}_paint_regions.json', {'reference_version': APPROVAL['parts'][key]['reference_version'],
        'palette_sRGB': s.PALETTE, 'palette_index_names': names, 'face_labels': labels,
        'face_count_by_color': dict(collections.Counter(labels)), 'note': 'Original faces and coplanar patches; no geometry/normal edits.'})
    return labels


original_outline = s.outline
def outline(body, key):
    obj = original_outline(body, key)
    width = max(body.dimensions) * .0012
    obj.modifiers['Outline_Width'].strength = -width / body.scale.x
    obj['outline_width_m'] = width
    return obj


def line_faces(mesh, labels, key):
    edges = collections.defaultdict(list)
    lo, hi = s.bounds([v.co for v in mesh.vertices])
    span = max(hi - lo)
    radius, minimum = span * .0006, span * .0045
    for p in mesh.polygons:
        ids = list(p.vertices)
        for a, b in zip(ids, ids[1:] + ids[:1]):
            edge = tuple(sorted(tuple(round(v, 4) for v in mesh.vertices[i].co) for i in (a, b)))
            edges[edge].append(p.index)
    result, selected = {p.index: [] for p in mesh.polygons}, 0
    for (a, b), faces in edges.items():
        faces = list(set(faces))
        if (Vector(a) - Vector(b)).length < minimum:
            continue
        sharp = len(faces) == 1 or any(mesh.polygons[x].normal.dot(mesh.polygons[y].normal) < math.cos(math.radians(40)) or labels[x] != labels[y]
            for i, x in enumerate(faces) for y in faces[i + 1:])
        if sharp:
            selected += 1
            for p in faces:
                result[p].append((np.array(a, dtype=np.float32), np.array(b, dtype=np.float32), radius))
    return result, selected, radius


original_save_image = s.save_image
def save_image(*args, **kwargs):
    im = original_save_image(*args, **kwargs)
    im.use_fake_user = True
    return im


original_toon = s.toon_material
def toon_material(*args, **kwargs):
    mat = original_toon(*args, **kwargs)
    mat.use_fake_user = True
    return mat


# Editable native vector motifs, painted through world-space coordinates onto
# the preserved model's new color UV. They are not extra mesh or baked light.
WINGMAN = [(0, 1), (.14, .82), (.16, .36), (.88, -.10), (.88, -.29), (.23, -.10),
           (.20, -.60), (.47, -.80), (.47, -.96), (.11, -.79), (0, -.96),
           (-.11, -.79), (-.47, -.96), (-.47, -.80), (-.20, -.60), (-.23, -.10),
           (-.88, -.29), (-.88, -.10), (-.16, .36), (-.14, .82)]
ARROW = [(0, 1), (.9, .48), (.30, .48), (.30, -1), (-.30, -1), (-.30, .48), (-.9, .48)]


def in_polygon(x, y, polygon):
    hit = np.zeros(x.shape, dtype=bool)
    for (ax, ay), (bx, by) in zip(polygon, polygon[1:] + polygon[:1]):
        if abs(by - ay) < 1e-12:
            continue
        hit ^= ((ay > y) != (by > y)) & (x < (bx - ax) * (y - ay) / (by - ay) + ax)
    return hit


original_color_atlas = s.color_atlas
def color_atlas(body, uv, labels):
    paint, orm, coverage = original_color_atlas(body, uv, labels)
    if not body.name.startswith('SM_SC_Drone_LaunchBay'):
        # Mesh naming is not the asset authority; source_file is stable.
        if 'Drone_LaunchBay' not in body['source_file']:
            return paint, orm, coverage
    mesh = body.data
    vertices = np.array([(body.matrix_world @ v.co)[:] for v in mesh.vertices], dtype=np.float32)
    painted = collections.Counter()
    for tri in mesh.loop_triangles:
        p = mesh.polygons[tri.polygon_index]
        if mesh.attributes['OriginalComponentSeed'].data[p.index].value != 0:
            continue
        center = body.matrix_world @ p.center
        exterior = p.normal.x < -.99 and center.x < -7.5
        if not exterior:
            continue
        coords = np.array([uv.data[i].uv[:] for i in tri.loops], dtype=np.float32) * s.SIZE
        a, b = coords[1] - coords[0], coords[2] - coords[0]
        det = a[0] * b[1] - a[1] * b[0]
        if abs(det) < 1e-7:
            continue
        x0, y0 = np.maximum(np.floor(coords.min(axis=0)).astype(int) - 1, 0)
        x1, y1 = np.minimum(np.ceil(coords.max(axis=0)).astype(int) + 1, s.SIZE)
        x = np.arange(x0, x1, dtype=np.float32)[None, :] + .5 - coords[0, 0]
        y = np.arange(y0, y1, dtype=np.float32)[:, None] + .5 - coords[0, 1]
        u = (x * b[1] - y * b[0]) / det
        v = (a[0] * y - a[1] * x) / det
        inside = (u >= -.001) & (v >= -.001) & (u + v <= 1.001)
        points = vertices[tri.vertices]
        pos = points[0] + u[:, :, None] * (points[1] - points[0]) + v[:, :, None] * (points[2] - points[0])
        dpdx = (b[1] * (points[1] - points[0]) - a[1] * (points[2] - points[0])) / det
        dpdy = (-b[0] * (points[1] - points[0]) + a[0] * (points[2] - points[0])) / det
        plane = np.zeros(inside.shape, dtype=np.float32)
        arrow = np.zeros_like(plane)
        for dx, dy in ((-.25, -.25), (.25, -.25), (-.25, .25), (.25, .25)):
            sample = pos + dx * dpdx + dy * dpdy
            plane += in_polygon(sample[:, :, 2] / 1.75, (sample[:, :, 1] + 3.0) / 3.6, WINGMAN) * .25
            arrow += in_polygon(sample[:, :, 2] / 1.15, (sample[:, :, 1] - 5.5) / 4.0, ARROW) * .25
        color = 'Pearl'
        plane *= inside
        arrow *= inside
        pixels = paint[y0:y1, x0:x1]
        pixels[:] = pixels * (1 - plane[:, :, None]) + np.array(s.linear(s.PALETTE[color])[:3]) * plane[:, :, None]
        pixels[:] = pixels * (1 - arrow[:, :, None]) + np.array(s.linear(s.PALETTE['Ochre'])[:3]) * arrow[:, :, None]
        orm[y0:y1, x0:x1][(plane > 0) | (arrow > 0)] = (1, .64, .1)
        painted['exterior_wingman'] += int(plane.sum())
        painted['exterior_arrow'] += int(arrow.sum())
    body['launch_direction_blender'] = [0.0, 1.0, 0.0]
    body['launch_direction_UE'] = [0.0, -1.0, 0.0]
    body['launch_marking_method'] = 'Flat BaseColor paint only on left thin-wall exterior, following user annotation; no new geometry or socket'
    s.dump(s.OUT / 'Drone_LaunchBay_launch_markings.json', {
        'reference': 'Drone_LaunchBay_Reference_v2.png', 'placement_override': 'References/Drone_LaunchBay_User_ExteriorMarking_20260917.png',
        'user_instruction': '这里是朝外的，图标放这里', 'direction_blender': [0, 1, 0], 'direction_UE': [0, -1, 0],
        'method': 'Native editable vector motifs projected through original surface into BaseColor UV',
        'wingman_polygon': WINGMAN, 'arrow_polygon': ARROW, 'pixels_painted': dict(painted),
        'exterior': {'component_seed': 0, 'surface_x_m': -7.568, 'normal_blender': [-1, 0, 0],
            'glyph_center_yz_m': [-3.0, 0], 'arrow_center_yz_m': [5.5, 0]},
        'prior_top_and_thick_wall_markings': 'removed; moved to user-specified exterior face',
        'geometry_added': False, 'new_sockets': []})
    def svg_points(poly, cx, cy, sx, sy):
        return ' '.join(f'{cx + x*sx:.2f},{cy - y*sy:.2f}' for x, y in poly)
    svg = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 700"><title>Exterior wingman launch direction +Y; white glyph on navy wall</title><polygon fill="#DDE6E6" points="{svg_points(WINGMAN, 200, 500, 100, 150)}"/><polygon fill="#C9903E" points="{svg_points(ARROW, 200, 165, 85, 135)}"/></svg>'
    (s.OUT / 'Drone_LaunchBay_launch_marking.svg').write_text(svg, encoding='utf-8')
    return paint, orm, coverage


def inspect_surfaces():
    result = {}
    for key in KEYS:
        body, _, _, seeds = load_original(key)
        groups = collections.defaultdict(list)
        for p in body.data.polygons:
            seed = collections.Counter(seeds[v] for v in p.vertices).most_common(1)[0][0]
            normal = tuple(round(v, 3) for v in p.normal)
            distance = round(p.normal.dot(p.center) * .01, 3)
            groups[(seed, normal, distance)].append(p)
        rows = []
        for (seed, normal, distance), polys in groups.items():
            area = sum(p.area for p in polys)
            center = sum((p.center * p.area for p in polys), Vector()) / max(area, 1e-20) * .01
            rows.append({'seed': seed, 'n': normal, 'd': distance, 'area_m2': round(area * .0001, 4),
                'c_m': [round(v, 4) for v in center], 'faces': [p.index for p in polys]})
        result[key] = {'matrix': [list(row) for row in body.matrix_world],
            'geometry_hash': s.ink_bake.geometry_hash(body.data), 'surfaces': rows}
    s.dump(s.OUT / 'surface_inventory.json', result)
    print('BATCH03_SURFACE_INVENTORY_COMPLETE', flush=True)


s.load_original = load_original
s.material_labels = material_labels
s.outline = outline
s.line_faces = line_faces
s.save_image = save_image
s.toon_material = toon_material
s.color_atlas = color_atlas


def build(key):
    s.build(key)
    path = s.OUT / f'{key}_material_report.json'
    report = json.loads(path.read_text(encoding='utf-8'))
    report.update({'candidate_version': VERSION, 'reference_version': APPROVAL['parts'][key]['reference_version'],
        'user_A': {'decision': 'approved', 'record': 'approval_A_20260917.json', 'user_message': APPROVAL['user_message']},
        'editable_palette_face_attribute': 'SC_PaintPaletteIndex', 'editable_corner_color_attribute': 'SC_EditablePaintColor',
        'source_object_matrix': json.loads(next(o for o in bpy.context.scene.objects if o.get('source_file'))['source_object_matrix']),
        'internal_line_halfwidth_local_units': report['internal_line_halfwidth_m'],
        'internal_line_halfwidth_m': report['internal_line_halfwidth_m'] * .01,
        'mechanical_type': 'static', 'bones': [], 'connection_fix': 'Whole original component and connected coplanar paint assignment.'})
    s.dump(path, report)


if __name__ == '__main__':
    args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    if '--inspect' in args:
        inspect_surfaces()
    else:
        for key in (args or KEYS):
            if key not in KEYS:
                raise ValueError(key)
            build(key)
        print('BATCH03_MATERIAL_CANDIDATES_COMPLETE', VERSION, flush=True)
