"""Preserve B17 texels while isolating the sampled mip levels of each chart."""

import hashlib
import json
import math
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

import bpy
import numpy as np


ROOT = Path(__file__).resolve().parent
B17 = ROOT.parent / 'Production_FeatureConcepts_B17'
DEST = ROOT / 'SSF_A5_IsolatedAtlases_B18.blend'
sys.dont_write_bytecode = True
sys.path.insert(0, str(B17))
from bake_surface_marks import B13, B15, BASELINE, REST, SCALE, STYLE_PROPERTY, UV_NAME, read_json, reference_projection
from render_review_views import ANGLES, ANGLE_SIZE, CAMERAS, angle_views, world_vertices
sys.path.insert(0, str(B15))
from bake_functional_details import UV as FUNCTIONAL_UV


PROBES = np.array([[1/3, 1/3, 1/3], [.86, .07, .07], [.07, .86, .07], [.07, .07, .86],
                   [.48, .48, .04], [.04, .48, .48], [.48, .04, .48]])


@dataclass
class Chart:
    face: int
    old_crop: np.ndarray
    texels: np.ndarray
    points: np.ndarray
    loop_indices: list
    level: int
    lods: dict

    @property
    def block(self):
        return 2 ** self.level

    @property
    def slot_size(self):
        return ((self.texels.shape[1::-1] + np.full(2, self.block * 2 - 1)) // self.block) * self.block


def pixels(image):
    width, height = image.size
    raw = np.empty(width * height * 4, dtype=np.float32)
    image.pixels.foreach_get(raw)
    return np.rint(raw.reshape(height, width, 4)[::-1] * 255).astype(np.uint8)


def atlas_coords(mesh, layer, width, height):
    uv = np.empty(len(mesh.loops) * 2, dtype=np.float32)
    mesh.uv_layers[layer].uv.foreach_get('vector', uv)
    texels = uv.reshape(-1, 2).astype(np.float64)
    texels[:, 1] = 1 - texels[:, 1]
    return texels * (width, height)


def measure_lods(vertices, views, texture_points):
    lods = {}
    for name, image_points in views.items():
        screen = image_points[vertices]
        differential = screen[1:] - screen[0]
        double_area = abs(np.linalg.det(differential))
        longest_side = np.linalg.norm(screen - np.roll(screen, 1, axis=0), axis=1).max()
        # Near-edge-on triangles can have a large area along one axis but
        # less than half a pixel of extent across the surface.
        if double_area <= 1.5 or double_area < .5 * longest_side:
            continue
        footprint = np.linalg.solve(differential, texture_points[1:] - texture_points[0])
        lods[name] = max(0., float(np.log2(np.linalg.norm(footprint, axis=1).max())))
    return lods


def extract_charts(mesh, layer, source_pixels, projected, views, required_margin=8):
    h, w = source_pixels.shape[:2]
    coords = atlas_coords(mesh, layer, w, h)
    neutral = [.5, .5]
    charts = []
    for face in mesh.polygons:
        loops = list(face.loop_indices)
        xy = coords[loops]
        if np.allclose(xy[0], neutral):
            continue
        vertices = list(face.vertices)
        native = projected[vertices]
        translate = np.rint(xy[0] - native[0]).astype(int)
        assert np.max(abs(xy - native - translate)) < .002
        low = np.floor(native.min(axis=0)).astype(int) - 1 + translate
        high = np.ceil(native.max(axis=0)).astype(int) + 1 + translate
        x0, y0 = low
        x1, y1 = high
        assert min(x0, y0, w - x1, h - y1) >= required_margin
        lods = measure_lods(vertices, views, xy)
        level = max(2, math.ceil(max(lods.values(), default=0.)))
        assert level <= 10, (face.index, level)
        charts.append(Chart(face.index, np.r_[low, high], source_pixels[y0:y1, x0:x1].copy(), xy - low, loops, level, lods))
    assert charts
    return charts


def shelves(charts, width):
    y = x = row_height = 0
    boxes = {}
    last_block = None
    for chart in charts:
        block = chart.block
        w, h = chart.slot_size
        if block != last_block:
            y += row_height
            x, row_height = (block if y == 0 else 0), 0
            last_block = block
        if x + w > width:
            if row_height == 0 and y == 0:
                return None
            y += row_height
            x, row_height = 0, 0
        if w > width:
            return None
        assert x % block == 0 and y % block == 0
        boxes[chart.face] = np.array([x, y, x + w, y + h])
        row_height = max(row_height, h)
        x += w
    return 2 ** math.ceil(math.log2(y + row_height)), boxes


def arrange(charts, loop_count, use_alpha):
    ordered = sorted(charts, key=lambda chart: (-chart.level, -chart.slot_size[1], -chart.slot_size[0], chart.face))
    options = []
    for width in (512, 1024, 2048, 4096, 8192):
        layout = shelves(ordered, width)
        if layout is not None and layout[0] <= 8192:
            options.append((width, *layout))
    width, height, boxes = min(options, key=lambda item: (item[0] * item[1], max(item[:2])))
    texels = np.zeros((height, width, 4), dtype=np.uint8)
    if not use_alpha:
        texels[..., 3] = 255
    uv = np.full((loop_count, 2), (.5 / width, 1 - .5 / height), dtype=np.float32)
    for chart in charts:
        x0, y0, x1, y1 = boxes[chart.face]
        padding = chart.block // 2
        h, w = chart.texels.shape[:2]
        pad = ((padding, y1 - y0 - h - padding), (padding, x1 - x0 - w - padding), (0, 0))
        texels[y0:y1, x0:x1] = np.pad(chart.texels, pad, mode='edge')
        point = chart.points + (x0 + padding, y0 + padding)
        assert np.all(point >= np.array([x0, y0]) + padding) and np.all(point <= np.array([x1, y1]) - padding)
        coords = point / (width, height)
        coords[:, 1] = 1 - coords[:, 1]
        uv[chart.loop_indices] = coords
    assert not texels[0, 0, :3].any() and texels[0, 0, 3] == (0 if use_alpha else 255)
    return texels, uv, boxes


def box_sum(integral, x0, y0, x1, y1):
    x0 = np.clip(x0, 0, integral.shape[1] - 1)
    x1 = np.clip(x1, 0, integral.shape[1] - 1)
    y0 = np.clip(y0, 0, integral.shape[0] - 1)
    y1 = np.clip(y1, 0, integral.shape[0] - 1)
    return integral[y1, x1] - integral[y1, x0] - integral[y0, x1] + integral[y0, x0]


def sample_foreign(integral, points, level, box):
    width = 2 ** level
    centered = points / width - .5
    low = np.floor(centered).astype(int)
    fractions = centered - low
    total = np.zeros(len(points))
    for dx, dy in ((0, 0), (0, 1), (1, 0), (1, 1)):
        x0, y0 = ((low + [dx, dy]) * width).T
        x1, y1 = x0 + width, y0 + width
        full = box_sum(integral, x0, y0, x1, y1)
        inside = box_sum(integral, np.clip(x0, box[0], box[2]), np.clip(y0, box[1], box[3]),
                         np.clip(x1, box[0], box[2]), np.clip(y1, box[1], box[3]))
        weight = (fractions[:, 0] if dx else 1 - fractions[:, 0]) * (fractions[:, 1] if dy else 1 - fractions[:, 1])
        total += (full - inside) * weight / (255 * width ** 2)
    return total


def audit_foreign(texels, charts, use_alpha, view_names, boxes=None):
    intensity = texels.max(axis=2) if use_alpha else texels[..., :2].max(axis=2)
    integral = np.pad(intensity, ((1, 0), (1, 0))).cumsum(axis=0, dtype=np.int64).cumsum(axis=1, dtype=np.int64)
    counts = Counter()
    affected = Counter()
    peak = Counter()
    for chart in charts:
        if boxes is None:
            box = chart.old_crop + [-8, -8, 8, 8]
            samples = PROBES @ chart.points + chart.old_crop[:2]
        else:
            box = boxes[chart.face]
            samples = PROBES @ chart.points + box[:2] + chart.block // 2
            # At every mip the geometry is at least half a largest texel from
            # chart edges aligned to that texel. No external box can contribute.
            assert np.all(box % chart.block == 0)
        for name, lod in chart.lods.items():
            low = math.floor(lod)
            upper_weight = lod - low
            value = sample_foreign(integral, samples, low, box) * (1 - upper_weight)
            if upper_weight > 0:
                value += sample_foreign(integral, samples, low + 1, box) * upper_weight
            worst = float(value.max())
            counts[name] += 1
            affected[name] += worst > 1 / 255
            peak[name] = max(peak[name], worst)
    return {key: {'tested_faces': counts[key], 'foreign_above_1_of_255': affected[key],
                  'peak_foreign_signal': round(peak[key], 8)} for key in sorted(view_names)}


def write_atlas(path, texels, use_alpha):
    h, w = texels.shape[:2]
    image = bpy.data.images.new(path.stem, width=w, height=h, alpha=use_alpha, is_data=True)
    if use_alpha:
        image.alpha_mode = 'CHANNEL_PACKED'
    image.colorspace_settings.name = 'Non-Color'
    storage = np.ascontiguousarray(texels[::-1], dtype=np.float32)
    storage /= 255.
    image.pixels.foreach_set(storage.ravel())
    image.update()
    image.file_format = 'PNG'
    image.filepath_raw = str(path)
    image.save()
    image.pack()
    image.filepath_raw = f'//Textures/{path.name}'
    assert image.packed_file.data == path.read_bytes()
    return image


def atlas_nodes(blue, red, key):
    detail = bpy.data.node_groups[f'{key} | A5 Surface Detail']
    tone = [node for node in detail.nodes if node.type == 'TEX_IMAGE']
    assert len(tone) == 1
    yield 'surface', UV_NAME, tone, False
    materials = {slot.material for obj in (blue, red) for slot in obj.material_slots if 'Finish' in slot.material}
    function = [node for material in materials for node in material.node_tree.nodes if node.type == 'TEX_IMAGE'
                and node.label == 'A5 functional details registered to the LOD0 part']
    if function:
        assert len({node.image for node in function}) == 1
        yield 'functional', FUNCTIONAL_UV, function, True


def test_views(key, rest, overview_xy, overview_pixels, cameras):
    views = {f'RTS {100//factor}%': overview_xy * (overview_pixels / factor) for factor in (1, 2, 4)}
    angles = {angle: cameras[f'{key}_{angle}'] for angle in ANGLES}
    rotations, width = angle_views(rest, angles)
    for angle, (rotation, _) in rotations.items():
        xy = (rest @ np.asarray(rotation.to_matrix(), dtype=np.float64))[:, :2]
        for factor in (1, 2):
            views[f'{angle} {100//factor}%'] = xy * (ANGLE_SIZE[0] / (width * factor))
    return views


def build():
    original = B17 / 'SSF_A5_SurfaceMarks_B17.blend'
    assert Path(bpy.data.filepath) == original
    source = read_json(BASELINE / 'B6_Material_Manifest.json')['models']
    edges = read_json(B13 / 'B13_Edge_Ink_Manifest.json')['models']
    cameras = read_json(CAMERAS)['paired_cameras']
    overview = next(scene for scene in bpy.data.scenes if scene.name.startswith('00 | '))
    bpy.context.window.scene = overview
    bpy.context.view_layer.update()
    deps = bpy.context.evaluated_depsgraph_get()
    view_matrix = np.asarray(overview.camera.rotation_euler.to_matrix(), dtype=float)
    overview_pixels = overview.render.resolution_x / overview.camera.data.ortho_scale
    textures = ROOT / 'Textures'
    textures.mkdir(exist_ok=True)
    (ROOT / 'Previews').mkdir(exist_ok=True)
    models = {}
    for key, row in source.items():
        blue, red = (bpy.data.objects[name] for name in row['objects'])
        assert blue.data == red.data
        mesh = blue.data
        linked = [obj for obj in overview.objects if obj.type == 'MESH' and obj.data == mesh]
        assert len(linked) == 2
        placed = world_vertices(linked[0], deps)
        overview_xy = ((placed - np.asarray(overview.camera.location)) @ view_matrix)[:, :2]
        rest_coords = [value.vector.copy() for value in mesh.attributes[REST].data]
        rest = np.asarray(rest_coords, dtype=np.float64)
        camera = edges[key]['reference_camera']
        projected = reference_projection(rest_coords, camera)[:, :2] * SCALE
        views = test_views(key, rest, overview_xy, overview_pixels, cameras)
        atlas_records = {}
        for name, layer, nodes, alpha in atlas_nodes(blue, red, key):
            previous = nodes[0].image
            old = pixels(previous)
            charts = extract_charts(mesh, layer, old, projected, views)
            original_audit = audit_foreign(old, charts, alpha, views)
            texture, uv, boxes = arrange(charts, len(mesh.loops), alpha)
            new_audit = audit_foreign(texture, charts, alpha, views, boxes)
            assert all(row['peak_foreign_signal'] == 0. for row in new_audit.values())
            mesh.uv_layers[layer].uv.foreach_set('vector', uv.ravel())
            filename = f'{key}_A5_{"Functional" if alpha else "SurfaceDetail"}.png'
            image = write_atlas(textures / filename, texture, alpha)
            for node in nodes:
                node.image = image
            if previous.users == 0:
                bpy.data.images.remove(previous)
            atlas_records[name] = {'file': filename, 'atlas_sha256': hashlib.sha256(image.packed_file.data).hexdigest(),
                                   'old_size': list(old.shape[1::-1][:2]), 'size': list(image.size),
                                   'layer': layer, 'face_count': len(charts), 'before': original_audit, 'after': new_audit,
                                   'face_mip_levels': {chart.face: chart.level for chart in charts}}
            print(key, name, list(image.size), [original_audit[f'RTS {n}%']['foreign_above_1_of_255'] for n in (100, 50, 25)],
                  '->', [new_audit[f'RTS {n}%']['foreign_above_1_of_255'] for n in (100, 50, 25)], flush=True)
        models[key] = atlas_records
    for scene in bpy.data.scenes:
        key = next((key for key, row in source.items() if row['scene'] == scene.name), 'SSF_Overview')
        scene.render.filepath = f'//Previews/{key}_B18.png'
        assert scene[STYLE_PROPERTY] == 1.
    bpy.context.window.scene = overview
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(DEST), compress=True, relative_remap=False)
    manifest = {'base': 'B17', 'reference': 'A5', 'models': models,
                'audit': 'Bilinear and trilinear footprint for projected triangles above 0.75 px² and 0.5 px minimum altitude',
                'visual_review': 'Pending user review in Blender'}
    (ROOT / 'B18_Isolated_Atlases_Manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n',
                                                            encoding='utf-8')


if __name__ == '__main__':
    build()
