"""Bake A5 reference details onto the visible, registered LOD0 surfaces in B13."""

import hashlib
import json
import math
import sys
from collections import Counter
from fnmatch import fnmatchcase
from pathlib import Path

import bpy
import numpy as np


ROOT = Path(__file__).resolve().parent
B13 = ROOT.parent / 'Production_FeatureConcepts_B13'
B8 = ROOT.parent / 'Production_FeatureConcepts_B8'
DEST = ROOT / 'SSF_A5_SurfaceDetail_B14.blend'
UV_NAME = 'SSF A5 Surface Detail UV'
ATLAS_WIDTH = 2048
GUTTER = 8

sys.dont_write_bytecode = True
sys.path.insert(0, str(B8))
from build_reference_ink import (BASELINE, REFERENCES, REST, SCALE, STYLE_PROPERTY,
                                 geometry_digest, linear_color, original_parts, paint_segment,
                                 projected_ownership, read_json, reference_projection, register_zone)


# These reference features describe openings or additional geometry. Keep the
# LOD0 silhouette and prevent their reference shading from printing onto the face.
UNSUPPORTED = {
    ('AirBase', '穹顶十字冠板与中间开口', 3),
    ('StrategyCenter', '阵列立柱的两段交叉撑', 3),
}


def extrema(values, radius, operation):
    """Separable square min or max filter, with replicated edge samples."""
    for axis in (0, 1):
        rows = np.swapaxes(values, 0, axis)
        length = len(rows)
        padded = np.pad(rows, ((radius, radius), (0, 0)), mode='edge')
        result = padded[:length].copy()
        for offset in range(1, 2 * radius + 1):
            operation(result, padded[offset:offset + length], out=result)
        values = np.swapaxes(result, 0, axis)
    return values


