"""Use the A5 strokes as guides to select existing LOD0 edges."""

import argparse
import hashlib
import json
import math
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from fnmatch import fnmatchcase
from pathlib import Path

import bpy
import numpy as np


ROOT = Path(__file__).resolve().parent
GUIDE = ROOT.parent / 'Production_FeatureConcepts_B8'
sys.dont_write_bytecode = True
sys.path.insert(0, str(GUIDE))
from build_reference_ink import (BASELINE, CAMERAS, REFERENCES, REFERENCE_SIZE, SCALE,
                                 BoundaryGraph, align_review_camera, read_json,
                                 reference_projection, register_zone)
from build_scene import (FINISHES, PART, REST, STYLE_PROPERTY, geometry_digest, linear_color,
                         original_parts, scene_style)


DISTANCE = 'SSF A5 Edge Distance'
START = 'SSF A5 Edge Start'
END = 'SSF A5 Edge End'
MIN_PROJECTED_LENGTH = 1.1
SEARCH_RADIUS = 5.5
MIN_DIRECTION_DOT = .81


@dataclass
class Edge:
    corners: tuple
    sides: tuple
    parts: frozenset
    kind: str
    xy: np.ndarray
    visible: np.ndarray


def source_edges(mesh, positions, xy, originals, parts, owner, section_edges=(), component_rims=None):
    """Join split normals/UV vertices for edge analysis without welding the mesh."""
    keys = [tuple(round(float(v) * 10000) for v in point) for point in positions]
    topology = defaultdict(list)
    normals = {}
    for face in mesh.polygons:
        if originals[face.index] == 2:
            continue
        a, b, c = (positions[i] for i in face.vertices)
        normals[face.index] = (b - a).cross(c - a).normalized()
        for a, b in face.edge_keys:
            if keys[a] != keys[b]:
                start, end = (a, b) if keys[a] < keys[b] else (b, a)
                topology[keys[start], keys[end]].append((face.index, start, end))

    component_rims = component_rims or {}
    assert set(section_edges) | component_rims.keys() <= topology.keys()
    for corners, hosts in component_rims.items():
        assert all(sum(originals[face] == slot and parts[face] == host
                       for face, _, _ in topology[corners]) == 1 for host, slot in hosts)
    edges = []
    for corners, joined in sorted(topology.items()):
        rims = component_rims.get(corners, ())
        display = tuple(side for side in joined if (parts[side[0]], 1) in rims and originals[side[0]] == 1)
        body = display or tuple(side for side in joined if originals[side[0]] == 0)
        if not body:
            continue
        hosts = frozenset(parts[side[0]] for side in body)
        if display:
            kind = 'display_rim'
        elif len(joined) == 1:
            kind = 'open_boundary'
        elif any(originals[side[0]] == 1 for side in joined) or (
                len(hosts) > 1 and len({mesh.polygons[side[0]].material_index for side in body}) > 1):
            kind = 'component_boundary'
        elif any(normals[one[0]].dot(normals[two[0]]) < .965
                 for i, one in enumerate(joined) for two in joined[i + 1:]):
            kind = 'fold'
        elif corners in section_edges:
            kind = 'tube_section'
        elif corners in component_rims:
            kind = 'component_rim'
        else:
            continue

        _, a, b = body[0]
        projected = xy[[a, b]]
        length = np.linalg.norm(projected[1] - projected[0])
        if length < MIN_PROJECTED_LENGTH:
            continue
        # Check geometric visibility at the reference camera, including subpixel rim samples.
        count = max(3, math.ceil(length / .8) + 1)
        t = np.linspace(0, 1, count)
        samples = np.floor((projected[0] + t[:, None] * (projected[1] - projected[0])) * SCALE).astype(int)
        incident = [side[0] for side in joined]
        visible = np.zeros(count, dtype=bool)
        for dy in (-2, 0, 2):
            for dx in (-2, 0, 2):
                x = np.clip(samples[:, 0] + dx, 0, owner.shape[1] - 1)
                y = np.clip(samples[:, 1] + dy, 0, owner.shape[0] - 1)
                visible |= np.isin(owner[y, x], incident)
        if visible.any():
            edges.append(Edge(corners, body, hosts, kind, projected, visible))
    return edges


def guide_samples(xy):
    for start, end in zip(xy, xy[1:]):
        delta = end - start
        length = float(np.linalg.norm(delta))
        if length > .01:
            count = max(1, math.ceil(length / .85))
            for i in range(count):
                yield start + delta * ((i + .5) / count), delta / length, length / count


