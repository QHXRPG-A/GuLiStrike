"""Register the fixed A5 colors and window contents to the B14 functional surfaces."""

import hashlib
import json
import math
import sys
from collections import Counter
from pathlib import Path

import bpy
import numpy as np


ROOT = Path(__file__).resolve().parent
B14 = ROOT.parent / 'Production_FeatureConcepts_B14'
DEST = ROOT / 'SSF_A5_FunctionalDetail_B15.blend'
UV = 'SSF A5 Functional Detail UV'
ATLAS_WIDTH = 1024
PADDING = 8
sys.dont_write_bytecode = True
sys.path.insert(0, str(B14))
from bake_surface_detail import (B13, BASELINE, REFERENCES, REST, SCALE, STYLE_PROPERTY, extrema,
                                 linear_color, original_parts, projected_ownership, read_json,
                                 reference_projection, sample)


# The four source windows with legible contents. Each quad follows the glass in
# the A5 painting, in the order near-left, near-right, far-right, far-left.
PANES = {
    'Capsule1': (((503, 196), (548, 196), (557, 215), (499, 215)),
                 ((500, 230), (566, 232), (559, 267), (493, 269))),
    'Capsule2': (((500, 309), (548, 309), (557, 327), (499, 328)),
                 ((502, 342), (565, 345), (565, 376), (499, 380))),
    'Capsule4': (((285, 293), (319, 253), (350, 258), (317, 299)),
                 ((318, 314), (356, 270), (363, 299), (327, 348))),
    'Capsule5': (((285, 404), (319, 364), (352, 368), (318, 413)),
                 ((319, 426), (360, 385), (364, 413), (327, 459))),
}
FACTION_SEPARATION = {'CloningCenter': 754, 'CommandCenter': 754, 'MilitaryFactory': 886,
                      'Reactor': 864, 'StrategyCenter': 779}


def image_samples(path):
    image = bpy.data.images.load(str(path), check_existing=False)
    image.colorspace_settings.name = 'Non-Color'
    w, h = image.size
    values = np.empty(w * h * 4, dtype=np.float32)
    image.pixels.foreach_get(values)
    bpy.data.images.remove(image)
    return np.ascontiguousarray(values.reshape(h, w, 4)[::-1, :, :3])


def range_mask(x, lo, hi):
    return (x >= lo) & (x <= hi)


def visible_surface(obj, mesh, points, rest, owner, face_parts, part, finish):
    panel = np.asarray([value.vector.copy() for value in mesh.attributes['SSF Panel Coordinates'].data])
    rows = []
    for face in mesh.polygons:
        if face_parts[face.index] != part or not obj.material_slots[face.material_index].material.get('Finish', '').startswith(finish):
            continue
        ids = list(face.vertices)
        xy = points[ids, :2] * SCALE
        x0, y0 = np.maximum(np.floor(xy.min(axis=0)).astype(int), 0)
        x1, y1 = np.minimum(np.ceil(xy.max(axis=0)).astype(int) + 1, owner.shape[::-1])
        yy, xx = np.nonzero(owner[y0:y1, x0:x1] == face.index)
        if not len(xx):
            continue
        target = np.column_stack((xx + x0, yy + y0)) + .5
        weights = (target - xy[0]) @ np.linalg.inv(xy[1:] - xy[0])
        weights = np.column_stack((1 - weights.sum(axis=1), weights))
        rows.append(np.column_stack((target / SCALE, weights @ rest[ids], weights @ panel[ids])))
    assert rows, (obj.name, part, finish)
    return np.concatenate(rows)


def normalized(value, lo, hi):
    assert lo < hi
    return np.clip((value - lo) / (hi - lo), 0, 1)


def quad_positions(quad, a, b):
    p, q, r, s = np.asarray(quad)
    a, b = a[:, None], b[:, None]
    return (p * (1 - a) + q * a) * (1 - b) + (s * (1 - a) + r * a) * b


def panel_positions(rows, quad, dims, limits):
    """Match the source directions to the projected local axes of the existing part."""
    parameters = rows[:, dims]
    direction = np.linalg.lstsq(np.column_stack((parameters, np.ones(len(rows)))), rows[:, :2], rcond=None)[0][:2]
    p, q, r, s = np.asarray(quad)
    axes = np.array([q + r - p - s, s + r - p - q])
    scaled = [normalized(value, lo, hi) for value, (lo, hi) in zip(parameters.T, limits, strict=True)]
    for i in range(2):
        if direction[i] @ axes[i] < 0:
            scaled[i] = 1 - scaled[i]
    return quad_positions(quad, *scaled)


