"""Transfer A5 ink onto the matching B6 LOD0 faces using the original reference camera."""

import hashlib
import json
import math
import sys
import tempfile
from collections import Counter
from fnmatch import fnmatchcase
from pathlib import Path

import bpy
import numpy as np
from mathutils import Euler, Vector


ROOT = Path(__file__).resolve().parent
BASELINE = ROOT.parent / 'Production_FeatureConcepts_B6'
REFERENCES = ROOT.parent / 'References_FeatureConcepts_A5'
CAMERAS = ROOT.parent / 'References_FacilityCoatings_A4' / 'Reference_A4_Manifest.json'
UV_NAME = 'SSF A5 Ink UV'
REFERENCE_SIZE = (850, 630)
SCALE = 2
PADDING = 3
ATLAS_WIDTH = 2048

sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT.parent / 'Production_FeatureConcepts_B1'))
from build_scene import (FINISHES, PART, REST, STYLE_PROPERTY, geometry_digest,
                         linear_color, mesh_bounds, original_parts, set_camera)


FINISH_IDS = {finish.label: key for key, finish in FINISHES.items()}
SURFACE_EDGES = {'guide_rail', 'glass', 'upper_ring', 'lower_ring', 'duct', 'radiator', 'array'}


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def reference_projection(rest, camera):
    location, euler, width = camera
    basis = Euler(euler).to_matrix().transposed()
    points = np.asarray([basis @ (point - Vector(location)) for point in rest])
    pixels_per_meter = REFERENCE_SIZE[0] / width
    points[:, 0] = points[:, 0] * pixels_per_meter + REFERENCE_SIZE[0] / 2
    points[:, 1] = REFERENCE_SIZE[1] / 2 - points[:, 1] * pixels_per_meter
    return points


def projected_ownership(points, faces, originals):
    """Record the nearest opaque source face at every reference sample."""
    width, height = [value * SCALE for value in REFERENCE_SIZE]
    depth = np.full((height, width), -np.inf, dtype=np.float32)
    owner = np.full((height, width), -1, dtype=np.int32)
    for face in faces:
        if originals[face.index] == 2:
            continue
        a, b, c = points[list(face.vertices)]
        xy = np.asarray([a[:2], b[:2], c[:2]]) * SCALE
        low = np.maximum(np.floor(xy.min(axis=0)).astype(int), 0)
        high = np.minimum(np.ceil(xy.max(axis=0)).astype(int) + 1, (width, height))
        x0, y0 = low
        x1, y1 = high
        if x1 <= x0 or y1 <= y0:
            continue
        u, v = xy[1:] - xy[0]
        determinant = u[0] * v[1] - u[1] * v[0]
        if abs(determinant) < 1e-8:
            continue
        x, y = np.meshgrid(np.arange(x0, x1) + .5 - xy[0, 0],
                           np.arange(y0, y1) + .5 - xy[0, 1])
        w1 = (x * v[1] - y * v[0]) / determinant
        w2 = (u[0] * y - u[1] * x) / determinant
        z = a[2] + w1 * (b[2] - a[2]) + w2 * (c[2] - a[2])
        current = depth[y0:y1, x0:x1]
        inside = (w1 >= -1e-6) & (w2 >= -1e-6) & (w1 + w2 <= 1.000001) & (z > current)
        current[inside] = z[inside]
        owner[y0:y1, x0:x1][inside] = face.index
    return owner


def register_zone(zone):
    source = np.asarray([pair[0] for pair in zone['anchors']], dtype=np.float64)
    target = np.asarray([pair[1] for pair in zone['anchors']], dtype=np.float64)
    design = np.column_stack((source, np.ones(len(source))))
    mapping, _, rank, _ = np.linalg.lstsq(design, target, rcond=None)
    assert rank == 3, zone['feature']
    anchor_error = np.linalg.norm(design @ mapping - target, axis=1)
    pixel_scale = math.sqrt(abs(np.linalg.det(mapping[:2])))
    assert .22 < pixel_scale < .86, (zone['feature'], pixel_scale)
    return mapping, pixel_scale, round(float(anchor_error.max()), 3)


