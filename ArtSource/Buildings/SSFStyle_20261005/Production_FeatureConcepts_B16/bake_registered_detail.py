"""Use B13 edge correspondences to improve B15's A5 surface registration."""

import hashlib
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector


ROOT = Path(__file__).resolve().parent
B15 = ROOT.parent / 'Production_FeatureConcepts_B15'
B14 = ROOT.parent / 'Production_FeatureConcepts_B14'
DEST = ROOT / 'SSF_A5_RegisteredDetail_B16.blend'
SIGMA = 9.
sys.dont_write_bytecode = True
sys.path.insert(0, str(B14))
from bake_surface_detail import (B13, B8, BASELINE, REFERENCES, REST, SCALE, STYLE_PROPERTY, UV_NAME,
                                 extract_surface_details, original_parts, pack_tiles, projected_ownership,
                                 read_json, reference_detail, reference_projection, save_texture)


def nearest_curve(points, tangent, guide):
    """Closest point on the annotated stroke with compatible tangent."""
    segments = np.diff(guide, axis=0)
    length = np.linalg.norm(segments, axis=1)
    candidates = (length > .05) & (abs(segments @ tangent) > .83 * length)
    start = guide[:-1][candidates]
    segments = segments[candidates]
    length = length[candidates]
    if not len(start):
        return None
    span = points[:, None, :] - start
    fraction = np.clip(np.sum(span * segments, axis=2) / length ** 2, 0, 1)
    offsets = start + fraction[..., None] * segments - points[:, None, :]
    distances = np.linalg.norm(offsets, axis=2)
    closest = distances.argmin(axis=1)
    return offsets[np.arange(len(points)), closest]


def correct(points, locations, offsets, trust):
    """Blend local translations with the original affine registration."""
    value = np.empty_like(points)
    local_sq = np.sum(locations ** 2, axis=1)
    for i in range(0, len(points), 4096):
        group = points[i:i+4096]
        squared = np.sum(group ** 2, axis=1)[:, None] + local_sq - 2 * (group @ locations.T)
        weights = np.exp(-np.maximum(squared, 0) / (2 * SIGMA ** 2)) * trust
        value[i:i+len(group)] = (weights @ offsets) / (1.35 + weights.sum(axis=1, keepdims=True))
    return value


def field_gradient(locations, offsets, trust):
    lo = np.floor(locations.min(axis=0) - 3 * SIGMA)
    hi = np.ceil(locations.max(axis=0) + 3 * SIGMA)
    x, y = np.meshgrid(np.arange(lo[0], hi[0], 1.5), np.arange(lo[1], hi[1], 1.5))
    points = np.column_stack((x.ravel(), y.ravel()))
    field = correct(points, locations, offsets, trust).reshape(*x.shape, 2)
    dy, dx = np.gradient(field, 1.5, axis=(0, 1))
    return float(np.sqrt(np.sum(dx ** 2 + dy ** 2, axis=2)).max())


