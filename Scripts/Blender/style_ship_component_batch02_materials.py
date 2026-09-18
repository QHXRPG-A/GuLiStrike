"""Approved Batch02 surface treatment on immutable original rigged meshes."""
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

PROJECT = Path('D:/UE5.7/test1')
ROOT = PROJECT / 'ArtSource/Ships/ShipComponentStyle_20260917/Batch02'
VERSION = os.environ.get('SHIP_BATCH02_VERSION', 'v1')
if not VERSION.startswith('v') or not VERSION[1:].isdigit():
    raise ValueError('Expected a version such as v1')
KEYS = ('Autocannon', 'Triple_Barrel_Turret', 'Single_Barrel_Turret')
spec = importlib.util.spec_from_file_location('original_material_pipeline', PROJECT / 'Scripts/Blender/style_ship_component_original_materials.py')
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)
s.ROOT = ROOT
s.OUT = ROOT / 'Production' / VERSION
s.TEX = s.OUT / 'Textures'
s.PRE = ROOT / 'Previews' / VERSION
s.SNAP = json.loads((ROOT / 'Source/source_snapshot_v1.json').read_text(encoding='utf-8'))
s.SOURCE_FILES = {key: ROOT / f'Source/ExistingAuthoring/SC_{key}.blend' for key in KEYS}
for folder in (s.OUT, s.TEX, s.PRE):
    folder.mkdir(parents=True, exist_ok=True)
APPROVAL = json.loads((ROOT / 'approval_A_20260917.json').read_text(encoding='utf-8'))
assert APPROVAL['stage'] == 'A' and APPROVAL['decision'] == 'approved'
for key, part in APPROVAL['parts'].items():
    for field in ('reference_sheet', 'original_authoring', 'original_rig_fbx', 'original_geometry_snapshot'):
        record = part[field]
        assert s.filehash(ROOT / record['path']) == record['sha256'], (key, field, 'approved input changed')


def coherent_planes(mesh, seeds, labels):
    """Color entire connected coplanar patches, never individual triangle guesses."""
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
            edge = tuple(sorted(tuple(round(x, 5) for x in mesh.vertices[v].co) for v in (a, b)))
            edges[edge].append(p.index)
    for faces in edges.values():
        for a in faces:
            for b in faces:
                if a >= b:
                    continue
                pa, pb = mesh.polygons[a], mesh.polygons[b]
                if seeds[pa.vertices[0]] == seeds[pb.vertices[0]] and pa.normal.dot(pb.normal) > .999995:
                    parent[root(b)] = root(a)
    groups = collections.defaultdict(list)
    for p in mesh.polygons:
        groups[root(p.index)].append(p.index)
    for faces in groups.values():
        votes = collections.defaultdict(float)
        for i in faces:
            votes[labels[i]] += mesh.polygons[i].area
        chosen = max(votes, key=votes.get)
        for i in faces:
            labels[i] = chosen
    return labels