def paint_segment(mask, start, end, width, strength):
    vector = end - start
    magnitude = float(vector @ vector)
    if magnitude < 1e-8:
        return
    radius = width / 2
    low = np.maximum(np.floor(np.minimum(start, end) - radius - 1).astype(int), 0)
    high = np.minimum(np.ceil(np.maximum(start, end) + radius + 1).astype(int), mask.shape[::-1])
    x0, y0 = low
    x1, y1 = high
    if x1 <= x0 or y1 <= y0:
        return
    x, y = np.meshgrid(np.arange(x0, x1) + .5 - start[0], np.arange(y0, y1) + .5 - start[1])
    portion = np.clip((x * vector[0] + y * vector[1]) / magnitude, 0, 1)
    distance = np.hypot(x - vector[0] * portion, y - vector[1] * portion)
    coverage = np.clip(radius + .65 - distance, 0, 1) * strength
    region = mask[y0:y1, x0:x1]
    np.maximum(region, coverage, out=region)


def paint_zones(zones, owner, face_parts):
    combined = np.zeros(owner.shape, dtype=np.float32)
    reports = []
    for zone in zones:
        mapping, pixel_scale, residual = register_zone(zone)
        selected = np.asarray([any(fnmatchcase(part, pattern) for pattern in zone['parts'])
                               for part in face_parts] + [False])
        assert selected.any(), (zone['feature'], zone['parts'])
        allowed = selected[owner]
        mask = np.zeros(owner.shape, dtype=np.float32)
        attached = set(zone.get('boundary_strokes', []))
        if attached:
            assert zone['surface_edge'] in SURFACE_EDGES
            assert attached <= set(range(1, len(zone['strokes']) + 1))
        per_path = []
        for index, stroke in enumerate(zone['strokes'], 1):
            if index in attached:
                per_path.append({'stroke': index, 'placement': zone['surface_edge']})
                continue
            line = np.zeros(owner.shape, dtype=np.float32)
            xy = np.asarray(stroke['xy'])
            dest = np.column_stack((xy, np.ones(len(xy)))) @ mapping * SCALE
            width = max(1.1, stroke.get('width', 2.5) * pixel_scale * SCALE)
            for start, end in zip(dest, dest[1:]):
                paint_segment(line, start, end, width, stroke.get('strength', .83))
            fraction = float(line[allowed].sum() / line.sum())
            per_path.append({'stroke': index, 'placement': 'camera_projection',
                             'assigned_surface_fraction': round(fraction, 4)})
            np.maximum(mask, line, out=mask)
        ink = float(mask.sum())
        mask *= allowed
        landed = float(mask.sum())
        touched = owner[mask > .1]
        if not landed and ink:
            raise ValueError(f"{zone['feature']}: strokes missed the assigned parts")
        np.maximum(combined, mask, out=combined)
        reports.append({'feature': zone['feature'], 'source_paths': len(zone['strokes']),
                        'exact_material_boundary_paths': len(attached),
                        'exact_material_boundary': zone.get('surface_edge'),
                        'max_anchor_residual_px': residual,
                        'paint_on_assigned_part': round(landed / ink, 4) if ink else None,
                        'paths': per_path,
                        'faces': len(set(touched)), 'parts': sorted({face_parts[index] for index in touched})})
    return combined, reports


