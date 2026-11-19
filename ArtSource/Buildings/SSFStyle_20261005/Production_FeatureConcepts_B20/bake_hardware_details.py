"""Register original A5 shutter fasteners, blade lips and case latches."""

import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

import bpy
import numpy as np


ROOT = Path(__file__).resolve().parent
B19 = ROOT.parent / 'Production_FeatureConcepts_B19'
DEST = ROOT / 'SSF_A5_HardwareDetails_B20.blend'
sys.dont_write_bytecode = True
sys.path.insert(0, str(B19))
from bake_reference_finishes import (B8, B13, BASELINE, CAMERAS, FUNCTIONAL_UV, LUMA, REFERENCES, REST,
                                     SCALE, STYLE_PROPERTY, image_samples, linear_color, normalized, original_parts,
                                     panel_positions, projected_ownership, range_mask, read_json, reference_projection,
                                     repack_functional, sample, shader_mix, test_views, visible_surface, world_vertices)
from bake_functional_details import shader_math
from build_scene import component_faces


SHUTTERS = (
    ('DoorPart1', ((511, 747), (719, 686), (731, 731), (509, 799)),
     ((520, 755), (518, 784), (715, 698), (723, 724)), (.076, .994), (.02, .99)),
    ('DoorPart2', ((517, 692), (709, 636), (722, 677), (514, 740)),
     ((522, 698), (522, 731), (708, 646), (715, 668)), (.086, .985), (.01, .99)),
    ('DoorPart3', ((523, 638), (705, 581), (714, 625), (520, 683)),
     ((526, 646), (527, 671), (701, 595), (710, 621)), (.107, .98), (.01, .99)),
    ('DoorPart4', ((528, 581), (698, 530), (707, 574), (526, 627)),
     ((531, 592), (529, 620), (694, 540), (702, 569)), (.125, .982), (.015, .63)),
)
LATCHES = (
    ('0_cube80', 6, 5, (.23, .46), (.29, .70), ((392, 590), (412, 585), (414, 611), (394, 616))),
    ('0_cube60', 5, 6, (.10, .35), (.28, .68), ((531, 605), (549, 615), (548, 639), (530, 629))),
    ('0_cube60', 6, 5, (.185, .36), (.28, .69), ((570, 616), (590, 611), (591, 638), (572, 644))),
    ('0_cube60', 6, 5, (.67, .86), (.28, .69), ((638, 594), (659, 588), (660, 615), (640, 622))),
    ('0_cube81', 6, 5, (.64, .87), (.30, .75), ((691, 491), (710, 484), (710, 512), (693, 522))),
)
FIXTURES = {
    'MilitaryFactory': tuple((name, 'Sand painted steel') for name, *_ in SHUTTERS),
    'Reactor': (('Block2', 'Gray-green cooling blade'),),
    'StrategyCenter': tuple((name, 'Faction') for name in ('0_cube60', '0_cube80', '0_cube81')),
}


def write_samples(target, rows, values):
    x, y = np.floor(rows[:, :2] * SCALE).astype(int).T
    target[y, x] = np.maximum(target[y, x], values)


def fastener_mask(source, pos, center):
    around = np.array([[-6, -4], [-6, 0], [-6, 4], [6, -4], [6, 0], [6, 4]])
    ring = np.array(center) + around
    base = max(.50, np.quantile(sample(source, *ring.T) @ LUMA, .75))
    other_base = max(.48, np.quantile(sample(source, ring[:, 0] + 886, ring[:, 1]) @ LUMA, .75))
    shifts = sorted((np.hypot(dx, dy), dx, dy) for dx in range(-8, 9) for dy in range(-8, 9))
    for distance, dx, dy in shifts:
        if distance > 8:
            break
        query = pos + (dx, dy)
        radius = np.linalg.norm(query - center, axis=1)
        active = radius < 5.7
        footprint = np.clip((5.7 - radius[active]) / 1.8, 0, 1)
        x, y = query[active].T
        luma = sample(source, x, y) @ LUMA
        red = [sample(source, x + 886 + u, y + v) @ LUMA for u, v in ((0, 0), (-1.5, 0), (1.5, 0), (0, -1.5), (0, 1.5))]
        shade = normalized(base - luma, .060, .31) * normalized(other_base - np.minimum.reduce(red), .035, .21) * footprint
        count = int(np.count_nonzero(shade > .20))
        if count >= 8:
            gleam = normalized(luma - base, .025, .125) * normalized(red[0] - other_base, .01, .10) * footprint
            return active, shade, gleam, count, [dx, dy]
    raise ValueError(f'A5 fastener {center} cannot be placed inside its panel')