def match_guide(guide, edges, subset):
    """Find aligned edge intervals near one registered source stroke."""
    hits = defaultdict(list)
    total = 0.
    if not subset:
        return hits, sum(weight for _, _, weight in guide_samples(guide))
    candidates = [edges[i] for i in subset]
    first = np.asarray([edge.xy[0] for edge in candidates])
    delta = np.asarray([edge.xy[1] - edge.xy[0] for edge in candidates])
    length = np.linalg.norm(delta, axis=1)
    tangent = delta / length[:, None]
    previous = None
    for xy, direction, weight in guide_samples(guide):
        total += weight
        align = np.abs(tangent @ direction)
        along = np.sum((xy - first) * tangent, axis=1)
        t = np.clip(along / length, 0., 1.)
        distance = np.linalg.norm(xy - (first + delta * t[:, None]), axis=1)
        valid = np.flatnonzero((align >= MIN_DIRECTION_DOT) & (distance <= SEARCH_RADIUS)
                               & (along >= -.9) & (along <= length + .9))
        winner = None
        best = float('inf')
        for index in valid:
            edge = candidates[index]
            if not edge.visible[round(t[index] * (len(edge.visible) - 1))]:
                continue
            continuity = 0.
            if previous is not None and edge is not previous and not set(edge.corners).intersection(previous.corners):
                continuity = 1.4
            score = distance[index] + 5. * (1. - align[index]) + continuity
            if score < best:
                winner, best = index, score
        if winner is None:
            previous = None
            continue
        edge = candidates[winner]
        hits[subset[winner]].append((float(t[winner]), weight, float(distance[winner])))
        previous = edge
    return hits, total


def corner_signature(value):
    return tuple(tuple(point) for point in value)


def closed_loop(value):
    edges = [corner_signature(edge) for edge in value]
    vertices = Counter(point for edge in edges for point in edge)
    assert len(vertices) == len(edges) and set(vertices.values()) == {2}
    pending = set(vertices)
    reached = {next(iter(pending))}
    for _ in range(len(vertices)):
        reached |= {point for a, b in edges if a in reached or b in reached for point in (a, b)}
    assert reached == pending
    return edges, vertices


def verified_sections(overrides):
    """Permit coplanar edges only when the authored set is a real closed tube section."""
    result = set()
    for paths in overrides.values():
        for item in paths.values():
            if item['mapping'] != 'tube_section':
                continue
            edges, vertices = closed_loop(item['edges'])
            assert len(vertices) == 6
            points = np.asarray(list(vertices), dtype=float) * .0001
            offsets = points - points.mean(axis=0)
            _, _, basis = np.linalg.svd(offsets, full_matrices=False)
            radii = np.linalg.norm(offsets, axis=1)
            assert max(abs(offsets @ basis[-1])) < .003
            assert radii.std() < radii.mean() * .085
            result.update(edges)
    return result


def verified_component_rims(overrides):
    rims = defaultdict(set)
    for paths in overrides.values():
        for item in paths.values():
            if item['mapping'] in ('component_rim', 'display_rim'):
                edges, vertices = closed_loop(item['edges'])
                assert 6 <= len(vertices) <= 16
                for edge in edges:
                    rims[edge].add((item['host'], int(item['mapping'] == 'display_rim')))
    return rims


