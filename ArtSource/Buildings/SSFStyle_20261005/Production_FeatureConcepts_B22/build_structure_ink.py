"""Strengthen the B21 structure lines and export a matching UE preview."""

import argparse
import hashlib
import math
import sys
from collections import Counter, defaultdict
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector


HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
BASE = ROOT / 'Production_FeatureConcepts_B21'
SOURCE = BASE / 'SSF_A5_ComponentColor_B21.blend'
OUTPUT = HERE / 'SSF_A5_StructureInk_B22.blend'
MANIFEST = HERE / 'B22_Structure_Ink_Manifest.json'
sys.dont_write_bytecode = True
sys.path.insert(0, str(BASE))
from build_component_colors import (COMPONENT_ID, EDGE_ATTRIBUTES, ORIGINAL_CLASS, OUTLINE,
                                    REST, STYLE_PROPERTY, component_faces, edge_hash, export_preview,
                                    geometry_digest, linear_color, original_parts, read_json, write_json)


INK = dict(color='#150017', gain=1., fade=(.84, 1.12), opacity=1., outline_strength=.94)
EDGE_SCALE = 1.65
SHELL_SCALE = 2.
FOLD_DOT = math.cos(math.radians(43))
RING_LEVELS = {'Ring': (8.577, 9.433), 'Ring2': (5.63, 6.50)}
MAIN_STRUCTURE = {
    'AirBase': {
        'polymsh045', 'polymsh049', 'polymsh046', 'polymsh050', 'polymsh19', 'polymsh052',
        'polymsh22', 'polymsh23', 'cube16', 'cube22', 'cube30', 'cube45',
    },
    'CloningCenter': {
        'BackPart', 'Basis', 'Column', 'Door', 'LeftPart', 'MainBlock', 'RightPart',
        *(f'Capsule{i}' for i in range(1, 6)),
    },
    'CommandCenter': {
        'BackBlock', 'BlockA', 'BlockL', 'BlockR', 'Gate', 'LBlock1', 'LBlock4',
        'MainBlock', 'Ring', 'Ring1', 'Ring2', 'Tower', 'TowerBase',
    },
    'MilitaryFactory': {
        'CivilBlock_L', 'CivilBlock_M', 'CivilBlock_R', 'MainBlock', 'PartStructure1', 'PartStructure2',
        *(f'DoorPart{i}' for i in range(1, 5)),
    },
    'Reactor': {'Airing', 'Block1', 'Block2', 'Block3', 'Hatch'},
    'StrategyCenter': {
        '0_cylinder', '0_cube47', '0_cube48', '0_cube60', '0_cube74', '0_cube78',
        '0_cube79', '0_cube80', '0_cube81', '0_cube100', '0_cube103', '0_polymsh7', '0_polymsh21',
    },
}
MORE_OUTLINES = {
    'AirBase': {'cube16', 'polymsh045', 'polymsh049'},
    'CloningCenter': {'LeftPart', 'RightPart'},
    'CommandCenter': {'BlockL', 'BlockR', 'BlockA'},
    'MilitaryFactory': set(),
    'Reactor': {'Airing'},
    'StrategyCenter': {'0_cube79', '0_cube48'},
}


def vector_rows(collection, field, size):
    values = np.empty(len(collection) * size, dtype='<f4')
    collection.foreach_get(field, values)
    return values.reshape(-1, size)


def signature(row):
    return tuple(round(float(value) * 10000) for value in row)


def edge_key(keys, u, v):
    return tuple(sorted((keys[u], keys[v])))


def edge_topology(mesh, positions, original):
    keys = [signature(p) for p in positions]
    topology = defaultdict(list)
    normals = {}
    for face in mesh.polygons:
        if original[face.index] != 0:
            continue
        a, b, c = (positions[i] for i in face.vertices)
        normals[face.index] = (b - a).cross(c - a).normalized()
        for u, v in face.edge_keys:
            if keys[u] != keys[v]:
                topology[edge_key(keys, u, v)].append((face.index, u, v))
    return keys, topology, normals