class SurfaceRegistration:
    def __init__(self, model, owner, face_parts):
        self.model = model
        self.owner = owner
        self.parts = np.asarray(face_parts)
        self.edges = {}
        self.reports = []
        for edge_id, edge in model['source_edges'].items():
            rest = [Vector(np.asarray(point) / 10000) for point in edge['corners_1e4m']]
            self.edges[int(edge_id)] = reference_projection(rest, model['reference_camera'])[:, :2]

    def visible_parts(self, points, edge):
        width, height = self.owner.shape[::-1]
        pixel = np.floor(points * SCALE).astype(int)
        x, y = pixel.T
        adjacent = []
        for dx, dy in ((0, 0), (-2, 0), (2, 0), (0, -2), (0, 2), (-1, -1), (1, 1)):
            adjacent.append(self.owner[np.clip(y + dy, 0, height - 1), np.clip(x + dx, 0, width - 1)])
        owners = np.stack(adjacent, axis=1)
        nearby = np.where(np.isin(owners, edge['faces']), owners, -1)
        return {part: ((nearby >= 0) & (self.parts[nearby] == part)).any(axis=1) for part in edge['parts']}

    def observations(self, zone, edge_zone, mapping):
        tracks = defaultdict(list)
        for source, path in zip(zone['strokes'], edge_zone['paths'], strict=True):
            if path['mapping'] in ('no_confirmed_edge', 'display_rim'):
                continue
            source_xy = np.asarray(source['xy'])
            guide = source_xy @ mapping[:2] + mapping[2]
            traced = path['mapping'] != 'registered_edge_match'
            for edge_id in path['edge_ids']:
                edge = self.model['source_edges'][str(edge_id)]
                start, end = self.edges[edge_id]
                delta = end - start
                length = np.linalg.norm(delta)
                low, high = path.get('edge_intervals', {}).get(str(edge_id), edge['extent'])
                span = length * (high - low)
                if span < 2.1:
                    continue
                count = max(3, int(np.ceil(span / 2.6)))
                ratio = low + (np.arange(count) + .5) / count * (high - low)
                points = start + ratio[:, None] * delta
                offsets = nearest_curve(points, delta / length, guide)
                if offsets is None:
                    continue
                offset_length = np.linalg.norm(offsets, axis=1)
                good = offset_length <= (10 if traced else 5.8)
                good &= abs(offsets @ (delta / length)) <= 2.2
                trust = (1.15 if traced else .76) * (1 - .035 * offset_length)
                held_out = np.arange(count) % 3 == 1
                for part, visible in self.visible_parts(points, edge).items():
                    matches = good & visible
                    if np.any(matches):
                        tracks[part].append(np.column_stack((points[matches], offsets[matches], trust[matches], held_out[matches])))
        return tracks

    def __call__(self, zone, edge_zone, mapping, points, face_ids):
        registered = points.copy()
        face_parts = self.parts[face_ids]
        fitted = []
        for part, tracks in self.observations(zone, edge_zone, mapping).items():
            selected = face_parts == part
            if not np.any(selected):
                continue
            data = np.concatenate(tracks)
            # Repeated edge references should not amplify the same constraint.
            _, unique = np.unique(np.round(data[:, :4], 2), axis=0, return_index=True)
            data = data[unique]
            train, test = data[data[:, 5] == 0], data[data[:, 5] == 1]
            if len(train) < 3 or not len(test):
                continue
            locations, offsets, trust = train[:, :2], train[:, 2:4], train[:, 4]
            gradient = field_gradient(locations, offsets, trust)
            if gradient > .55:
                offsets = offsets * (.55 / gradient)
            expected = test[:, 2:4]
            estimate = correct(test[:, :2], locations, offsets, trust)
            before = np.linalg.norm(expected, axis=1)
            after = np.linalg.norm(expected - estimate, axis=1)
            # Keep the original registration when neighboring constraints conflict.
            if (before.mean() - after.mean() < .22 or after.max() > before.max() + .45):
                continue
            displacement = correct(points[selected], locations, offsets, trust)
            registered[selected] += displacement
            fitted.append({'part': part, 'controls': len(train), 'checked_samples': len(test),
                           'mean_before_px': round(float(before.mean()), 4), 'mean_after_px': round(float(after.mean()), 4),
                           'max_displacement_px': round(float(np.linalg.norm(displacement, axis=1).max()), 4),
                           'max_sampled_gradient': round(min(gradient, .55), 4)})
        self.reports.append({'feature': zone['feature'], 'parts': fitted})
        return registered