def record_reference_matches(zones, edges, pixel_meter, overrides=None):
    supports = defaultdict(list)
    report = []
    overrides = overrides or {}
    edge_ids = {edge.corners: i for i, edge in enumerate(edges)}
    assert overrides.keys() <= {zone['feature'] for zone in zones}
    for zone in zones:
        mapping, scale, _ = register_zone(zone)
        hosts = zone['parts']
        subset = [i for i, edge in enumerate(edges)
                  if any(fnmatchcase(part, pattern) for part in edge.parts for pattern in hosts)]
        allowed = set(subset)
        explicit = overrides.get(zone['feature'], {})
        assert {int(index) for index in explicit} <= set(range(1, len(zone['strokes']) + 1))
        paths = []
        for index, stroke in enumerate(zone['strokes'], 1):
            xy = np.asarray(stroke['xy'])
            guide = np.column_stack((xy, np.ones(len(xy)))) @ mapping
            width = max(.012, stroke.get('width', 2.5) * scale * pixel_meter * .64)
            if str(index) in explicit:
                item = explicit[str(index)]
                method = item['mapping']
                if method == 'no_confirmed_edge':
                    paths.append({'stroke': index, 'mapping': method, 'edge_ids': [], 'note': item['note']})
                    continue
                assert method in ('model_edge', 'tube_section', 'component_rim', 'display_rim')
                signatures = [corner_signature(edge) for edge in item['edges']]
                selected = sorted({edge_ids[edge] for edge in signatures if edge in edge_ids})
                assert selected, (zone['feature'], index)
                if method == 'display_rim':
                    assert all(edges[edge_id].kind == 'display_rim' for edge_id in selected)
                else:
                    assert set(selected) <= allowed, (zone['feature'], index, selected)
                if method == 'model_edge':
                    assert len(selected) == len(signatures)
                elif method == 'tube_section':
                    assert all(any(part.startswith(item['host']) for part in edges[edge_id].parts)
                               for edge_id in selected)
                else:
                    assert all(item['host'] in edges[edge_id].parts for edge_id in selected)
                intervals = {}
                if 'intervals' in item:
                    assert method == 'model_edge' and len(item['intervals']) == len(signatures)
                    intervals = {edge_ids[signature]: tuple(interval)
                                 for signature, interval in zip(signatures, item['intervals'])}
                    assert all(0. <= low < high <= 1. for low, high in intervals.values())
                for edge_id in selected:
                    edge = edges[edge_id]
                    span = float(np.linalg.norm(edge.xy[1] - edge.xy[0]))
                    low, high = intervals.get(edge_id, (0., 1.))
                    if edge_id in intervals:
                        samples = np.linspace(0., 1., len(edge.visible))
                        assert np.any(edge.visible & (samples >= low) & (samples <= high))
                    supports[edge_id].append((low, high, width, span * (high - low)))
                path = {'stroke': index, 'mapping': method, 'edge_ids': selected,
                        **{name: item[name] for name in ('note', 'host', 'center_px') if name in item}}
                if intervals:
                    path['edge_intervals'] = intervals
                if 'chains' in item:
                    chains = [[corner_signature(edge) for edge in chain] for chain in item['chains']]
                    assert set(signatures) == {edge for chain in chains for edge in chain}
                    assert all(set(a).intersection(b) for chain in chains for a, b in zip(chain, chain[1:]))
                    path['edge_chains'] = [[edge_ids[edge] for edge in chain] for chain in chains]
                paths.append(path)
                continue
            matches, total = match_guide(guide, edges, subset)
            selected = {}
            weight = 0.
            for edge_id, samples in matches.items():
                run_length = sum(length for _, length, _ in samples)
                if run_length < 1.15:
                    continue
                edge = edges[edge_id]
                edge_length = float(np.linalg.norm(edge.xy[1] - edge.xy[0]))
                margin = .75 / edge_length
                low = max(0., min(p for p, _, _ in samples) - margin)
                high = min(1., max(p for p, _, _ in samples) + margin)
                if (high - low) * edge_length < 1.1:
                    continue
                supports[edge_id].append((low, high, width, run_length))
                selected[edge_id] = max(distance for _, _, distance in samples)
                weight += run_length
            paths.append({'stroke': index, 'guide_length_px': round(total, 3),
                          'matched_guide_fraction': round(weight / total, 4),
                          'edge_ids': sorted(selected),
                          'max_distance_px': round(max(selected.values()), 3) if selected else None,
                          'mapping': 'registered_edge_match' if selected else 'no_confirmed_edge'})
        report.append({'feature': zone['feature'], 'paths': paths})
    return supports, report


def fixture_edges(key, mesh, positions, edges):
    """Use explicit structural edges for identifiable repeated A5 features."""
    coordinate = mesh.attributes[PART].data
    labels = {i: material.get('Finish', '') for i, material in enumerate(mesh.materials)}
    selected = {}
    for index, edge in enumerate(edges):
        finishes = [labels[mesh.polygons[face].material_index] for face, _, _ in edge.sides]
        _, a, b = edge.sides[0]
        if key == 'StrategyCenter':
            grid_faces = [label == FINISHES['array'].label for label in finishes]
            if any(grid_faces) and (len(grid_faces) == 1 or not all(grid_faces)):
                selected[index] = 'array_tile_front_perimeter'
        elif key == 'Reactor' and edge.kind == 'fold':
            if not all(label == FINISHES['radiator'].label for label in finishes):
                continue
            va, vb = coordinate[a].vector, coordinate[b].vector
            if min(va.y, vb.y) >= .78 and abs(va.x - vb.x) >= .07 and abs(va.z - vb.z) < .15:
                selected[index] = 'radiator_blade_front_lip'
        elif key == 'CommandCenter':
            p, q = positions[a], positions[b]
            if abs(p.z - q.z) > .035 or (p - q).length < .15:
                continue
            z = (p.z + q.z) * .5
            if 'Ring' in edge.parts and min(abs(z - rim) for rim in (8.577, 9.433)) <= .018:
                selected[index] = 'upper_observation_ring_rim'
            elif 'Ring2' in edge.parts and (5.59 < z < 5.66 or 6.47 < z < 6.59):
                selected[index] = 'lower_observation_ring_rim'
        elif key == 'MilitaryFactory' and any(part.startswith('DoorPart') for part in edge.parts):
            delta = positions[b] - positions[a]
            if abs(delta.x) > 1. and abs(delta.z) < .04 and abs(delta.y) < .04:
                selected[index] = 'individual_shutter_front_edge'
    return selected