def material_labels(key, mesh, seeds):
    labels = []
    for p in mesh.polygons:
        seed = collections.Counter(seeds[v] for v in p.vertices).most_common(1)[0][0]
        n, c = p.normal, p.center
        label = 'Pearl'
        if key == 'Autocannon':
            if seed in (16, 101, 186):
                label = 'Steel' if c.y < -11.95 else 'Slate'
                cx, cz = {16: (.415, .756), 101: (-.415, .756), 186: (0, 1.475)}[seed]
                radial = Vector((c.x - cx, 0, c.z - cz))
                if c.y < -12 and n.dot(radial) < -.05:
                    label = 'Navy'
            elif seed in (271, 297, 323, 406, 689, 0, 349, 613, 661):
                label = 'Steel'
            elif seed in (669, 365):
                label = 'Ochre' if n.z > -.1 else 'Slate'
            elif seed in (384, 726):
                label = 'Slate'
            elif seed == 543:
                label = 'Navy'
            elif seed in (573, 621):
                side = -1 if seed == 573 else 1
                if n.z < -.2:
                    label = 'Navy'
                elif n.x * side > .65:
                    label = 'Ochre'
                elif n.x * side < -.45:
                    label = 'Navy'
            elif seed == 481:
                label = 'Navy' if abs(n.x) > .5 or n.z < -.2 else 'Pearl'
        elif key == 'Triple_Barrel_Turret':
            if seed in (40, 221, 418):
                label = 'Steel'
                cx = {40: -8.91, 221: .02, 418: 8.91}[seed]
                if n.dot(Vector((c.x-cx, 0, c.z-1.30))) < -.1:
                    label = 'Navy'
            elif seed in (306, 362, 503, 537, 589, 671, 569, 621, 703):
                label = 'Slate'
            elif seed in (0, 631, 713):
                label = 'Pearl' if n.z > .08 and c.z > 1 else 'Slate'
                # Existing recessed rectangular top-cover floor, repeated on all 3 hoods.
                if n.z > .98 and abs(c.z - 4.97386) < .005:
                    label = 'Ochre'
            elif seed in (785, 1110):
                label = 'Ochre'
            elif seed in (934, 950, 1026, 1050):
                label = 'Pearl' if n.z > .12 else 'Navy'
            elif seed == 974:
                label = 'Navy'
            elif seed == 887:
                label = 'Pearl' if n.z > .3 else 'Navy'
            elif seed in (1132, 1148, 1164, 851, 871, 1066, 807, 1094):
                label = 'Slate'
            elif seed in (753, 761, 769, 777, 799, 1002, 1010, 1018, 1102, 1124):
                label = 'Steel'
            elif seed in s.rig_tools.SPECS[key]['moving']:
                label = 'Steel'
        else:
            if seed in (98, 171):
                label = 'Slate'
            elif seed == 233:
                label = 'Cyan' if n.z > -.35 else 'Navy'
                if c.y < -2.48 and n.z < .8:
                    label = 'Slate'
            elif seed in (0, 44, 8, 354):
                label = 'Cyan'
            elif seed in (147, 155, 163, 211, 322, 542, 570, 598, 626, 654, 682, 710, 730, 738,
                          22, 76, 219, 280, 294, 308, 454, 476, 36, 60, 68, 90, 404, 468, 490, 498, 52, 436):
                label = 'Steel'
            elif seed in (270, 412, 444, 506):
                label = 'Navy'
            elif seed in (422, 528):
                label = 'Pearl' if n.z > .1 else 'Navy'
            elif seed in (342, 516):
                side = 1 if seed == 342 else -1
                if n.z < -.2:
                    label = 'Navy'
                elif n.x * side > .65:
                    label = 'Cyan'
                elif n.x * side < -.45:
                    label = 'Slate'
            elif seed == 368:
                if c.z < -.9 or n.z < -.3:
                    label = 'Navy'
                elif abs(c.x) < 1.8 and c.y < 4.2 and n.z < .99:
                    label = 'Slate'
                elif abs(n.x) > .6:
                    label = 'Cyan' if c.x * n.x > 0 else 'Slate'
                elif n.y > .6 and abs(c.x) > 2.6:
                    label = 'Navy'
        labels.append(label)
    labels = coherent_planes(mesh, seeds, labels)
    names = list(s.PALETTE)
    index = mesh.attributes.new('SC_PaintPaletteIndex', 'INT', 'FACE')
    colors = mesh.color_attributes.new(name='SC_EditablePaintColor', type='FLOAT_COLOR', domain='CORNER')
    for p, label in zip(mesh.polygons, labels):
        index.data[p.index].value = names.index(label)
        for loop in p.loop_indices:
            colors.data[loop].color = s.linear(s.PALETTE[label])
    s.dump(s.OUT / f'{key}_paint_regions.json', {
        'reference_version': APPROVAL['parts'][key]['reference_version'], 'palette_sRGB': s.PALETTE,
        'palette_index_names': names, 'face_labels': labels,
        'face_count_by_color': dict(collections.Counter(labels)),
        'note': 'Editable original face/part assignments; no mesh coordinate, topology, normal or rig edits.'})
    return labels


original_outline = s.outline
def outline(body, key):
    obj = original_outline(body, key)
    width = max(body.dimensions) * .0012
    obj.modifiers['Outline_Width'].strength = -width
    obj['outline_width_m'] = width
    return obj


def line_faces(mesh, labels, key):
    edges = collections.defaultdict(list)
    points = [v.co for v in mesh.vertices]
    lo, hi = s.bounds(points)
    span = max(hi-lo)
    radius, minimum = span * .0006, span * .0045
    for p in mesh.polygons:
        ids = list(p.vertices)
        for a, b in zip(ids, ids[1:] + ids[:1]):
            pa = tuple(round(v, 5) for v in mesh.vertices[a].co)
            pb = tuple(round(v, 5) for v in mesh.vertices[b].co)
            edges[tuple(sorted((pa, pb)))].append(p.index)
    result, selected = {p.index: [] for p in mesh.polygons}, 0
    for (a, b), faces in edges.items():
        faces = list(set(faces))
        if (Vector(a)-Vector(b)).length < minimum:
            continue
        sharp = len(faces) == 1 or any(
            mesh.polygons[x].normal.dot(mesh.polygons[y].normal) < math.cos(math.radians(40)) or labels[x] != labels[y]
            for i, x in enumerate(faces) for y in faces[i+1:])
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


s.material_labels = material_labels
s.outline = outline
s.line_faces = line_faces
s.save_image = save_image
s.toon_material = toon_material


def build(key):
    s.build(key)
    report_path = s.OUT / f'{key}_material_report.json'
    report = json.loads(report_path.read_text(encoding='utf-8'))
    report['candidate_version'] = VERSION
    report['reference_version'] = APPROVAL['parts'][key]['reference_version']
    report['user_A'] = {'decision': 'approved', 'record': 'approval_A_20260917.json', 'user_message': APPROVAL['user_message']}
    report['editable_palette_face_attribute'] = 'SC_PaintPaletteIndex'
    report['editable_corner_color_attribute'] = 'SC_EditablePaintColor'
    s.dump(report_path, report)


if __name__ == '__main__':
    keys = sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else KEYS
    for key in keys:
        if key not in KEYS:
            raise ValueError(key)
        build(key)
    print('BATCH02_MATERIAL_CANDIDATES_COMPLETE', VERSION, flush=True)