def pack_face_tiles(mesh, points, owner, paint):
    face_ids = sorted(set(owner[paint > 0]))
    assert face_ids and face_ids[0] >= 0
    faces = []
    max_x, max_y = owner.shape[::-1]
    for index in face_ids:
        face = mesh.polygons[index]
        xy = points[list(face.vertices), :2] * SCALE
        low = np.maximum(np.floor(xy.min(axis=0)).astype(int) - 1, 0)
        high = np.minimum(np.ceil(xy.max(axis=0)).astype(int) + 1, (max_x, max_y))
        x0, y0 = low
        x1, y1 = high
        owns_sample = owner[y0:y1, x0:x1] == index
        expanded = np.pad(owns_sample, 1)
        h, w = owns_sample.shape
        near_face = np.logical_or.reduce([expanded[j:j+h, i:i+w] for j in range(3) for i in range(3)])
        local = np.where(near_face, paint[y0:y1, x0:x1], 0)
        assert local.max() > 0
        faces.append((index, xy - low, local))

    x, y, row_height = 2 * PADDING, PADDING, 0
    packed = []
    for index, xy, local in sorted(faces, key=lambda face: face[2].shape[0], reverse=True):
        height, width = local.shape
        assert width < ATLAS_WIDTH - 2 * PADDING
        if x + width + PADDING > ATLAS_WIDTH:
            x = PADDING
            y += row_height + PADDING * 2
            row_height = 0
        packed.append((index, xy, local, x, y))
        row_height = max(row_height, height)
        x += width + PADDING * 2
    height = 2 ** math.ceil(math.log2(y + row_height + PADDING))
    assert height <= 4096
    atlas = np.zeros((height, ATLAS_WIDTH), dtype=np.float32)
    uv = np.full((len(mesh.loops), 2), (.5 / ATLAS_WIDTH, 1. - .5 / height), dtype=np.float32)
    for index, xy, local, x, y in packed:
        h, w = local.shape
        atlas[y:y+h, x:x+w] = local
        # Extend samples through triangle boundaries for bilinear filtering.
        bleed = np.pad(local, 1, mode='edge')
        atlas[y-1:y+h+1, x-1:x+w+1] = bleed
        coordinates = (xy + (x, y)) / (ATLAS_WIDTH, height)
        coordinates[:, 1] = 1. - coordinates[:, 1]
        uv[list(mesh.polygons[index].loop_indices)] = coordinates
    assert mesh.uv_layers.get(UV_NAME) is None
    active = mesh.uv_layers.active_index
    layer = mesh.uv_layers.new(name=UV_NAME, do_init=False)
    layer.uv.foreach_set('vector', uv.ravel())
    mesh.uv_layers.active_index = active
    return atlas, face_ids


def create_mask_image(key, atlas):
    height, width = atlas.shape
    image = bpy.data.images.new(f'{key} | A5 traced ink', width=width, height=height, alpha=True)
    image.colorspace_settings.name = 'Non-Color'
    pixels = np.zeros((height, width, 4), dtype=np.float32)
    pixels[:, :, 3] = atlas[::-1]
    image.pixels.foreach_set(pixels.ravel())
    image.update()
    with tempfile.TemporaryDirectory(prefix='SSF-A5-ink-') as directory:
        path = Path(directory) / f'{key}_A5_Ink.png'
        image.file_format = 'PNG'
        image.filepath_raw = str(path)
        image.save()
        image.pack()
    image.filepath_raw = f'//{key}_A5_Ink.png'
    assert image.packed_file
    return image


class BoundaryGraph:
    def __init__(self, material, origin):
        self.tree = material.node_tree
        self.origin = origin
        self.count = 0

    def node(self, kind, label=None):
        node = self.tree.nodes.new(kind)
        if label:
            node.label = label
        col, row = divmod(self.count, 8)
        node.location = (self.origin.x - 200 * col, self.origin.y - 155 * row)
        self.count += 1
        return node

    def math(self, operation, *arguments):
        node = self.node('ShaderNodeMath')
        node.operation = operation
        for socket, arg in zip(node.inputs, arguments):
            if isinstance(arg, bpy.types.NodeSocket):
                self.tree.links.new(arg, socket)
            else:
                socket.default_value = arg
        return node.outputs[0]

    def coordinates(self, name):
        attribute = self.node('ShaderNodeAttribute', name)
        attribute.attribute_name = name
        xyz = self.node('ShaderNodeSeparateXYZ')
        self.tree.links.new(attribute.outputs['Vector'], xyz.inputs[0])
        return xyz.outputs

    def interval(self, value, low, high):
        return self.math('MULTIPLY', self.math('GREATER_THAN', value, low),
                         self.math('LESS_THAN', value, high))

    def edges(self, value, positions, width):
        weights = []
        for position in positions:
            distance = self.math('ABSOLUTE', self.math('SUBTRACT', value, position))
            alpha = self.node('ShaderNodeMapRange')
            alpha.interpolation_type = 'SMOOTHSTEP'
            alpha.clamp = True
            self.tree.links.new(distance, alpha.inputs['Value'])
            alpha.inputs['From Min'].default_value = width * .28
            alpha.inputs['From Max'].default_value = width
            alpha.inputs['To Min'].default_value = .79
            alpha.inputs['To Max'].default_value = 0.
            weights.append(alpha.outputs[0])
        for weight in weights[1:]:
            weights[0] = self.math('MAXIMUM', weights[0], weight)
        return weights[0]