def structural_edges(key, topology, normals, positions, owners, row, existing, span):
    components = {entry['id']: entry for entry in row['components']}
    candidates = defaultdict(list)
    for corners, sides in topology.items():
        if corners in existing:
            continue
        ids = {owners[face] for face, _, _ in sides}
        if len(ids) != 1 or len(sides) > 2:
            continue
        owner = ids.pop()
        part = components[owner]['source_part']
        if part not in MAIN_STRUCTURE[key]:
            continue
        _, u, v = sides[0]
        a, b = positions[u], positions[v]
        length = (a - b).length
        dot = normals[sides[0][0]].dot(normals[sides[1][0]]) if len(sides) == 2 else 0.
        if len(sides) == 2 and (dot > FOLD_DOT or dot < -.985):
            continue
        if key == 'CommandCenter' and part in RING_LEVELS:
            z = (a.z + b.z) * .5
            if len(sides) == 2 and abs(a.z - b.z) <= .035 and length >= .15 and min(
                    abs(z - level) for level in RING_LEVELS[part]) <= .018:
                yield corners, sides, 'observation_ring_fold', part
            continue
        if length < .085 * span or components[owner]['surface_area_m2'] < .024 * span * span:
            continue
        reason = 'panel_fold' if len(sides) == 2 else 'component_rim'
        score = length * (1. + .25 * (1. - max(-.75, dot)))
        candidates[owner].append((score, corners, sides, reason, part))
    for owner, entries in candidates.items():
        area = components[owner]['surface_area_m2']
        limit = 10 if area >= .1 * span * span else 6
        for _, corners, sides, reason, part in sorted(entries, reverse=True)[:limit]:
            yield corners, sides, reason, part


def widen_existing(mesh, positions, keys, old_rows, arrays):
    distance, start, end = arrays
    existing = {tuple(map(tuple, row['corners_1e4m'])): row for row in old_rows.values()}
    matches = defaultdict(set)
    effective_scales = []
    for face in mesh.polygons:
        loops = list(face.loop_indices)
        vertices = list(face.vertices)
        for opposite in range(3):
            other = [i for i in range(3) if i != opposite]
            column = distance[loops, opposite]
            if not all(column[i] == 0. for i in other) or column[opposite] <= 0.:
                continue
            u, v = (vertices[i] for i in other)
            a, b = positions[u], positions[v]
            altitude = ((b - a).cross(positions[vertices[opposite]] - a)).length / (b - a).length
            corners = edge_key(keys, u, v)
            row = existing[corners]
            matches[corners].add(face.index)
            old_width = altitude / float(column[opposite])
            assert abs(row['half_width_m'] - old_width) < .0001
            factor = min(EDGE_SCALE, .45 * altitude / old_width)
            assert factor > 1.
            for array in arrays:
                array[loops, opposite] /= factor
            effective_scales.append(factor)
    assert matches == {corners: set(row['faces']) for corners, row in existing.items()}
    return existing, effective_scales


def apply_new_edge(mesh, positions, keys, corners, sides, width, arrays):
    distance, start, end = arrays
    data = []
    for face, u, v in sides:
        if keys[u] != corners[0]:
            u, v = v, u
        polygon = mesh.polygons[face]
        opposite = next(i for i, vertex in enumerate(polygon.vertices) if vertex not in (u, v))
        a, b = positions[u], positions[v]
        axis = b - a
        tangent = axis.normalized()
        h = axis.cross(positions[polygon.vertices[opposite]] - a).length / axis.length
        data.append((polygon, opposite, a, tangent, axis.length, h))
    width = min(width, min(sample[-1] for sample in data) * .40, data[0][-2] * .13)
    if width < .010:
        return 0.
    for polygon, opposite, a, tangent, length, altitude in data:
        loops = list(polygon.loop_indices)
        assert all(np.all(array[loops, opposite] == 8.) for array in arrays)
        for i, (loop, vertex) in enumerate(zip(loops, polygon.vertices, strict=True)):
            along = (positions[vertex] - a).dot(tangent)
            distance[loop, opposite] = altitude / width if i == opposite else 0.
            start[loop, opposite] = -along / width
            end[loop, opposite] = (along - length) / width
    return width