def shutter_fasteners(source, rows, baked, context):
    result = []
    for part, quad, centers, u_range, w_range in SHUTTERS:
        panel = rows[part]
        u, v, w = panel[:, 5:].T
        panel = panel[(v < .2) & range_mask(u, *u_range) & range_mask(w, *w_range)]
        pos = panel_positions(panel, quad, (5, 7), (u_range, w_range))
        values = np.zeros((len(panel), 4), dtype=np.float32)
        marks = []
        for x, y in centers:
            active, shade, gleam, count, shift = fastener_mask(source, pos, (x, y))
            values[active, 0] = np.maximum(values[active, 0], shade)
            values[active, 1] = np.maximum(values[active, 1], gleam)
            marks.append({'source_px': [x, y], 'samples': count, 'inset_sampling_shift_px': shift})
        write_samples(baked, panel, values)
        result.append({'part': part, 'source_quad_px': quad, 'fasteners': marks})
    return {'feature': '四段卷门各四处紧固件', 'shutters': result}


def radiator_lips(source, rows, baked, context):
    mesh, faces, parts, rest, owner, guides = context
    indices = component_faces(mesh, sorted(faces), parts, rest)
    assert len(indices) == 3
    front = rows['Block2']
    x, y = np.floor(front[:, :2] * SCALE).astype(int).T
    part_owners = owner[y, x]
    blades = [front[np.isin(part_owners, island)] for island in indices]
    assert all(len(blade) for blade in blades)
    blades.sort(key=lambda blade: -np.mean(blade[:, 4]))
    result = []
    for i, (blade, stroke) in enumerate(zip(blades, guides[2]['strokes'][1:4], strict=True), 1):
        u, depth, w = blade[:, 5:].T
        blade = blade[(depth > .71) & range_mask(w, .18, .78) & range_mask(u, .12, .90)]
        line = np.asarray(stroke['xy'])
        x = line[0, 0] + normalized(blade[:, 5], .15, .88) * (line[-1, 0] - line[0, 0])
        y = np.interp(x, line[:, 0], line[:, 1]) + 8 - normalized(blade[:, 7], .18, .78) * 14
        left = sample(source, x, y)
        right = sample(source, x + 864, y)

        def cyan(color):
            green_blue = color[:, 1:].min(axis=1)
            return normalized(green_blue - color[:, 0], .085, .21) * normalized(green_blue, .30, .50)

        teal = cyan(left) * normalized(cyan(right), .03, .45)
        pale = normalized((left @ LUMA + right @ LUMA) * .5, .53, .71) * normalized(left[:, 0], .40, .63)
        a = np.ones(len(blade))
        write_samples(baked, blade, np.column_stack((teal, pale, np.zeros(len(blade)), a)))
        count = int(np.count_nonzero(teal > .20))
        assert count >= 18, (i, count)
        result.append({'blade_top_to_bottom': i, 'teal_samples': count, 'covered_samples': len(blade),
                       'source_stroke_px': line.tolist()})
    return {'feature': '三片原散热片的青色前缘', 'part': 'Block2', 'blades': result,
            'reference_blade_rows': 4, 'physical_blade_rows': 3}