def reference_detail(path):
    """Isolate small depressions and highlights without copying the broad light bands."""
    image = bpy.data.images.load(str(path), check_existing=False)
    image.colorspace_settings.name = 'Non-Color'
    width, height = image.size
    pixels = np.empty(width * height * 4, dtype=np.float32)
    image.pixels.foreach_get(pixels)
    bpy.data.images.remove(image)
    rgb = pixels.reshape(height, width, 4)[::-1, :width // 2, :3]
    luma = rgb @ np.array([.2126, .7152, .0722], dtype=np.float32)
    channels = []
    for values in (luma, 1 - luma):
        contrasts = []
        for radius, weight in ((2, 1.), (5, .78)):
            local = extrema(extrema(values, radius, np.maximum), radius, np.minimum)
            contrasts.append(np.clip((local - values - .019) / (.105 + .14 * luma), 0, 1) * weight)
        channels.append(np.maximum(*contrasts))
    return np.stack(channels, axis=-1)


def sample(image, x, y):
    """Bilinear lookup in top-origin source pixel coordinates."""
    h, w = image.shape[:2]
    x = np.clip(x - .5, 0, w - 1.001)
    y = np.clip(y - .5, 0, h - 1.001)
    left, top = x.astype(int), y.astype(int)
    u, v = (x - left)[..., None], (y - top)[..., None]
    upper = image[top, left] * (1 - u) + image[top, left + 1] * u
    lower = image[top + 1, left] * (1 - u) + image[top + 1, left + 1] * u
    return upper * (1 - v) + lower * v


def path_mask(mask, points, width):
    for start, end in zip(points, points[1:]):
        paint_segment(mask, start, end, width, 1.)


def extract_surface_details(key, detail, zones, model, owner, face_parts, align=None):
    """Inverse-project each reference crop onto its designated mechanical parts."""
    painted = np.zeros((*owner.shape, 2), dtype=np.float32)
    priority = np.zeros(owner.shape, dtype=np.float32)
    reports = []
    for zone, edge_zone in zip(zones, model['zones'], strict=True):
        assert zone['feature'] == edge_zone['feature']
        mapping, _, residual = register_zone(zone)
        paths = [np.asarray(stroke['xy']) for stroke in zone['strokes']]
        anchors = np.asarray([pair[0] for pair in zone['anchors']])
        points = np.concatenate([anchors, *paths])
        lo = np.maximum(np.floor(points.min(axis=0)).astype(int) - 22, 0)
        hi = np.minimum(np.ceil(points.max(axis=0)).astype(int) + 22, detail.shape[1::-1])
        x0, y0 = lo
        x1, y1 = hi
        crop = detail[y0:y1, x0:x1].copy()
        exclusions = np.zeros(crop.shape[:2], dtype=np.float32)
        paint_masks = []
        for index, (stroke, path, edge) in enumerate(zip(zone['strokes'], paths, edge_zone['paths'], strict=True), 1):
            assert edge['stroke'] == index
            if edge['mapping'] == 'no_confirmed_edge' and (key, zone['feature'], index) not in UNSUPPORTED:
                mask = np.zeros(crop.shape[:2], dtype=np.float32)
                path_mask(mask, path - lo, stroke.get('width', 2.5) + 5)
                paint_masks.append((index, mask))
            else:
                width = stroke.get('width', 2.5) + (13 if edge['mapping'] == 'no_confirmed_edge' else 2.5)
                path_mask(exclusions, path - lo, width)
        crop *= (1 - exclusions[..., None])

        corners = np.array([[x0, y0, 1], [x1, y0, 1], [x0, y1, 1], [x1, y1, 1]]) @ mapping * SCALE
        u0, v0 = np.maximum(np.floor(corners.min(axis=0)).astype(int), 0)
        u1, v1 = np.minimum(np.ceil(corners.max(axis=0)).astype(int), owner.shape[::-1])
        targets = owner[v0:v1, u0:u1]
        allowed = np.array([any(fnmatchcase(part, pattern) for pattern in zone['parts'])
                            for part in face_parts] + [False])
        assert allowed.any(), (key, zone['feature'])
        yy, xx = np.nonzero(allowed[targets])
        target_xy = (np.column_stack((xx + u0, yy + v0)) + .5) / SCALE
        sample_xy = target_xy if align is None else align(zone, edge_zone, mapping, target_xy, targets[yy, xx])
        source_xy = (sample_xy - mapping[2]) @ np.linalg.inv(mapping[:2]) - lo
        distance_to_crop = np.minimum(source_xy, (hi - lo) - source_xy).min(axis=1)
        fade = np.clip(distance_to_crop / 12, 0, 1)
        values = sample(crop, *source_xy.T) * fade[:, None]

        # Choose the registration tied to the closest source guides where crops overlap.
        distance = np.full(len(source_xy), np.inf)
        for path in paths:
            for start, end in zip(path, path[1:]):
                vector = end - start
                if not np.any(vector):
                    continue
                relative = source_xy + lo - start
                along = np.clip((relative @ vector) / (vector @ vector), 0, 1)
                distance = np.minimum(distance, np.linalg.norm(relative - along[:, None] * vector, axis=1))
        score = fade * np.exp(-distance / 42)
        values *= np.clip((54 - distance) / 26, 0, 1)[:, None]
        priority_crop = priority[v0:v1, u0:u1]
        best = (score > priority_crop[yy, xx]) & (values.max(axis=1) >= 2 / 255)
        priority_crop[yy[best], xx[best]] = score[best]
        painted[v0:v1, u0:u1][yy[best], xx[best]] = values[best]

        marks = []
        for index, mask in paint_masks:
            transfer = sample(mask[..., None], *source_xy.T)[:, 0]
            transferred = (transfer > .35) & (values.max(axis=1) > .06)
            marks.append({'stroke': index, 'detail_samples_on_host': int(np.count_nonzero(transferred))})
        reports.append({'feature': zone['feature'], 'max_anchor_error_px': residual,
                        'samples': int(np.count_nonzero(best)), 'surface_marks': marks,
                        'parts': sorted({face_parts[i] for i in targets[yy[best], xx[best]]})})
    assert not painted[owner < 0].any()
    return painted, reports


def pack_tiles(mesh, points, owner, painted):
    """Place visible triangle crops in padded UV islands and leave unseen faces neutral."""
    face_ids = sorted(set(owner[painted.max(axis=2) >= 2 / 255]))
    assert face_ids and face_ids[0] >= 0
    tiles = []
    for index in face_ids:
        xy = points[list(mesh.polygons[index].vertices), :2] * SCALE
        low = np.floor(xy.min(axis=0)).astype(int) - 1
        high = np.ceil(xy.max(axis=0)).astype(int) + 1
        x0, y0 = low
        x1, y1 = high
        assert x0 >= 0 and y0 >= 0 and x1 <= owner.shape[1] and y1 <= owner.shape[0], index
        inside = owner[y0:y1, x0:x1] == index
        bleed = extrema(inside, 1, np.maximum)
        tile = np.where(bleed[..., None], painted[y0:y1, x0:x1], 0.)
        tiles.append((index, xy - low, tile))

    x, y, row_height = 3 * GUTTER, GUTTER, 0
    placed = []
    for index, xy, tile in sorted(tiles, key=lambda row: row[2].shape[0], reverse=True):
        h, w = tile.shape[:2]
        assert w + 2 * GUTTER < ATLAS_WIDTH
        if x + w + GUTTER > ATLAS_WIDTH:
            x = GUTTER
            y += row_height + 2 * GUTTER
            row_height = 0
        placed.append((index, xy, tile, x, y))
        x += w + 2 * GUTTER
        row_height = max(row_height, h)
    height = 2 ** math.ceil(math.log2(y + row_height + GUTTER))
    assert height <= 4096
    pixels = np.zeros((height, ATLAS_WIDTH, 4), dtype=np.float32)
    pixels[..., 3] = 1.
    uv = np.full((len(mesh.loops), 2), (.5 / ATLAS_WIDTH, 1 - .5 / height), dtype=np.float32)
    for index, xy, tile, x, y in placed:
        h, w = tile.shape[:2]
        padding = ((GUTTER, GUTTER), (GUTTER, GUTTER), (0, 0))
        pixels[y-GUTTER:y+h+GUTTER, x-GUTTER:x+w+GUTTER, :2] = np.pad(tile, padding, mode='edge')
        coordinates = (xy + (x, y)) / (ATLAS_WIDTH, height)
        coordinates[:, 1] = 1 - coordinates[:, 1]
        uv[list(mesh.polygons[index].loop_indices)] = coordinates
    return pixels, uv, face_ids


def save_texture(key, pixels, directory=ROOT / 'Textures'):
    height, width = pixels.shape[:2]
    path = directory / f'{key}_A5_SurfaceDetail.png'
    image = bpy.data.images.new(path.stem, width=width, height=height, alpha=False, is_data=True)
    image.colorspace_settings.name = 'Non-Color'
    image.pixels.foreach_set(np.ascontiguousarray(pixels[::-1]).ravel())
    image.update()
    image.file_format = 'PNG'
    image.filepath_raw = str(path)
    image.save()
    image.pack()
    image.filepath_raw = f'//Textures/{path.name}'
    assert image.packed_file and image.packed_file.data == path.read_bytes()
    return image


def detail_group(key, image):
    group = bpy.data.node_groups.new(f'{key} | A5 Surface Detail', 'ShaderNodeTree')
    group.interface.new_socket(name='Base Color', in_out='INPUT', socket_type='NodeSocketColor')
    group.interface.new_socket(name='Color', in_out='OUTPUT', socket_type='NodeSocketColor')
    nodes = group.nodes
    link = group.links.new
    base = nodes.new('NodeGroupInput')
    base.location = (0, 190)
    uv = nodes.new('ShaderNodeUVMap')
    uv.uv_map = UV_NAME
    uv.location = (0, -30)
    texture = nodes.new('ShaderNodeTexImage')
    texture.label = 'A5 surface tones | red recess, green highlights'
    texture.image = image
    texture.extension = 'CLIP'
    texture.location = (190, -30)
    link(uv.outputs['UV'], texture.inputs['Vector'])
    separate = nodes.new('ShaderNodeSeparateColor')
    separate.location = (470, -30)
    link(texture.outputs['Color'], separate.inputs['Color'])
    recess = nodes.new('ShaderNodeMixRGB')
    recess.label = 'Engraving, seam and bolt recesses'
    recess.blend_type = 'MULTIPLY'
    recess.inputs[2].default_value = (.32, .32, .32, 1.)
    recess.location = (660, 190)
    link(separate.outputs['Red'], recess.inputs[0])
    link(base.outputs['Base Color'], recess.inputs[1])
    gain = nodes.new('ShaderNodeMath')
    gain.operation = 'MULTIPLY'
    gain.inputs[1].default_value = .19
    gain.location = (660, -30)
    link(separate.outputs['Green'], gain.inputs[0])
    highlights = nodes.new('ShaderNodeMixRGB')
    highlights.label = 'Metal edge and fastener highlights'
    highlights.inputs[2].default_value = linear_color('#FEE4D9')
    highlights.location = (870, 190)
    link(gain.outputs[0], highlights.inputs[0])
    link(recess.outputs[0], highlights.inputs[1])
    output = nodes.new('NodeGroupOutput')
    output.location = (1080, 190)
    link(highlights.outputs[0], output.inputs['Color'])
    return group


def linked(socket):
    assert len(socket.links) == 1
    return socket.links[0].from_node


def connect_detail(material, group):
    tree = material.node_tree
    physical = next(node for node in tree.nodes if node.label == 'Editable physical finish')
    emission = next(node for node in tree.nodes if node.label == 'Three-tone color')
    ink = linked(emission.inputs['Color'])
    assert ink.label == 'A5 ink along original model edges'
    toon = linked(ink.inputs[1])
    if toon.blend_type == 'ADD':
        toon = linked(toon.inputs[1])
    assert toon.blend_type == 'MULTIPLY'
    base = physical.inputs['Base Color'].links[0].from_socket
    assert toon.inputs[1].links[0].from_socket == base
    detail = tree.nodes.new('ShaderNodeGroup')
    detail.node_tree = group
    detail.label = 'A5 reference detail before lighting'
    detail.location = (toon.location.x - 225, toon.location.y - 390)
    tree.links.new(base, detail.inputs['Base Color'])
    tree.links.new(detail.outputs['Color'], toon.inputs[1])
    tree.links.new(detail.outputs['Color'], physical.inputs['Base Color'])


def build():
    assert Path(bpy.data.filepath).parent == B13
    source = read_json(BASELINE / 'B6_Material_Manifest.json')['models']
    reference = read_json(REFERENCES / 'Reference_A5_Manifest.json')['deliverables']
    edge_models = read_json(B13 / 'B13_Edge_Ink_Manifest.json')['models']
    zones = read_json(B8 / 'reference_strokes.json')
    (ROOT / 'Textures').mkdir(exist_ok=True)
    reports = {}
    for row in reference:
        key = row['key']
        previous = source[key]
        blue, red = (bpy.data.objects[name] for name in previous['objects'])
        mesh = blue.data
        assert red.data == mesh
        classes = [value.value for value in mesh.attributes['SSF Original Material Index'].data]
        assert geometry_digest(blue, classes) == previous['geometry_uv_weights_sha256']
        positions = [value.vector.copy() for value in mesh.attributes[REST].data]
        vertex_parts = original_parts(blue)
        face_parts = [Counter(vertex_parts[i] for i in face.vertices).most_common(1)[0][0]
                      for face in mesh.polygons]
        points = reference_projection(positions, edge_models[key]['reference_camera'])
        owner = projected_ownership(points, mesh.polygons, [0 if value == 0 else 2 for value in classes])
        path = REFERENCES / row['file']
        assert hashlib.sha256(path.read_bytes()).hexdigest() == row['sha256'] == edge_models[key]['reference_sha256']
        detail = reference_detail(path)
        painted, reports_by_zone = extract_surface_details(key, detail, zones[key], edge_models[key], owner, face_parts)
        atlas, uv, face_ids = pack_tiles(mesh, points, owner, painted)
        assert mesh.uv_layers.get(UV_NAME) is None
        active = mesh.uv_layers.active_index
        render_layer = next(layer for layer in mesh.uv_layers if layer.active_render)
        mesh.uv_layers.new(name=UV_NAME, do_init=False).uv.foreach_set('vector', uv.ravel())
        mesh.uv_layers.active_index = active
        render_layer.active_render = True
        image = save_texture(key, atlas)
        group = detail_group(key, image)
        materials = {slot.material for obj in (blue, red) for slot in obj.material_slots
                     if slot.material and 'Finish' in slot.material}
        assert len(materials) == edge_models[key]['materials']
        for material in materials:
            connect_detail(material, group)
        reports[key] = {'model_id': row['model_id'], 'reference_sha256': row['sha256'],
                        'atlas': image.filepath_raw[2:], 'atlas_sha256': hashlib.sha256(image.packed_file.data).hexdigest(),
                        'atlas_size_px': list(image.size), 'uv_layer': UV_NAME,
                        'triangles': len(mesh.polygons), 'painted_faces': len(face_ids),
                        'detail_samples': int(np.count_nonzero(painted.max(axis=2) >= 2 / 255)),
                        'geometry_original_uvs_weights_sha256': previous['geometry_uv_weights_sha256'],
                        'zones': reports_by_zone}
        print(key, reports[key]['painted_faces'], reports[key]['detail_samples'], list(image.size), flush=True)
    assert all(scene[STYLE_PROPERTY] == 1 and scene.render.engine == 'BLENDER_EEVEE' for scene in bpy.data.scenes)
    preview = ROOT / 'Previews'
    preview.mkdir(exist_ok=True)
    for scene in bpy.data.scenes:
        key = next((key for key, item in source.items() if item['scene'] == scene.name), 'SSF_Overview')
        scene.render.filepath = f'//Previews/{key}_B14.png'
    bpy.context.window.scene = bpy.data.scenes[source['AirBase']['scene']]
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(DEST), compress=True, relative_remap=False)
    manifest = {'source': 'B13', 'reference': 'A5', 'maps': {'red': 'recess', 'green': 'highlight'},
                'models': reports, 'unrepresented_reference_geometry': sorted(UNSUPPORTED),
                'visual_review': 'Pending user review in Blender'}
    (ROOT / 'B14_Surface_Detail_Manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n',
                                                        encoding='utf-8')


if __name__ == '__main__':
    build()
