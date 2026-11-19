"""Build the SSF component-color review from the B13 Blender file."""

import argparse
import hashlib
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector


HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SOURCE = ROOT / 'Production_FeatureConcepts_B13' / 'SSF_A5_ModelEdgeInk_B13.blend'
OUTPUT = HERE / 'SSF_A5_ComponentColor_B21.blend'
MANIFEST = HERE / 'B21_Component_Color_Manifest.json'
sys.dont_write_bytecode = True
sys.path[:0] = [str(ROOT / f'Production_FeatureConcepts_{v}') for v in ('B1', 'B9')]
from build_scene import (ART_LIGHT, ARRAY_BOXES, ARRAY_TILES, CAPSULES, DUCTS, FINISHES,
                         LIGHT_BANDS, REST, SHUTTERS, STYLE_PROPERTY, TEAMS, Nodes,
                         component_faces, geometry_digest, linear_color, original_parts, scene_style)
from build_edge_ink import DISTANCE, END, START, connect_edges


OUTLINE = 'SSF original outline shell'
HIDDEN = 'SSF suppressed outline faces'
ORIGINAL_CLASS = 'SSF Original Material Index'
COMPONENT_ID = 'SSF Paint Component'
EDGE_ATTRIBUTES = (DISTANCE, START, END)
THRESHOLDS = (.33, .69)
FIXED = {key.title(): FINISHES[key].color for key in ('ivory', 'sand', 'sage', 'graphite', 'amber')}
FIXED.update(Recess=FINISHES['dark'].color, Jade='#0A6F73')
FACTION = {'Primary': 0, 'Secondary': 1}
ORIGINAL_FINISH = {v.label: k for k, v in FINISHES.items()}
WHOLE_FINISH = {
    'silver': 'Ivory', 'rubber': 'Graphite', 'dark': 'Recess', 'shoulder': 'Primary',
    'cap': 'Secondary', 'glass': 'Jade', 'upper_ring': 'Recess', 'lower_ring': 'Jade',
    'duct': 'Graphite', 'radiator': 'Sage', 'reactor_lock': 'Amber', 'array': 'Graphite',
    'rear_shell': 'Ivory', 'jamb': 'Sage', 'guide_rail': 'Sage', 'pedestal': 'Sage',
}
# Original alpha selects these display polygons. The active reactor inserts use
# their entire modeled footprint. The transparent display polygons stay hidden.
DISPLAY = {
    'AirBase': ('Ivory', {5198, 5199, *range(5208, 5216)}),
    'Reactor': ('Amber', {3534, 3535, 3538, 3539, 3544, 3545}),
    'StrategyCenter': ('Amber', {*range(4164, 4167), *range(4171, 4175)}),
}


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def write_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def edge_hash(mesh):
    digest = hashlib.sha256()
    for name in EDGE_ATTRIBUTES:
        attribute = mesh.attributes[name]
        assert attribute.domain == 'CORNER' and attribute.data_type == 'FLOAT_VECTOR'
        values = np.empty(len(mesh.loops) * 3, dtype='<f4')
        attribute.data.foreach_get('vector', values)
        digest.update(values.tobytes())
    return digest.hexdigest()


def material_color(role, team):
    return TEAMS[team][FACTION[role]] if role in FACTION else FIXED[role]