def color_signal(pixels, pos, kind):
    r, g, b = sample(pixels, *pos.T).T
    if kind == 'mint':
        return normalized(g, .47, .60) * normalized(g - r, .105, .19) * normalized(g - b, .015, .06)
    if kind == 'teal':
        return normalized(g - r, .05, .14) * normalized(g - b, -.045, .012)
    return normalized(r - g, .095, .185) * normalized(g - b, .12, .23) * normalized(r, .52, .70)


def place(key, pixels, baked, rows, pos, feature, kind, channel, reports):
    xx, yy = np.floor(rows[:, :2] * SCALE).astype(int).T
    signal = color_signal(pixels, pos, kind)
    # Require corresponding fixed chroma on the other team. A two-pixel support
    # window tolerates the small painted differences without softening the source.
    red_pos = pos + (FACTION_SEPARATION[key], 0)
    witness = np.maximum.reduce([color_signal(pixels, red_pos + (dx, dy), kind)
                                 for dx, dy in ((0, 0), (-2, 0), (2, 0), (0, -2), (0, 2))])
    signal *= normalized(witness, .035, .35)
    assert np.count_nonzero(signal > .2) > 20, (key, feature)
    baked[yy, xx, channel] = np.maximum(baked[yy, xx, channel], signal)
    baked[yy, xx, 3] = 1.
    reports.append({'feature': feature, 'host_samples': len(rows), 'color_samples': int(np.count_nonzero(signal > .2)),
                    'source_bounds_px': np.round([pos.min(axis=0), pos.max(axis=0)], 1).tolist(),
                    'max_strength': round(float(signal.max()), 3)})


def windows(pixels, baked, surface, reports):
    for part, quads in PANES.items():
        rows = surface(part, 'Cultivation viewport')
        u, v, w = rows[:, 5:].T
        band = range_mask(u, .19, .81)
        top = (w > .77) & band
        side = (abs(v - .5) > .33) & range_mask(w, .31, .79) & band & ~top
        for name, mask, other, quad in zip(('top', 'side'), (top, side), (6, 7), quads, strict=True):
            pane = rows[mask]
            lo, hi = np.quantile(pane[:, other], [.015, .985])
            pos = panel_positions(pane, quad, (5, other), ((.19, .81), (lo, hi)))
            r, g, b = sample(pixels, *pos.T).T
            rr, gg, bb = sample(pixels, pos[:, 0] + FACTION_SEPARATION['CloningCenter'], pos[:, 1]).T
            valid = normalized(g - r, .01, .10) * normalized(gg - rr, -.005, .075)
            brightness = .2126 * r + .7152 * g + .0722 * b
            other_brightness = .2126 * rr + .7152 * gg + .0722 * bb
            glow = normalized(brightness, .30, .68)
            figure = normalized(brightness, .57, .91) * normalized(other_brightness, .38, .65)
            xx, yy = np.floor(pane[:, :2] * SCALE).astype(int).T
            baked[yy, xx, 0] = glow
            baked[yy, xx, 1] = figure
            baked[yy, xx, 3] = valid
            assert np.count_nonzero(valid > .5) > 100, (part, name)
            reports.append({'feature': f'{part} {name} contents', 'host_samples': len(pane),
                            'color_samples': int(np.count_nonzero(valid > .5)),
                            'source_quad_px': quad, 'max_figure_strength': round(float(figure.max()), 3)})


def status_ring(pixels, baked, surface, reports):
    rows = surface('Ring2', 'Command status strip')
    rows = rows[range_mask(rows[:, 0], 416., 484.5) & range_mask(rows[:, 4], 5.87, 6.25)]
    x = 435 + normalized(rows[:, 0], 416., 484.5) * 132
    trace = np.interp(x, [435, 472, 491, 511, 529, 545, 567], [462, 462, 459, 455, 448, 442, 432])
    y = trace + (6.112 - rows[:, 4]) * 55
    place('CommandCenter', pixels, baked, rows, np.column_stack((x, y)), 'Ring2 A5 status bars', 'mint', 0, reports)