def case_latches(source, rows, baked, context):
    result = []
    for part, plane, axis, horizontal, vertical, quad in LATCHES:
        panel = rows[part]
        target = panel[(panel[:, plane] < .121) & range_mask(panel[:, axis], *horizontal) &
                       range_mask(panel[:, 7], *vertical)]
        pos = panel_positions(target, quad, (axis, 7), (horizontal, vertical))
        left = sample(source, *pos.T)
        right = sample(source, pos[:, 0] + 779, pos[:, 1])
        gray = (1 - normalized(np.ptp(left, axis=1), .085, .16)) * (1 - normalized(np.ptp(right, axis=1), .09, .17))
        luma = (left @ LUMA + right @ LUMA) * .5
        graphite = gray * (1 - normalized(luma, .38, .57))
        polish = gray * normalized(luma, .35, .64)
        write_samples(baked, target, np.column_stack((graphite, polish, np.zeros((len(target), 2)))))
        count = int(np.count_nonzero(graphite > .20))
        assert count >= 16, (part, quad, count)
        result.append({'part': part, 'source_quad_px': quad, 'painted_samples': count, 'host_samples': len(target)})
    return {'feature': '三座可见机箱上的五处锁扣', 'latches': result}


def connect(material, image, key):
    tree = material.node_tree
    detail = next(node for node in tree.nodes if node.label == 'A5 reference detail before lighting')
    origin = (detail.location.x - 1060, detail.location.y - 420)
    uv = tree.nodes.new('ShaderNodeUVMap')
    uv.uv_map = FUNCTIONAL_UV
    uv.location = origin
    texture = tree.nodes.new('ShaderNodeTexImage')
    texture.image = image
    texture.extension = 'CLIP'
    texture.label = 'A5 fixed hardware on original LOD0 part'
    texture.location = (origin[0] + 180, origin[1])
    separate = tree.nodes.new('ShaderNodeSeparateColor')
    separate.location = (origin[0] + 465, origin[1])
    tree.links.new(uv.outputs[0], texture.inputs['Vector'])
    tree.links.new(texture.outputs['Color'], separate.inputs['Color'])
    dark = separate.outputs['Red']
    bright = separate.outputs['Green']
    base = detail.inputs['Base Color'].links[0].from_socket
    new_math = []
    if key == 'Reactor':
        lip = next(node for node in tree.nodes if node.label == 'Painted heat exchanger lip')
        previous = lip.inputs[0].links[0].from_socket
        remain = shader_math(tree, 'SUBTRACT', 1., texture.outputs['Alpha'])
        background = shader_math(tree, 'MULTIPLY', previous, remain)
        mask = shader_math(tree, 'MAXIMUM', background, dark)
        tree.links.new(mask, lip.inputs[0])
        polished = shader_math(tree, 'MULTIPLY', bright, .48)
        new_math.extend([remain.node, background.node, mask.node, polished.node])
    else:
        dark_factor = shader_math(tree, 'MULTIPLY', dark, .90 if key == 'StrategyCenter' else .72)
        shade = shader_mix(tree, 'A5 graphite fixed hardware', dark_factor, base, linear_color('#2C3735'))
        shade.node.location = (detail.location.x - 400, origin[1] - 230)
        base = shade
        polished = shader_math(tree, 'MULTIPLY', bright, .48 if key == 'StrategyCenter' else .30)
        new_math.extend([dark_factor.node, polished.node])
    for i, node in enumerate(new_math):
        node.location = (origin[0] + 670, origin[1] - i * 120)
    finish = shader_mix(tree, 'A5 fixed metal glints', polished, base, linear_color('#FEE4D9'))
    finish.node.location = (detail.location.x - 190, origin[1] - 140)
    tree.links.new(finish, detail.inputs['Base Color'])