def create_material(role, team):
    if role == 'Outline':
        return bpy.data.materials[OUTLINE]
    if role == 'Hidden':
        return bpy.data.materials[HIDDEN]
    faction = team if role in FACTION else 'Fixed'
    name = f'SSF B21 | {role} | {faction}'
    if existing := bpy.data.materials.get(name):
        return existing
    color = material_color(role, team)
    material = bpy.data.materials.new(name)
    material.diffuse_color = linear_color(color)
    material['Base sRGB'] = color
    material['Faction'] = faction
    material['Paint Role'] = role
    graph = Nodes(material)
    base = graph.new('ShaderNodeRGB', 'Component base color')
    base.outputs[0].default_value = material.diffuse_color
    direction = graph.new('ShaderNodeVectorMath', 'B13 directional three-tone shading')
    direction.operation = 'DOT_PRODUCT'
    graph.put(direction.inputs[0], graph.new('ShaderNodeNewGeometry').outputs['Normal'])
    direction.inputs[1].default_value = ART_LIGHT
    dark, mid, bright = LIGHT_BANDS
    level = direction.outputs['Value']
    middle_step = graph.math('MULTIPLY', graph.math('GREATER_THAN', level, THRESHOLDS[0]), mid - dark)
    bright_step = graph.math('MULTIPLY', graph.math('GREATER_THAN', level, THRESHOLDS[1]), bright - mid)
    band = graph.math('ADD', graph.math('ADD', dark, middle_step), bright_step)
    painted = graph.mix(1., base.outputs[0], band, 'MULTIPLY', 'Three discrete light bands')
    emission = graph.new('ShaderNodeEmission', 'Three-tone color')
    graph.put(emission.inputs['Color'], painted)
    connect_edges(material)
    stylized = emission.inputs['Color'].links[0].from_socket
    style = scene_style(graph.new('ShaderNodeValue', 'Scene Properties | SSF Stylization').outputs[0])
    graph.put(emission.inputs['Color'], graph.mix(style, base.outputs[0], stylized,
                                                  label='Component color / three-tone and ink'))
    output = graph.new('ShaderNodeOutputMaterial')
    graph.put(output.inputs['Surface'], emission.outputs[0])
    graph.arrange(output)
    return material


def component_role(key, part, count, dominant):
    """Choose one paint for each welded, original rigid component."""
    if key == 'CloningCenter':
        if part in CAPSULES:
            return 'Jade'
        if part == 'BackPart' and dominant == 'rear_shell' and count != 84:
            return 'Sand'
    if key == 'CommandCenter':
        if part in ('LBlock1', 'LBlock4'):
            return 'Primary'
        if part in ('BlockL', 'BlockR') and dominant == 'primary':
            return 'Secondary'
        if part == 'Ring':
            return 'Recess'
        if part == 'Ring2':
            return 'Jade'
    if key == 'MilitaryFactory':
        if part in DUCTS or part.startswith('Pipe'):
            return 'Graphite'
        if part in SHUTTERS:
            return 'Sand'
        if part.startswith('Light') and count == 32:
            return 'Amber'
    if key == 'StrategyCenter':
        if part in ARRAY_TILES and dominant in ('array', 'ivory', 'graphite'):
            return 'Graphite'
        if part in ARRAY_BOXES:
            return 'Primary'
    return WHOLE_FINISH.get(dominant, dominant.title())