def span_on_edge(intervals, edge_length):
    """Merge touching runs and prefer the best-supported run across a real gap."""
    runs = []
    for start, end, width, support in sorted(intervals):
        if runs and start <= runs[-1][1] + 1.25 / edge_length:
            old_start, old_end, old_width, old_support = runs[-1]
            runs[-1] = (old_start, max(old_end, end), max(old_width, width), old_support + support)
        else:
            runs.append((start, end, width, support))
    return max(runs, key=lambda run: run[3])[:3]


def write_edge_distances(mesh, positions, edges, supports, fixtures, pixel_meter):
    distances = np.full((len(mesh.loops), 3), 8., dtype=np.float32)
    starts, ends = np.full_like(distances, 8.), np.full_like(distances, 8.)
    written = {}
    for index in sorted(set(supports) | fixtures.keys()):
        edge = edges[index]
        length_px = float(np.linalg.norm(edge.xy[1] - edge.xy[0]))
        low, high, width = (0., 1., max(.012, .72 * pixel_meter)) if index in fixtures else (
            span_on_edge(supports[index], length_px))
        samples = []
        for face_index, a, b in edge.sides:
            face = mesh.polygons[face_index]
            other = next(i for i, vertex in enumerate(face.vertices) if vertex not in (a, b))
            axis = positions[b] - positions[a]
            height = axis.cross(positions[face.vertices[other]] - positions[a]).length / axis.length
            samples.append((face, a, other, axis, height))
        width = min(width, .38 * min(sample[4] for sample in samples))
        if width <= 1e-6:
            continue
        for face, a, other, axis, height in samples:
            extent = axis.length
            tangent = axis / extent
            for corner, loop in enumerate(face.loop_indices):
                along = (positions[face.vertices[corner]] - positions[a]).dot(tangent)
                distances[loop, other] = height / width if corner == other else 0.
                starts[loop, other] = (low * extent - along) / width
                ends[loop, other] = (along - high * extent) / width
        written[index] = {'corners_1e4m': edge.corners, 'faces': [side[0] for side in edge.sides],
                          'parts': sorted(edge.parts), 'kind': edge.kind,
                          'extent': [round(low, 6), round(high, 6)], 'half_width_m': round(width, 6),
                          'reference_fixture': fixtures.get(index)}
    assert written and all(np.isfinite(values).all() for values in (distances, starts, ends))
    for name, values in ((DISTANCE, distances), (START, starts), (END, ends)):
        attribute = mesh.attributes.new(name, 'FLOAT_VECTOR', 'CORNER')
        attribute.data.foreach_set('vector', values.ravel())
    return written


def connect_edges(material, display=False):
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    emission = next(node for node in nodes
                    if node.type == 'EMISSION' and (display or node.label == 'Three-tone color'))
    lit = emission.inputs['Color']
    graph = BoundaryGraph(material, emission.location.copy())
    distance, start, end = (graph.coordinates(name) for name in (DISTANCE, START, END))
    strips = [graph.math('MAXIMUM', d, graph.math('MAXIMUM', a, b)) for d, a, b in zip(distance, start, end)]
    combined = graph.math('MINIMUM', strips[0], graph.math('MINIMUM', strips[1], strips[2]))
    fade = graph.node('ShaderNodeMapRange', 'Soft width around real model edge')
    fade.interpolation_type = 'SMOOTHSTEP'
    fade.clamp = True
    links.new(combined, fade.inputs['Value'])
    fade.inputs['From Min'].default_value = .32
    fade.inputs['From Max'].default_value = 1.12
    fade.inputs['To Min'].default_value = .88
    fade.inputs['To Max'].default_value = 0.
    ink = graph.node('ShaderNodeMixRGB', 'A5 ink along original model edges')
    ink.inputs[2].default_value = linear_color('#1A182F')
    factor = fade.outputs[0]
    if display:
        control = graph.node('ShaderNodeValue', 'Scene Properties | SSF Stylization')
        factor = graph.math('MULTIPLY', factor, scene_style(control.outputs[0]))
    links.new(factor, ink.inputs[0])
    if lit.is_linked:
        links.new(lit.links[0].from_socket, ink.inputs[1])
    else:
        ink.inputs[1].default_value = lit.default_value
    links.new(ink.outputs[0], emission.inputs['Color'])