def jambs(pixels, baked, surface, reports):
    rows = surface('MainBlock', 'Loading jamb')
    for direction, quad in ((-1, ((463, 697), (507, 713), (509, 798), (483, 800))),
                            (1, ((735, 640), (768, 620), (758, 728), (731, 728)))):
        x, y, z = rows[:, 2:5].T
        guard = rows[(direction * x > 0) & range_mask(abs(x), 2.04, 2.62) & range_mask(z, .28, 1.63)]
        pos = panel_positions(guard, quad, (2, 4), ((direction * 2.33 - .29, direction * 2.33 + .29), (.28, 1.63)))
        place('MilitaryFactory', pixels, baked, guard, pos, f'Jamb amber guard {direction:+}', 'amber', 2, reports)


def reactor_lock(pixels, baked, surface, reports):
    rows = surface('Rod', 'Graphite lock')
    # The actual LOD0 exposes the compact grip. The extended mechanism in A5 is
    # outside this part, so only the exposed orange grip supplies color.
    a = normalized(rows[:, 0], 418.75, 446.75)
    b = normalized(rows[:, 1] - 0.34 * (rows[:, 0] - 418.75), 210.9, 224.)
    pos = quad_positions(((419, 132), (464, 145), (454, 180), (390, 161)), a, b)
    place('Reactor', pixels, baked, rows, pos, 'Rod lock grip', 'amber', 2, reports)


def pedestal(pixels, baked, surface, reports):
    rows = surface('0_polymsh7', 'Gray-green pedestal')
    x, y, z = rows[:, 2:5].T
    radius, angle = np.hypot(x, y), np.arctan2(x, -y)
    param = np.column_stack((rows, angle, radius))
    for low, high, quad in ((-.40, -.045, ((535, 503), (574, 503), (576, 521), (532, 520))),
                            (.045, .40, ((572, 506), (598, 486), (608, 495), (579, 519)))):
        arc = param[range_mask(angle, low, high) & range_mask(radius, 2.045, 2.30) & range_mask(z, 3.81, 4.3)]
        pos = panel_positions(arc, quad, (8, 9), ((low, high), (2.045, 2.30)))
        place('StrategyCenter', pixels, baked, arc, pos, f'Pedestal datum arc {low:+.3f}', 'teal', 0, reports)
    nose = param[range_mask(angle, -.16, .16) & range_mask(radius, 2.32, 2.92) & (z > 3.66)]
    pos = panel_positions(nose, ((566, 539), (602, 527), (624, 561), (584, 576)), (8, 9), ((-.16, .16), (2.32, 2.92)))
    place('StrategyCenter', pixels, baked, nose, pos, 'Pedestal amber index plate', 'amber', 2, reports)


def atlas(mesh, points, owner, baked):
    selected = sorted(set(owner[baked[..., 3] > 2 / 255]))
    assert selected and selected[0] >= 0
    tiles = []
    for i in selected:
        xy = points[list(mesh.polygons[i].vertices), :2] * SCALE
        lo = np.floor(xy.min(axis=0)).astype(int) - 1
        hi = np.ceil(xy.max(axis=0)).astype(int) + 1
        x0, y0 = lo
        x1, y1 = hi
        assert x0 >= 0 and y0 >= 0 and x1 <= owner.shape[1] and y1 <= owner.shape[0]
        footprint = extrema(owner[y0:y1, x0:x1] == i, 1, np.maximum)
        tiles.append((i, xy - lo, np.where(footprint[..., None], baked[y0:y1, x0:x1], 0)))
    x, y, row_h = PADDING * 3, PADDING, 0
    placed = []
    for i, xy, tile in sorted(tiles, key=lambda row: -row[2].shape[0]):
        h, w = tile.shape[:2]
        if x + w + PADDING > ATLAS_WIDTH:
            x, y, row_h = PADDING, y + row_h + PADDING * 2, 0
        assert x + w + PADDING <= ATLAS_WIDTH
        placed.append((i, xy, tile, x, y))
        x += w + PADDING * 2
        row_h = max(row_h, h)
    height = 2 ** math.ceil(math.log2(y + row_h + PADDING))
    texels = np.zeros((height, ATLAS_WIDTH, 4), dtype=np.float32)
    uv = np.full((len(mesh.loops), 2), (.5 / ATLAS_WIDTH, 1 - .5 / height), dtype=np.float32)
    for i, xy, tile, x, y in placed:
        h, w = tile.shape[:2]
        texels[y-PADDING:y+h+PADDING, x-PADDING:x+w+PADDING] = np.pad(
            tile, ((PADDING, PADDING), (PADDING, PADDING), (0, 0)), mode='edge')
        coordinates = (xy + (x, y)) / (ATLAS_WIDTH, height)
        coordinates[:, 1] = 1 - coordinates[:, 1]
        uv[list(mesh.polygons[i].loop_indices)] = coordinates
    active = mesh.uv_layers.active_index
    assert mesh.uv_layers.get(UV) is None
    mesh.uv_layers.new(name=UV, do_init=False).uv.foreach_set('vector', uv.ravel())
    mesh.uv_layers.active_index = active
    return texels, len(selected)