def add_outline_shells(key, mesh, positions, original, parts):
    outline_slot = mesh.materials.find(OUTLINE)
    shell = [f.index for f in mesh.polygons if original[f.index] == 2]
    islands = component_faces(mesh, shell, parts, positions)
    additions = {}
    for part in sorted(MORE_OUTLINES[key]):
        eligible = [island for island in islands if parts[island[0]] == part]
        largest = max(eligible, key=lambda island: sum(mesh.polygons[i].area for i in island))
        assert all(mesh.polygons[i].material_index != outline_slot for i in largest)
        for i in largest:
            mesh.polygons[i].material_index = outline_slot
        additions[part] = len(largest)
    return additions


def thicken_outline(obj, positions, original):
    mesh = obj.data
    shell_ids = {f.index for f in mesh.polygons if original[f.index] == 2
                 and mesh.materials[f.material_index].name == OUTLINE}
    shell = {i for f in shell_ids for i in mesh.polygons[f].vertices}
    other = {i for f in mesh.polygons if f.index not in shell_ids for i in f.vertices}
    assert not shell & other
    groups = original_parts(obj)
    body = defaultdict(set)
    points = np.array([p[:] for p in positions], dtype=np.float64)
    for face in mesh.polygons:
        if original[face.index] == 0:
            for v in face.vertices:
                body[groups[v]].add(v)
    candidates = {part: points[sorted(indices)] for part, indices in body.items()}
    inverse = obj.matrix_world.to_3x3().inverted()
    offset = []
    for v in sorted(shell):
        matches = candidates[groups[v]]
        delta = points[v] - matches
        displacement = delta[np.argmin(np.einsum('ij,ij->i', delta, delta))]
        distance = float(np.linalg.norm(displacement))
        assert 0. < distance < .05, (obj.name, groups[v], v, distance)
        obj.data.vertices[v].co += inverse @ Vector(displacement * (SHELL_SCALE - 1.))
        offset.append(distance)
    mesh.update()
    return shell, [min(offset), max(offset)]