def paint(key, record, edge_record):
    blue, red = (bpy.data.objects[name] for name in record['objects'])
    mesh = blue.data
    assert mesh == red.data and len(mesh.polygons) == record['triangles']
    original = [a.value for a in mesh.attributes[ORIGINAL_CLASS].data]
    assert geometry_digest(blue, original) == record['geometry_uv_weights_sha256']
    colors_before = list(slot.material for slot in blue.material_slots)
    edge_digest = edge_hash(mesh)
    positions = [a.vector[:] for a in mesh.attributes[REST].data]
    groups = original_parts(blue)
    parts = {p.index: Counter(groups[v] for v in p.vertices).most_common(1)[0][0] for p in mesh.polygons}
    assigned = ['Hidden'] * len(original)
    component_ids = [0] * len(original)
    table = []
    body = [i for i, cls in enumerate(original) if cls == 0]
    for faces in component_faces(mesh, body, parts, positions):
        part = parts[faces[0]]
        area = Counter()
        for i in faces:
            polygon = mesh.polygons[i]
            old = colors_before[polygon.material_index]
            area[ORIGINAL_FINISH[old['Finish']]] += polygon.area
        dominant = area.most_common(1)[0][0]
        role = component_role(key, part, len(faces), dominant)
        component = len(table) + 1
        for i in faces:
            assigned[i] = role
            component_ids[i] = component
        table.append(dict(id=component, source_part=part, triangles=len(faces), first_face=faces[0],
                          material=role, previous_finishes=list(area), surface_area_m2=round(area.total() / 10000, 5)))

    display_role, active_displays = DISPLAY.get(key, (None, set()))
    assert all(original[i] == 1 for i in active_displays)
    for i, cls in enumerate(original):
        if i in active_displays:
            assigned[i] = display_role
        if cls == 2 and colors_before[mesh.polygons[i].material_index].name == OUTLINE:
            assigned[i] = 'Outline'

    for group in (body, active_displays):
        assert all(assigned[i] in FIXED or assigned[i] in FACTION for i in group)
    used = sorted(set(assigned))
    assert all(role in used for role in FACTION), (key, used)
    mesh.materials.clear()
    for role in used:
        mesh.materials.append(create_material(role, 'Blue'))
    indices = {role: i for i, role in enumerate(used)}
    mesh.polygons.foreach_set('material_index', [indices[role] for role in assigned])
    mesh.attributes.new(COMPONENT_ID, 'INT', 'FACE').data.foreach_set('value', component_ids)
    for model, team in ((blue, 'Blue'), (red, 'Red')):
        for slot, role in zip(model.material_slots, used, strict=True):
            slot.link = 'OBJECT' if team == 'Red' and role in FACTION else 'DATA'
            if slot.link == 'OBJECT':
                slot.material = create_material(role, team)
            assert slot.material == create_material(role, team)
        model['Coating revision'] = 'B21 component colors'
    assert geometry_digest(blue, original) == record['geometry_uv_weights_sha256']
    assert edge_hash(mesh) == edge_digest
    assert sum(row['triangles'] for row in table) == len(body)
    for role in ('Outline', 'Hidden'):
        assert assigned.count(role) > 0
    for mat in set(blue.data.materials) | {slot.material for slot in red.material_slots}:
        assert not any(node.type in ('TEX_IMAGE', 'BSDF_PRINCIPLED', 'BSDF_ANISOTROPIC')
                       for node in mat.node_tree.nodes), mat.name

    scene = bpy.data.scenes[record['scene']]
    assert scene[STYLE_PROPERTY] == 1.
    mesh.name = f'{key} | Shared LOD0 with component color'
    return dict(model_id=record['model_id'], formal_resource=record['formal_resource'],
                scene=scene.name, objects=record['objects'], triangles=len(original),
                geometry_uv_weights_sha256=record['geometry_uv_weights_sha256'],
                edge_attributes_sha256=edge_digest, original_ink_edges=len(edge_record['source_edges']),
                components=table, role_triangles=dict(Counter(assigned)),
                display_faces=sorted(active_displays), roles=used)