def exact_surface_ink(graph, finish):
    """Stroke the same surface coordinates that define the B6 material transitions."""
    multiply = lambda a, b: graph.math('MULTIPLY', a, b)
    merge = lambda a, b: graph.math('MAXIMUM', a, b)
    if finish in ('upper_ring', 'lower_ring'):
        _, _, z = graph.coordinates(REST)
        boundaries = (8.74, 9.30) if finish == 'upper_ring' else (5.69, 6.38)
        return graph.edges(z, boundaries, .022)
    if finish == 'guide_rail':
        x, y, z = graph.coordinates(REST)
        x, y = graph.math('ABSOLUTE', x), graph.math('ABSOLUTE', y)
        track = multiply(graph.interval(y, 6.53, 8.34), graph.math('GREATER_THAN', z, 3.76))
        side = graph.edges(x, (.245,), .025)
        pitch = graph.math('FRACT', multiply(graph.math('SUBTRACT', y, 6.53), 5. / 1.81))
        bars = multiply(graph.interval(x, 0., .245), graph.edges(pitch, (.19, .44), .040))
        return multiply(track, merge(side, bars))
    u, v, w = graph.coordinates(PART)
    if finish == 'duct':
        return graph.edges(w, (.125, .902), .013)
    if finish == 'radiator':
        return multiply(graph.math('GREATER_THAN', v, .77), graph.edges(w, (.21, .48), .055))
    if finish == 'array':
        return merge(multiply(graph.interval(v, .054, .946), graph.edges(u, (.04, .96), .013)),
                     multiply(graph.interval(u, .039, .961), graph.edges(v, (.055, .945), .013)))
    side = graph.math('GREATER_THAN', graph.math('ABSOLUTE', graph.math('SUBTRACT', v, .5)), .33)
    top = graph.math('GREATER_THAN', w, .77)
    window_face = merge(top, multiply(side, graph.interval(w, .31, .79)))
    vertical = multiply(window_face, graph.edges(u, (.19, .81), .016))
    horizontal = multiply(multiply(side, graph.interval(u, .185, .815)), graph.edges(w, (.31, .77), .027))
    return merge(vertical, horizontal)


def connect_ink(material, image):
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    emission = next(node for node in nodes if node.label == 'Three-tone color')
    lit_color = emission.inputs['Color'].links[0].from_socket
    uv = nodes.new('ShaderNodeUVMap')
    uv.label = 'A5 ink surface coordinates'
    uv.uv_map = UV_NAME
    uv.location = (emission.location.x - 715, emission.location.y - 320)
    mask = nodes.new('ShaderNodeTexImage')
    mask.label = 'A5 reference strokes'
    mask.image = image
    mask.extension = 'CLIP'
    mask.location = (uv.location.x + 190, uv.location.y)
    ink = nodes.new('ShaderNodeMixRGB')
    ink.label = 'Reference-aligned ink'
    ink.inputs[2].default_value = linear_color('#1A182F')
    ink.location = (emission.location.x - 215, emission.location.y)
    links.new(uv.outputs[0], mask.inputs['Vector'])
    factor = mask.outputs['Alpha']
    finish = FINISH_IDS[material['Finish']]
    if finish in SURFACE_EDGES:
        graph = BoundaryGraph(material, Vector((uv.location.x, uv.location.y - 330)))
        factor = graph.math('MAXIMUM', factor, exact_surface_ink(graph, finish))
        factor.node.label = f'A5 boundary on actual surface | {finish}'
    links.new(factor, ink.inputs[0])
    links.new(lit_color, ink.inputs[1])
    links.new(ink.outputs[0], emission.inputs['Color'])