def build():
    assert Path(bpy.data.filepath) == B15 / 'SSF_A5_FunctionalDetail_B15.blend'
    source = read_json(BASELINE / 'B6_Material_Manifest.json')['models']
    prior = read_json(B14 / 'B14_Surface_Detail_Manifest.json')['models']
    references = read_json(REFERENCES / 'Reference_A5_Manifest.json')['deliverables']
    edge_models = read_json(B13 / 'B13_Edge_Ink_Manifest.json')['models']
    zones = read_json(B8 / 'reference_strokes.json')
    textures = ROOT / 'Textures'
    textures.mkdir(exist_ok=True)
    (ROOT / 'Previews').mkdir(exist_ok=True)
    reports = {}
    for row in references:
        key = row['key']
        blueprint = source[key]
        blue, red = (bpy.data.objects[name] for name in blueprint['objects'])
        mesh = blue.data
        assert red.data == mesh
        rest = [value.vector.copy() for value in mesh.attributes[REST].data]
        classes = [0 if item.value == 0 else 2 for item in mesh.attributes['SSF Original Material Index'].data]
        vertex_parts = original_parts(blue)
        parts = [Counter(vertex_parts[index] for index in face.vertices).most_common(1)[0][0] for face in mesh.polygons]
        model = edge_models[key]
        points = reference_projection(rest, model['reference_camera'])
        owner = projected_ownership(points, mesh.polygons, classes)
        path = REFERENCES / row['file']
        assert hashlib.sha256(path.read_bytes()).hexdigest() == row['sha256'] == prior[key]['reference_sha256']
        registration = SurfaceRegistration(model, owner, parts)
        data, zone_reports = extract_surface_details(key, reference_detail(path), zones[key], model, owner, parts, registration)
        assert len(zone_reports) == len(registration.reports)
        assert all(zone['samples'] for zone in zone_reports)
        assert all(mark['detail_samples_on_host'] for zone in zone_reports for mark in zone['surface_marks'])
        pixels, uv, faces = pack_tiles(mesh, points, owner, data)
        mesh.uv_layers[UV_NAME].uv.foreach_set('vector', uv.ravel())
        texture = next(node for node in bpy.data.node_groups[f'{key} | A5 Surface Detail'].nodes if node.type == 'TEX_IMAGE')
        previous_image = texture.image
        texture.image = save_texture(key, pixels, textures)
        digest = hashlib.sha256(texture.image.packed_file.data).hexdigest()
        assert digest != prior[key]['atlas_sha256']
        if previous_image.users == 0:
            bpy.data.images.remove(previous_image)
        validated = [part for zone in registration.reports for part in zone['parts']]
        total = sum(part['checked_samples'] for part in validated)
        assert total > 0, key
        before, after = [sum(part[field] * part['checked_samples'] for part in validated) / total
                         for field in ('mean_before_px', 'mean_after_px')]
        reports[key] = {'reference_sha256': row['sha256'], 'atlas': texture.image.filepath_raw[2:],
                        'atlas_sha256': digest, 'atlas_size_px': list(texture.image.size), 'mapped_faces': len(faces),
                        'validation_samples': total, 'mean_before_px': round(before, 3), 'mean_after_px': round(after, 3),
                        'registrations': registration.reports, 'surface_details': zone_reports}
        print(key, len(faces), len(validated), total, round(before, 3), round(after, 3), flush=True)
    for scene in bpy.data.scenes:
        key = next((key for key, item in source.items() if item['scene'] == scene.name), 'SSF_Overview')
        scene.render.filepath = f'//Previews/{key}_B16.png'
        assert scene[STYLE_PROPERTY] == 1.
    for image in bpy.data.images:
        if image.name.endswith('_A5_Functional') and image.filepath_raw.startswith('//Textures/'):
            image.filepath_raw = '//../Production_FeatureConcepts_B15/' + image.filepath_raw[2:]
    bpy.context.window.scene = bpy.data.scenes[source['AirBase']['scene']]
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(DEST), compress=True, relative_remap=False)
    manifest = {'base': 'B15', 'reference': 'A5', 'registration_reference': 'B13', 'models': reports,
                'validation_coordinates': '850 x 630 model reference projection',
                'visual_review': 'Pending user review in Blender'}
    (ROOT / 'B16_Registered_Detail_Manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n',
                                                           encoding='utf-8')


if __name__ == '__main__':
    build()