def build():
    assert Path(bpy.data.filepath) == B19 / 'SSF_A5_ReferenceFinishes_B19.blend'
    source = read_json(BASELINE / 'B6_Material_Manifest.json')['models']
    edges = read_json(B13 / 'B13_Edge_Ink_Manifest.json')['models']
    cameras = read_json(CAMERAS)['paired_cameras']
    zones = read_json(B8 / 'reference_strokes.json')
    references = {row['key']: row for row in read_json(REFERENCES / 'Reference_A5_Manifest.json')['deliverables']}
    overview = next(scene for scene in bpy.data.scenes if scene.name.startswith('00 | '))
    bpy.context.window.scene = overview
    bpy.context.view_layer.update()
    deps = bpy.context.evaluated_depsgraph_get()
    view_matrix = np.asarray(overview.camera.rotation_euler.to_matrix(), dtype=float)
    overview_pixels = overview.render.resolution_x / overview.camera.data.ortho_scale
    textures = ROOT / 'Textures'
    textures.mkdir(exist_ok=True)
    (ROOT / 'Previews').mkdir(exist_ok=True)
    for image in bpy.data.images:
        if image.filepath_raw.startswith('//Textures/'):
            image.filepath_raw = '//../Production_FeatureConcepts_B19/' + image.filepath_raw[2:]
    reports = {}
    for key, paint in (('MilitaryFactory', shutter_fasteners), ('Reactor', radiator_lips), ('StrategyCenter', case_latches)):
        blue, red = (bpy.data.objects[name] for name in source[key]['objects'])
        mesh = blue.data
        assert red.data == mesh
        obj = next(obj for obj in overview.objects if obj.type == 'MESH' and obj.data == mesh)
        overview_xy = ((world_vertices(obj, deps) - np.asarray(overview.camera.location)) @ view_matrix)[:, :2]
        rest = [item.vector.copy() for item in mesh.attributes[REST].data]
        raw = np.asarray(rest, dtype=np.float64)
        projected = reference_projection(rest, edges[key]['reference_camera'])
        views = test_views(key, raw, overview_xy, overview_pixels, cameras)
        parts = original_parts(blue)
        face_parts = {face.index: Counter(parts[i] for i in face.vertices).most_common(1)[0][0] for face in mesh.polygons}
        classes = [0 if item.value == 0 else 2 for item in mesh.attributes['SSF Original Material Index'].data]
        owner = projected_ownership(projected, mesh.polygons, classes)
        fixtures = dict(FIXTURES[key])
        host = {face.index for face in mesh.polygons if classes[face.index] == 0 and face_parts[face.index] in fixtures
                and blue.material_slots[face.material_index].material.get('Finish', '').startswith(fixtures[face_parts[face.index]])}
        rows = {part: visible_surface(blue, mesh, projected, raw, owner, face_parts, part, finish)
                for part, finish in fixtures.items()}
        reference = REFERENCES / references[key]['file']
        assert hashlib.sha256(reference.read_bytes()).hexdigest() == references[key]['sha256']
        baked = np.zeros((*owner.shape, 4), dtype=np.float32)
        report = paint(image_samples(reference), rows, baked, (mesh, host, face_parts, rest, owner, zones[key]))
        image, atlas_report = repack_functional(blue, red, key, projected[:, :2] * SCALE, views, baked, owner, host, textures)
        slots = {mesh.polygons[index].material_index for index in atlas_report['new_faces']}
        materials = {obj.material_slots[index].material for obj in (blue, red) for index in slots}
        for material in materials:
            connect(material, image, key)
        report.update(atlas_report, source_sha256=references[key]['sha256'], hosts=fixtures,
                      new_materials=sorted(material.name for material in materials))
        reports[key] = report
        print(key, report['feature'], report['new_charts'], flush=True)
    for scene in bpy.data.scenes:
        key = next((key for key, row in source.items() if row['scene'] == scene.name), 'SSF_Overview')
        scene.render.filepath = f'//Previews/{key}_B20.png'
        assert scene[STYLE_PROPERTY] == 1.
    bpy.context.window.scene = bpy.data.scenes[source['MilitaryFactory']['scene']]
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(DEST), compress=True, relative_remap=False)
    manifest = {'base': 'B19', 'reference': 'A5', 'models': reports, 'visual_review': 'Pending user review in Blender'}
    (ROOT / 'B20_Hardware_Details_Manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n',
                                                           encoding='utf-8')


if __name__ == '__main__':
    build()