def run(revision):
    from build_reference_ink import projected_ownership
    assert Path(bpy.data.filepath) == BASELINE / 'SSF_A5_Materials_B6.blend'
    baseline = read_json(BASELINE / 'B6_Material_Manifest.json')
    cameras = read_json(CAMERAS)['paired_cameras']
    reference = read_json(REFERENCES / 'Reference_A5_Manifest.json')
    annotations = read_json(GUIDE / 'reference_strokes.json')
    output = ROOT.parent / f'Production_FeatureConcepts_{revision}'
    overrides = read_json(output / 'edge_correspondence.json') if revision != 'B9' else {}
    assert overrides.keys() <= set(annotations)
    report = {}
    for item in reference['deliverables']:
        key = item['key']
        original = baseline['models'][key]
        blue, red = (bpy.data.objects[name] for name in original['objects'])
        mesh = blue.data
        assert mesh == red.data
        classes = [face.value for face in mesh.attributes['SSF Original Material Index'].data]
        assert geometry_digest(blue, classes) == original['geometry_uv_weights_sha256']
        for field, sha in (('file', 'sha256'), ('geometry_reference', 'geometry_reference_sha256')):
            assert hashlib.sha256((REFERENCES / item[field]).read_bytes()).hexdigest() == item[sha]
        positions = [value.vector.copy() for value in mesh.attributes[REST].data]
        parts = original_parts(blue)
        face_parts = [Counter(parts[v] for v in face.vertices).most_common(1)[0][0] for face in mesh.polygons]
        camera = cameras[f'{key}_Hero']
        projected = reference_projection(positions, camera)
        owner = projected_ownership(projected, mesh.polygons, classes)
        explicit = overrides.get(key, {})
        sections = verified_sections(explicit)
        rims = verified_component_rims(explicit)
        edges = source_edges(mesh, positions, projected[:, :2], classes, face_parts, owner, sections, rims)
        pixel_meter = camera[2] / REFERENCE_SIZE[0]
        supports, zones = record_reference_matches(annotations[key], edges, pixel_meter, explicit)
        fixtures = fixture_edges(key, mesh, positions, edges)
        written = write_edge_distances(mesh, positions, edges, supports, fixtures, pixel_meter)
        assert geometry_digest(blue, classes) == original['geometry_uv_weights_sha256']
        materials = {slot.material for obj in (blue, red) for slot in obj.material_slots if 'Finish' in slot.material}
        for material in materials:
            connect_edges(material)
        displays = {mesh.materials[mesh.polygons[face].material_index]
                    for edge in written.values() if edge['kind'] == 'display_rim' for face in edge['faces']}
        for material in displays:
            connect_edges(material, display=True)
        align_review_camera(bpy.data.scenes[original['scene']], blue, red, camera,
                            bpy.data.images[original['reference']])
        for zone in zones:
            for path in zone['paths']:
                path['edge_ids'] = [edge for edge in path['edge_ids'] if edge in written]
        paths = [path for zone in zones for path in zone['paths']]
        report[key] = {'model_id': item['model_id'], 'reference_sha256': item['sha256'],
                       'reference_camera': camera, 'triangles': len(mesh.polygons),
                       'materials': len(materials), 'display_edge_materials': sorted(m.name for m in displays),
                       'reference_paths': len(paths),
                       'paths_with_matching_edges': sum(bool(path['edge_ids']) for path in paths),
                       'source_edges': written, 'zones': zones,
                       'fixture_edges': dict(Counter(rule for rule in fixtures.values()))}
        print(key, len(written), 'original edges', report[key]['paths_with_matching_edges'], '/', len(paths),
              'A5 paths', len(fixtures), 'fixture edges', flush=True)

    assert len(report) == 6 and all(scene[STYLE_PROPERTY] == 1. for scene in bpy.data.scenes)
    target = output / f'SSF_A5_ModelEdgeInk_{revision}.blend'
    assert not target.exists()
    manifest = json.dumps({'source': 'B6', 'reference': 'A5', 'models': report},
                          ensure_ascii=False, indent=2) + '\n'
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(target), compress=True, relative_remap=False)
    (output / f'{revision}_Edge_Ink_Manifest.json').write_text(manifest, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--revision', choices=('B9', 'B10', 'B11', 'B12', 'B13'), default='B9')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    run(args.revision)