def revise(key, row, original_edges):
    a, b = (bpy.data.objects[name] for name in row['objects'])
    mesh = a.data
    assert mesh == b.data and edge_hash(mesh) == row['edge_attributes_sha256']
    original = [item.value for item in mesh.attributes[ORIGINAL_CLASS].data]
    assert geometry_digest(a, original) == row['geometry_uv_weights_sha256']
    positions = [item.vector.copy() for item in mesh.attributes[REST].data]
    span = max(max(p[i] for p in positions) - min(p[i] for p in positions) for i in range(2))
    owners = [item.value for item in mesh.attributes[COMPONENT_ID].data]
    keys, topology, normals = edge_topology(mesh, positions, original)
    arrays = [vector_rows(mesh.attributes[name].data, 'vector', 3) for name in EDGE_ATTRIBUTES]
    existing, scales = widen_existing(mesh, positions, keys, original_edges, arrays)
    new_edges = []
    for corners, sides, reason, part in structural_edges(key, topology, normals, positions, owners, row, existing, span):
        width = apply_new_edge(mesh, positions, keys, corners, sides, max(.024, span * .0045), arrays)
        if width:
            new_edges.append(dict(corners_1e4m=corners, faces=[face for face, _, _ in sides],
                                  part=part, kind=reason, half_width_m=round(width, 6)))
    assert new_edges and all(np.isfinite(array).all() for array in arrays)
    for name, array in zip(EDGE_ATTRIBUTES, arrays, strict=True):
        mesh.attributes[name].data.foreach_set('vector', array.ravel())
    groups = original_parts(a)
    parts = {f.index: Counter(groups[v] for v in f.vertices).most_common(1)[0][0]
             for f in mesh.polygons if original[f.index] == 2}
    extra_shells = add_outline_shells(key, mesh, positions, original, parts)
    original_vertices = vector_rows(mesh.vertices, 'co', 3)
    shell, displacement = thicken_outline(a, positions, original)
    vertex_rows = vector_rows(mesh.vertices, 'co', 3)
    assert set(np.flatnonzero(np.any(vertex_rows != original_vertices, axis=1))) == shell
    final_digest = geometry_digest(a, original)
    mesh.vertices.foreach_set('co', original_vertices.ravel())
    assert geometry_digest(a, original) == row['geometry_uv_weights_sha256']
    mesh.vertices.foreach_set('co', vertex_rows.ravel())
    mesh.update()
    assert len(mesh.polygons) == row['triangles'] and geometry_digest(b, original) == final_digest
    a['Ink revision'] = b['Ink revision'] = 'B22'
    row.update(
        geometry_uv_weights_sha256=final_digest,
        edge_attributes_sha256=edge_hash(mesh),
        structure_ink_edges=len(existing) + len(new_edges),
        additional_edges=new_edges,
        ink_scale_range=[round(min(scales), 5), round(max(scales), 5)],
        outline_scale=SHELL_SCALE, outline_vertices=len(shell), original_shell_gap_m=displacement,
        additional_outline_parts=extra_shells,
        role_triangles=dict(Counter(row['roles'][f.material_index] for f in mesh.polygons)),
    )


def set_ink_materials():
    color = linear_color(INK['color'])
    for mat in bpy.data.materials:
        if not mat.name.startswith('SSF B21 | '):
            continue
        nodes = mat.node_tree.nodes
        stroke = next(n for n in nodes if n.label == 'A5 ink along original model edges')
        stroke.label = 'B22 structure ink'
        stroke.inputs[2].default_value = color
        ramp = stroke.inputs[0].links[0].from_node
        ramp.label = 'Narrow antialiasing at the ink boundary'
        ramp.inputs['From Min'].default_value, ramp.inputs['From Max'].default_value = INK['fade']
        ramp.inputs['To Min'].default_value = INK['opacity']
        mat.name = mat.name.replace('B21', 'B22', 1)
    outline = bpy.data.materials[OUTLINE]
    emissive = next(n for n in outline.node_tree.nodes if n.type == 'EMISSION')
    emissive.inputs['Color'].default_value = color
    emissive.inputs['Strength'].default_value = INK['outline_strength']


def build(folder):
    assert Path(bpy.data.filepath) == SOURCE
    report = read_json(BASE / 'B21_Component_Color_Manifest.json')
    assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == report['blend_sha256']
    original = read_json(ROOT / 'Production_FeatureConcepts_B13/B13_Edge_Ink_Manifest.json')['models']
    assert len(report['models']) == 6
    for key, row in report['models'].items():
        revise(key, row, original[key]['source_edges'])
        print(key, row['original_ink_edges'], row['structure_ink_edges'], row['additional_outline_parts'], flush=True)
    set_ink_materials()
    report.update(source=SOURCE.relative_to(ROOT).as_posix(), source_sha256=report['blend_sha256'], ink=INK)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.context.window.scene = next(scene for scene in bpy.data.scenes if scene.name.startswith('00 | '))
    bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT), compress=True)
    export_preview(report, folder, 'B22')
    report['blend_sha256'] = hashlib.sha256(OUTPUT.read_bytes()).hexdigest()
    write_json(MANIFEST, report)
    write_json(folder / MANIFEST.name, report)