def shader_math(tree, operation, left, right):
    node = tree.nodes.new('ShaderNodeMath')
    node.operation = operation
    for socket, value in zip(node.inputs, (left, right)):
        if isinstance(value, bpy.types.NodeSocket):
            tree.links.new(value, socket)
        else:
            socket.default_value = value
    return node.outputs[0]


def shader_mix(tree, label, weight, base, color):
    node = tree.nodes.new('ShaderNodeMixRGB')
    node.label = label
    for socket, value in zip(node.inputs, (weight, base, color), strict=True):
        if isinstance(value, bpy.types.NodeSocket):
            tree.links.new(value, socket)
        else:
            socket.default_value = value
    return node.outputs[0]


def connect(material, key, image):
    tree = material.node_tree
    detail = next(node for node in tree.nodes if node.label == 'A5 reference detail before lighting')
    uv = tree.nodes.new('ShaderNodeUVMap')
    uv.uv_map = UV
    tex = tree.nodes.new('ShaderNodeTexImage')
    tex.label = 'A5 functional details registered to the LOD0 part'
    tex.image = image
    tex.extension = 'CLIP'
    tree.links.new(uv.outputs[0], tex.inputs['Vector'])
    separate = tree.nodes.new('ShaderNodeSeparateColor')
    tree.links.new(tex.outputs['Color'], separate.inputs['Color'])
    red, green, blue = (separate.outputs[name] for name in ('Red', 'Green', 'Blue'))
    coverage = tex.outputs['Alpha']
    labels = {'CommandCenter': ('Independent status slots', red),
              'MilitaryFactory': ('Fixed amber jamb guards', blue),
              'Reactor': ('Amber grip', blue),
              'StrategyCenter': ('Two painted teal datum arcs', red)}
    if key == 'CloningCenter':
        tint = shader_mix(tree, 'A5 glass luminance', red, linear_color('#032E30'), linear_color('#0D9099'))
        contents = shader_mix(tree, 'A5 illuminated cultivation contents', green, tint, linear_color('#FEE4D9'))
        base = detail.inputs['Base Color'].links[0].from_socket
        finish = shader_mix(tree, 'A5 contents within existing glass', coverage, base, contents)
        tree.links.new(finish, detail.inputs['Base Color'])
    else:
        label, mask = labels[key]
        existing = next(node for node in tree.nodes if node.label == label)
        original = existing.inputs[0].links[0].from_socket
        destinations = [link.to_socket for link in original.links]
        remainder = shader_math(tree, 'MULTIPLY', original, shader_math(tree, 'SUBTRACT', 1., coverage))
        factor = shader_math(tree, 'MAXIMUM', remainder, mask)
        for socket in destinations:
            tree.links.new(factor, socket)
        if key == 'StrategyCenter':
            finish = shader_mix(tree, 'A5 amber index plate', blue, existing.outputs[0], linear_color('#EE9D58'))
            tree.links.new(finish, detail.inputs['Base Color'])
    new_nodes = [node for node in tree.nodes if node.location.length == 0 and node not in (uv, tex, separate)]
    uv.location = (detail.location.x - 1050, detail.location.y - 440)
    tex.location = (uv.location.x + 190, uv.location.y)
    separate.location = (tex.location.x + 290, tex.location.y)
    for i, node in enumerate(new_nodes):
        node.location = (separate.location.x + 180 + (i % 3) * 175, separate.location.y - (i // 3) * 155)


def build():
    assert Path(bpy.data.filepath).parent == B14
    base = read_json(BASELINE / 'B6_Material_Manifest.json')['models']
    edges = read_json(B13 / 'B13_Edge_Ink_Manifest.json')['models']
    refs = {row['key']: row for row in read_json(REFERENCES / 'Reference_A5_Manifest.json')['deliverables']}
    methods = {'CloningCenter': ('Cultivation viewport', windows), 'CommandCenter': ('Command status strip', status_ring),
               'MilitaryFactory': ('Loading jamb', jambs), 'Reactor': ('Graphite lock', reactor_lock),
               'StrategyCenter': ('Gray-green pedestal', pedestal)}
    reports = {}
    (ROOT / 'Textures').mkdir(exist_ok=True)
    (ROOT / 'Previews').mkdir(exist_ok=True)
    for key, (finish, paint) in methods.items():
        blue, red = (bpy.data.objects[name] for name in base[key]['objects'])
        mesh = blue.data
        assert red.data == mesh
        vertex_parts = original_parts(blue)
        parts = [Counter(vertex_parts[i] for i in face.vertices).most_common(1)[0][0] for face in mesh.polygons]
        position = [row.vector.copy() for row in mesh.attributes[REST].data]
        points = reference_projection(position, edges[key]['reference_camera'])
        classes = [0 if row.value == 0 else 2 for row in mesh.attributes['SSF Original Material Index'].data]
        owner = projected_ownership(points, mesh.polygons, classes)
        rest = np.asarray(position)

        def surface(part, finish_prefix):
            return visible_surface(blue, mesh, points, rest, owner, parts, part, finish_prefix)

        ref = REFERENCES / refs[key]['file']
        assert hashlib.sha256(ref.read_bytes()).hexdigest() == refs[key]['sha256']
        pixels = image_samples(ref)
        baked = np.zeros((*owner.shape, 4), dtype=np.float32)
        features = []
        paint(pixels, baked, surface, features)
        assert not baked[owner < 0].any()
        texels, face_count = atlas(mesh, points, owner, baked)
        path = ROOT / 'Textures' / f'{key}_A5_Functional.png'
        height, width = texels.shape[:2]
        image = bpy.data.images.new(path.stem, width, height, alpha=True, is_data=True)
        image.alpha_mode = 'CHANNEL_PACKED'
        image.colorspace_settings.name = 'Non-Color'
        image.pixels.foreach_set(np.ascontiguousarray(texels[::-1]).ravel())
        image.update()
        image.file_format = 'PNG'
        image.filepath_raw = str(path)
        image.save()
        image.pack()
        image.filepath_raw = f'//Textures/{path.name}'
        assert image.packed_file.data == path.read_bytes()
        materials = {slot.material for obj in (blue, red) for slot in obj.material_slots
                     if slot.material.get('Finish', '').startswith(finish)}
        assert materials
        for material in materials:
            connect(material, key, image)
        reports[key] = {'source_sha256': refs[key]['sha256'], 'size': list(image.size), 'atlas': path.name,
                        'atlas_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                        'faces': face_count, 'materials': sorted(material.name for material in materials), 'features': features}
        print(key, face_count, [(feature['feature'], feature['color_samples']) for feature in features], flush=True)
    for scene in bpy.data.scenes:
        key = next((key for key, row in base.items() if row['scene'] == scene.name), 'SSF_Overview')
        scene.render.filepath = f'//Previews/{key}_B15.png'
        assert scene[STYLE_PROPERTY] == 1.
    for image in bpy.data.images:
        if image.filepath_raw.startswith('//Textures/') and image.name.endswith('_SurfaceDetail'):
            image.filepath_raw = '//../Production_FeatureConcepts_B14/' + image.filepath_raw[2:]
    bpy.context.window.scene = bpy.data.scenes[base['CloningCenter']['scene']]
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(DEST), compress=True, relative_remap=False)
    manifest = {'base': 'B14', 'models': reports, 'air_base': 'Existing A5 amber tabs remain on actual LOD0 fixtures.',
                'visual_review': 'Pending user review in Blender'}
    (ROOT / 'B15_Functional_Detail_Manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n',
                                                           encoding='utf-8')


if __name__ == '__main__':
    build()