def align_review_camera(scene, blue, red, camera, image):
    for obj in tuple(scene.objects):
        if obj == scene.camera or obj.name.startswith('A5 reference |'):
            bpy.data.objects.remove(obj, do_unlink=True)
    rotation = Euler(camera[1]).to_matrix()
    direction = rotation @ Vector((0, 0, 1))
    set_camera(scene, mesh_bounds(blue) + mesh_bounds(red), direction, image)


def run(probe):
    source = read_json(BASELINE / 'B6_Material_Manifest.json')
    reference = read_json(REFERENCES / 'Reference_A5_Manifest.json')
    cameras = read_json(CAMERAS)['paired_cameras']
    annotations = {} if probe else read_json(ROOT / 'reference_strokes.json')
    report = {}
    for row in reference['deliverables']:
        key = row['key']
        model = source['models'][key]
        blue, red = (bpy.data.objects[name] for name in model['objects'])
        mesh = blue.data
        assert red.data is mesh
        originals = [value.value for value in mesh.attributes['SSF Original Material Index'].data]
        assert mesh.uv_layers.get(UV_NAME) is None
        assert geometry_digest(blue, originals) == model['geometry_uv_weights_sha256']
        for path, sha in ((row['file'], row['sha256']), (row['geometry_reference'], row['geometry_reference_sha256'])):
            assert hashlib.sha256((REFERENCES / path).read_bytes()).hexdigest() == sha
        positions = [value.vector.copy() for value in mesh.attributes[REST].data]
        vertex_parts = original_parts(blue)
        face_parts = [Counter(vertex_parts[v] for v in face.vertices).most_common(1)[0][0]
                      for face in mesh.polygons]
        points = reference_projection(positions, cameras[f'{key}_Hero'])
        owner = projected_ownership(points, mesh.polygons, originals)
        if probe:
            with (Path(tempfile.gettempdir()) / f'SSF-A5-{key}-ownership.npz').open('wb') as handle:
                np.savez_compressed(handle, owner=owner, parts=np.asarray(face_parts))
            low, high = points[:, :2].min(axis=0), points[:, :2].max(axis=0)
            print(key, [round(v, 2) for v in low], [round(v, 2) for v in high], flush=True)
            continue
        paint, zones = paint_zones(annotations[key], owner, face_parts)
        atlas, face_ids = pack_face_tiles(mesh, points, owner, paint)
        image = create_mask_image(key, atlas)
        materials = {slot.material for obj in (blue, red) for slot in obj.material_slots if 'Finish' in slot.material}
        for material in materials:
            connect_ink(material, image)
        registered_edges = {zone['surface_edge'] for zone in annotations[key] if 'surface_edge' in zone}
        assert registered_edges <= {FINISH_IDS[material['Finish']] for material in materials}, key
        align_review_camera(bpy.data.scenes[model['scene']], blue, red, cameras[f'{key}_Hero'],
                            bpy.data.images[model['reference']])
        report[key] = {'model_id': model['model_id'], 'reference_sha256': row['sha256'],
                       'geometry_reference_sha256': row['geometry_reference_sha256'],
                       'triangles': len(mesh.polygons), 'painted_faces': len(face_ids),
                       'material_slots': len(materials), 'atlas_size_px': list(image.size),
                       'ink_sha256': hashlib.sha256(image.packed_file.data).hexdigest(),
                       'reference_camera': cameras[f'{key}_Hero'],
                       'geometry_uv_weights_sha256_before_new_ink_uv': model['geometry_uv_weights_sha256'],
                       'zones': zones}
        print(key, len(face_ids), [(zone['feature'], zone['paint_on_assigned_part']) for zone in zones], flush=True)
    if probe:
        return
    assert set(report) == set(annotations) == set(source['models'])
    assert all(scene[STYLE_PROPERTY] == 1. for scene in bpy.data.scenes)
    dest = ROOT / 'SSF_A5_ReferenceInk_B8.blend'
    assert not dest.exists()
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(dest), compress=True, relative_remap=False)
    (ROOT / 'B8_Reference_Ink_Manifest.json').write_text(
        json.dumps({'source': 'B6', 'reference': 'A5', 'models': report}, ensure_ascii=False, indent=2) + '\n',
        encoding='utf-8')


if __name__ == '__main__':
    run('--probe' in sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else False)