def export_saved_preview(folder):
    assert Path(bpy.data.filepath) == OUTPUT
    report = read_json(MANIFEST)
    assert hashlib.sha256(OUTPUT.read_bytes()).hexdigest() == report['blend_sha256']
    export_preview(report, folder, 'B22')
    write_json(folder / MANIFEST.name, report)


def review(render):
    assert Path(bpy.data.filepath) == OUTPUT
    report = read_json(MANIFEST)
    old = read_json(BASE / 'B21_Component_Color_Manifest.json')
    verified = []
    for key, row in report['models'].items():
        before = old['models'][key]
        blue, red = (bpy.data.objects[name] for name in row['objects'])
        assert blue.data == red.data and row['triangles'] == before['triangles'] == len(blue.data.polygons)
        original = [item.value for item in blue.data.attributes[ORIGINAL_CLASS].data]
        assert geometry_digest(blue, original) == row['geometry_uv_weights_sha256']
        assert edge_hash(blue.data) == row['edge_attributes_sha256']
        assert Counter(row['roles'][f.material_index] for f in blue.data.polygons) == row['role_triangles']
        for obj, team in ((blue, 'Blue'), (red, 'Red')):
            assert any(mod.type == 'ARMATURE' and mod.object for mod in obj.modifiers)
            for role, slot in zip(row['roles'], obj.material_slots, strict=True):
                if role in ('Hidden', 'Outline'):
                    continue
                choice = old['palette'][role]
                color = choice[team] if isinstance(choice, dict) else choice
                assert slot.material['Base sRGB'] == color
                nodes = slot.material.node_tree.nodes
                assert not any(node.type == 'TEX_IMAGE' for node in nodes)
                stroke = next(node for node in nodes if node.label == 'B22 structure ink')
                assert all(abs(x-y) < 1.e-7 for x, y in zip(stroke.inputs[2].default_value, linear_color(INK['color'])))
                steps = [round(n.inputs[1].default_value, 2) for n in nodes
                         if n.type == 'MATH' and n.operation == 'GREATER_THAN']
                assert sorted(steps) == old['thresholds']
        verified.append(key)
    print('Verified saved models', verified, flush=True)
    if render:
        render_previews(report)


def render_previews(report):
    output = HERE / 'Previews'
    output.mkdir(exist_ok=True)
    pairs = {row['scene']: (key, row) for key, row in report['models'].items()}
    for scene in bpy.data.scenes:
        assert scene.render.engine == 'BLENDER_EEVEE' and scene[STYLE_PROPERTY] == 1.
        bpy.context.window.scene = scene
        scene.render.image_settings.file_format = 'PNG'
        scene.render.image_settings.color_mode = 'RGB'
        key = 'SSF_Overview'
        if scene.name in pairs:
            key, row = pairs[scene.name]
            scene.render.resolution_x, scene.render.resolution_y = 1600, 1000
            for obj in scene.objects:
                if obj.name.startswith('A5 reference |'):
                    obj.hide_render = True
            camera = scene.camera
            bpy.context.view_layer.update()
            transform = camera.matrix_world.inverted()
            corners = [transform @ bpy.data.objects[name].matrix_world @ Vector(p)
                       for name in row['objects'] for p in bpy.data.objects[name].bound_box]
            low = [min(p[i] for p in corners) for i in range(2)]
            high = [max(p[i] for p in corners) for i in range(2)]
            camera.location += camera.matrix_world.to_quaternion() @ Vector(((low[0]+high[0])/2, (low[1]+high[1])/2, 0))
            camera.data.ortho_scale = 1.11 * max(high[0]-low[0], (high[1]-low[1])*1.6)
        scene.render.filepath = str(output / f'{key}_B22.png')
        bpy.ops.render.render(write_still=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--build', type=Path)
    mode.add_argument('--export-preview', type=Path)
    parser.add_argument('--render', action='store_true')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    if args.build:
        build(args.build)
    elif args.export_preview:
        export_saved_preview(args.export_preview)
    else:
        review(args.render)