def export_preview(report, folder, revision='B21'):
    folder.mkdir(parents=True, exist_ok=True)
    for key, row in report['models'].items():
        scene = bpy.data.scenes[row['scene']]
        bpy.context.window.scene = scene
        bpy.context.view_layer.update()
        depsgraph = bpy.context.evaluated_depsgraph_get()
        source = bpy.data.objects[row['objects'][0]]
        evaluated = source.evaluated_get(depsgraph)
        rest = source.data.attributes[REST].data
        matrix = evaluated.matrix_world.copy()
        offset = np.mean([np.asarray(matrix @ v.co) - np.asarray(rest[v.index].vector)
                          for v in evaluated.data.vertices], axis=0)
        matrix.translation -= Vector(offset)
        mesh = bpy.data.meshes.new_from_object(evaluated, preserve_all_data_layers=True, depsgraph=depsgraph)
        mesh.transform(matrix)
        obj = bpy.data.objects.new(f'SSF_{key}_{revision}', mesh)
        scene.collection.objects.link(obj)
        assert len(mesh.polygons) == row['triangles'] and edge_hash(mesh) == row['edge_attributes_sha256']
        for slot, role in zip(obj.material_slots, row['roles'], strict=True):
            tag = f'SSF_{revision}_{role}'
            slot.link = 'DATA'
            slot.material = bpy.data.materials.get(tag) or bpy.data.materials.new(tag)
        # UV channels carry the three edge vectors and the original corner normal.
        # They are interpolated as geometric data and never used to sample an image.
        for layer in list(mesh.uv_layers):
            mesh.uv_layers.remove(layer)
        channels = []
        for attr, field in [(mesh.attributes[name].data, 'vector') for name in EDGE_ATTRIBUTES] + [(mesh.corner_normals, 'vector')]:
            values = np.empty(len(mesh.loops) * 3, dtype=np.float32)
            attr.foreach_get(field, values)
            channels.append(values.reshape(-1, 3))
        packed = np.concatenate(channels, axis=1)
        assert packed.shape == (len(mesh.loops), 12) and np.isfinite(packed).all()
        for index in range(6):
            layer = mesh.uv_layers.new(name=f'SSFEdgeNormal{index:02d}', do_init=False)
            layer.uv.foreach_set('vector', packed[:, 2 * index:2 * index + 2].ravel())
        mesh.uv_layers[0].active_render = True
        for item in bpy.context.selected_objects:
            item.select_set(False)
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        name = f'SM_{key}_{revision}.fbx'
        file = folder / name
        result = bpy.ops.export_scene.fbx(filepath=str(file), use_selection=True, object_types={'MESH'},
                                          axis_forward='-Y', axis_up='Z', apply_unit_scale=True,
                                          apply_scale_options='FBX_SCALE_UNITS', bake_anim=False, add_leaf_bones=False)
        assert result == {'FINISHED'} and file.stat().st_size > 2000
        row['preview_fbx'] = name
        row['preview_fbx_sha256'] = hashlib.sha256(file.read_bytes()).hexdigest()
        coords = np.array([v.co for v in mesh.vertices])
        row['preview_bounds_m'] = [coords.min(axis=0).tolist(), coords.max(axis=0).tolist()]
        # UE's legacy FBX importer flips V. Hash each triangle's unordered set
        # of UV corners and then all triangles so reordering has no effect.
        uvs = packed.copy()
        uvs[:, 1::2] = np.float32(1.) - uvs[:, 1::2]
        uvs[uvs == 0.] = 0.
        fingerprints = defaultdict(list)
        for polygon in mesh.polygons:
            role = row['roles'][polygon.material_index]
            fingerprints[role].append(b''.join(sorted(uvs[i].astype('<f4').tobytes() for i in polygon.loop_indices)))
        row['preview_uv_sha256'] = {role: hashlib.sha256(b''.join(sorted(entries))).hexdigest()
                                    for role, entries in fingerprints.items()}
        bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.meshes.remove(mesh)


def main(folder):
    assert Path(bpy.data.filepath) == SOURCE
    baseline = read_json(ROOT / 'Production_FeatureConcepts_B6' / 'B6_Material_Manifest.json')['models']
    ink = read_json(SOURCE.parent / 'B13_Edge_Ink_Manifest.json')['models']
    assert len(baseline) == 6 and all(scene[STYLE_PROPERTY] == 1. for scene in bpy.data.scenes)
    report = dict(source=SOURCE.relative_to(ROOT).as_posix(),
                  source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
                  palette={**FIXED, **{role: {t: material_color(role, t) for t in TEAMS} for role in FACTION}},
                  light=tuple(ART_LIGHT), thresholds=THRESHOLDS, light_bands=LIGHT_BANDS,
                  models={key: paint(key, row, ink[key]) for key, row in baseline.items()})
    for mat in list(bpy.data.materials):
        if mat.users == 0:
            bpy.data.materials.remove(mat)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.context.window.scene = next(scene for scene in bpy.data.scenes if scene.name.startswith('00 | '))
    bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT), compress=True)
    export_preview(report, folder)
    report['blend_sha256'] = hashlib.sha256(OUTPUT.read_bytes()).hexdigest()
    write_json(MANIFEST, report)
    print('Component color totals', {key: (len(row['components']), row['triangles'], row['original_ink_edges'])
                                     for key, row in report['models'].items()}, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('preview_fbx_directory', type=Path)
    main(parser.parse_args(sys.argv[sys.argv.index('--') + 1:]).preview_fbx_directory)
