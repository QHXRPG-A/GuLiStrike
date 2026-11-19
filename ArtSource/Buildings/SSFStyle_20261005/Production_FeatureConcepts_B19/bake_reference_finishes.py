"""Transfer A5's guide crossbars, capsule number and observation glass finish."""

import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

import bpy
import numpy as np


ROOT = Path(__file__).resolve().parent
B18 = ROOT.parent / 'Production_FeatureConcepts_B18'
DEST = ROOT / 'SSF_A5_ReferenceFinishes_B19.blend'
sys.dont_write_bytecode = True
sys.path.insert(0, str(B18))
from pack_isolated_atlases import (B13, B17, BASELINE, CAMERAS, FUNCTIONAL_UV, REST, SCALE, STYLE_PROPERTY,
                                  Chart, arrange, atlas_nodes, audit_foreign, extract_charts, measure_lods,
                                  pixels, read_json, reference_projection, test_views, world_vertices, write_atlas)
from bake_surface_marks import B8, REFERENCES, original_parts, projected_ownership
from bake_surface_detail import extrema, linear_color, sample
from bake_functional_details import image_samples, normalized, panel_positions, range_mask, shader_mix, visible_surface


FIXTURES = {
    'AirBase': ('polymsh049', 'Hangar guide rail'),
    'CloningCenter': ('Capsule3', 'Cultivation viewport'),
    'CommandCenter': ('Ring', 'Command observation'),
}
LUMA = np.array([.2126, .7152, .0722])


def rail_source(pixels_a5, longitudinal, transverse):
    center = np.interp(longitudinal, [493, 513, 534, 552, 573, 597, 622], [685, 695, 707, 717, 727, 738, 746])
    x = center + transverse
    y = longitudinal - .43 * transverse
    return sample(pixels_a5, x, y), sample(pixels_a5, x + 886, y)


def rail(pixels_a5, rows, baked, _):
    # Find each pale crossbar in both factions of the actual source image.
    sweep = np.arange(493, 622, .5)
    transverse, longitudinal = np.meshgrid(np.arange(-5., 6), sweep)
    blue, red = rail_source(pixels_a5, longitudinal, transverse)
    profile = ((blue @ LUMA).mean(axis=1) > .57) & ((red @ LUMA).mean(axis=1) > .50)
    cuts = np.flatnonzero(np.diff(np.r_[False, profile, False]))
    runs = [(sweep[i], sweep[j - 1]) for i, j in zip(cuts[::2], cuts[1::2], strict=True) if j - i >= 4]
    assert len(runs) == 7, runs
    x, y, z = rows[:, 2:5].T
    track = rows[(np.abs(x) < .245) & range_mask(-y, 6.24, 8.50) & (z > 3.76)]
    axes = np.column_stack((track[:, 2:5], np.ones(len(track))))
    fit = np.linalg.lstsq(axes, track[:, 1], rcond=None)[0]
    across = track[:, 2]
    centerline = track[:, 1] - across * fit[0]
    low, high = centerline.min(), centerline.max()
    along = 493 + normalized(centerline, low, high) * 129
    blue, red = rail_source(pixels_a5, along, (across / .245) * 6.6 * np.sign(-fit[0]))
    pale = normalized(blue @ LUMA, .43, .72) * normalized(red @ LUMA, .36, .58)
    evidence = np.zeros(len(along))
    samples = []
    for start, end in runs:
        fade = np.clip(np.minimum(along - start + 1.6, end + 1.6 - along) / 1.6, 0, 1)
        evidence = np.maximum(evidence, fade)
        samples.append(int(np.count_nonzero((fade > .25) & (pale > .2))))
    assert min(samples) >= 12, samples
    xx, yy = np.floor(track[:, :2] * SCALE).astype(int).T
    baked[yy, xx, 0] = pale * evidence
    baked[yy, xx, 3] = 1.
    return {'feature': '近端导轨七道浅色横档', 'source_bands_px': runs, 'samples_per_band': samples,
            'projected_samples': len(track), 'source_y_px': [493, 622]}


def capsule_number(pixels_a5, rows, baked, _):
    u, v, w = rows[:, 5:].T
    cap = rows[(u > .95) & range_mask(v, .18, .82) & range_mask(w, .22, .80)]
    source_quad = ((461, 352), (494, 346), (500, 380), (464, 386))
    pos = panel_positions(cap, source_quad, (6, 7), ((.18, .82), (.22, .80)))
    blue = sample(pixels_a5, *pos.T)
    red = sample(pixels_a5, pos[:, 0] + 754, pos[:, 1])

    def cream(color):
        r, g, b = color.T
        return normalized(r, .49, .69) * normalized(g, .41, .57) * normalized(r - b, .095, .20)

    signal = cream(blue) * normalized(cream(red), .055, .52)
    xx, yy = np.floor(cap[:, :2] * SCALE).astype(int).T
    baked[yy, xx, 2] = signal
    count = int(np.count_nonzero(signal > .20))
    assert count >= 60, count
    return {'feature': '中舱端盖浅色 3 字', 'source_quad_px': source_quad,
            'projected_samples': len(cap), 'color_samples': count}


def observation(pixels_a5, rows, baked, zones):
    windows = rows[range_mask(rows[:, 4], 8.74, 9.30) & range_mask(rows[:, 0], 367.5, 487.5)]
    x = 335 + normalized(windows[:, 0], 367.5, 487.5) * 240
    top, bottom = (np.asarray(stroke['xy']) for stroke in zones[0]['strokes'][:2])
    near = np.interp(x, top[:, 0], top[:, 1]) + 3.5
    far = np.interp(x, bottom[:, 0], bottom[:, 1]) - 2.
    t = normalized(windows[:, 4], 8.74, 9.30)
    y = near * t + far * (1 - t)
    blue = sample(pixels_a5, x, y)
    red = sample(pixels_a5, x + 754, y)
    cool = normalized(blue[:, 2] - blue[:, 0], .021, .075) * normalized(red[:, 2] - red[:, 0], .017, .07)
    strength = normalized((blue @ LUMA + red @ LUMA) * .5, .17, .33)
    ends = np.clip(np.minimum(x - 335, 575 - x) / 8, 0, 1)
    signal = cool * strength * ends
    xx, yy = np.floor(windows[:, :2] * SCALE).astype(int).T
    baked[yy, xx, 1] = signal
    count = int(np.count_nonzero(signal > .20))
    assert count >= 600, count
    return {'feature': '上层观察窗斜向冷色玻璃纹理', 'window_height_m': [8.74, 9.30],
            'projected_samples': len(windows), 'color_samples': count}


def new_charts(mesh, projected, views, baked, owner, host_faces):
    affected = sorted(set(owner[baked.max(axis=2) > 2 / 255]))
    assert affected and affected[0] >= 0 and set(affected) <= host_faces
    for index in affected:
        face = mesh.polygons[index]
        vertices = list(face.vertices)
        points = projected[vertices]
        low = np.floor(points.min(axis=0)).astype(int) - 1
        high = np.ceil(points.max(axis=0)).astype(int) + 1
        x0, y0 = low
        x1, y1 = high
        assert 0 <= x0 < x1 <= owner.shape[1] and 0 <= y0 < y1 <= owner.shape[0]
        boundary = extrema(owner[y0:y1, x0:x1] == index, 1, np.maximum)
        tile = np.rint(np.where(boundary[..., None], baked[y0:y1, x0:x1], 0.) * 255).astype(np.uint8)
        lods = measure_lods(vertices, views, points)
        level = max(2, int(np.ceil(max(lods.values(), default=0.))))
        yield Chart(int(index), np.r_[low, high], tile, points - low, list(face.loop_indices), level, lods)


def repack_functional(blue, red, key, projected, views, baked, owner, host_faces, textures):
    mesh = blue.data
    changed = list(new_charts(mesh, projected, views, baked, owner, host_faces))
    prior_nodes = [node for name, _, nodes, _ in atlas_nodes(blue, red, key) if name == 'functional' for node in nodes]
    previous_image = prior_nodes[0].image if prior_nodes else None
    previous = extract_charts(mesh, FUNCTIONAL_UV, pixels(previous_image), projected, views,
                              required_margin=0) if previous_image else []
    assert not {tile.face for tile in changed}.intersection(tile.face for tile in previous)
    charts = previous + changed
    atlas, uv, boxes = arrange(charts, len(mesh.loops), True)
    audit = audit_foreign(atlas, charts, True, views, boxes)
    assert all(row['peak_foreign_signal'] == 0. for row in audit.values())
    uv_layer = mesh.uv_layers.get(FUNCTIONAL_UV)
    if not uv_layer:
        active = mesh.uv_layers.active_index
        uv_layer = mesh.uv_layers.new(name=FUNCTIONAL_UV, do_init=False)
        mesh.uv_layers.active_index = active
    uv_layer.uv.foreach_set('vector', uv.ravel())
    image = write_atlas(textures / f'{key}_A5_Functional.png', atlas, True)
    for node in prior_nodes:
        node.image = image
    if previous_image and previous_image.users == 0:
        bpy.data.images.remove(previous_image)
    report = {'atlas': image.filepath_raw[2:], 'atlas_sha256': hashlib.sha256(image.packed_file.data).hexdigest(),
              'size': list(image.size), 'preserved_charts': len(previous), 'new_charts': len(changed),
              'faces': {chart.face: chart.level for chart in charts}, 'new_faces': [chart.face for chart in changed],
              'mip_readback': audit}
    return image, report


def connect(material, image, key):
    tree = material.node_tree
    detail = next(node for node in tree.nodes if node.label == 'A5 reference detail before lighting')
    old_color = detail.inputs['Base Color'].links[0].from_socket
    if key == 'CloningCenter':
        texture = next(node for node in tree.nodes if node.type == 'TEX_IMAGE' and node.image == image)
        separate = next(link.to_node for link in texture.outputs['Color'].links if link.to_node.type == 'SEPARATE_COLOR')
        finish = shader_mix(tree, 'A5 Capsule3 number', separate.outputs['Blue'], old_color, linear_color('#FEE4D9'))
    else:
        uv = tree.nodes.new('ShaderNodeUVMap')
        uv.uv_map = FUNCTIONAL_UV
        texture = tree.nodes.new('ShaderNodeTexImage')
        texture.image = image
        texture.extension = 'CLIP'
        texture.label = 'A5 reference finish on the original LOD0 host'
        tree.links.new(uv.outputs[0], texture.inputs['Vector'])
        separate = tree.nodes.new('ShaderNodeSeparateColor')
        tree.links.new(texture.outputs['Color'], separate.inputs[0])
        uv.location = (detail.location.x - 955, detail.location.y - 300)
        texture.location = (uv.location.x + 180, uv.location.y)
        separate.location = (texture.location.x + 275, texture.location.y)
        if key == 'AirBase':
            insert = shader_mix(tree, 'A5 seven crossbars', separate.outputs['Red'], linear_color('#2C3735'), linear_color('#D5C09C'))
            insert.node.location = (separate.location.x + 195, separate.location.y)
            finish = shader_mix(tree, 'A5 colors on the near guide slot', texture.outputs['Alpha'], old_color, insert)
        else:
            finish = shader_mix(tree, 'A5 upper glass reflections', separate.outputs['Green'], old_color, linear_color('#274E61'))
    finish.node.location = (detail.location.x - 205, detail.location.y - 310)
    tree.links.new(finish, detail.inputs['Base Color'])


def build():
    assert Path(bpy.data.filepath) == B18 / 'SSF_A5_IsolatedAtlases_B18.blend'
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
            image.filepath_raw = '//../Production_FeatureConcepts_B18/' + image.filepath_raw[2:]
    reports = {}
    for key, method in (('AirBase', rail), ('CloningCenter', capsule_number), ('CommandCenter', observation)):
        blue, red = (bpy.data.objects[name] for name in source[key]['objects'])
        mesh = blue.data
        assert red.data == mesh
        placed = next(obj for obj in overview.objects if obj.type == 'MESH' and obj.data == mesh)
        overview_xy = ((world_vertices(placed, deps) - np.asarray(overview.camera.location)) @ view_matrix)[:, :2]
        rest = [value.vector.copy() for value in mesh.attributes[REST].data]
        raw = np.asarray(rest, dtype=np.float64)
        projected = reference_projection(rest, edges[key]['reference_camera'])
        views = test_views(key, raw, overview_xy, overview_pixels, cameras)
        vertex_parts = original_parts(blue)
        parts = [Counter(vertex_parts[i] for i in face.vertices).most_common(1)[0][0] for face in mesh.polygons]
        classes = [0 if item.value == 0 else 2 for item in mesh.attributes['SSF Original Material Index'].data]
        owner = projected_ownership(projected, mesh.polygons, classes)
        part, finish = FIXTURES[key]
        host_faces = {face.index for face in mesh.polygons if parts[face.index] == part and classes[face.index] == 0
                      and blue.material_slots[face.material_index].material.get('Finish', '').startswith(finish)}
        rows = visible_surface(blue, mesh, projected, raw, owner, parts, part, finish)
        ref = REFERENCES / references[key]['file']
        assert hashlib.sha256(ref.read_bytes()).hexdigest() == references[key]['sha256']
        baked = np.zeros((*owner.shape, 4), dtype=np.float32)
        report = method(image_samples(ref), rows, baked, zones[key])
        image, atlas_report = repack_functional(blue, red, key, projected[:, :2] * SCALE, views, baked, owner, host_faces, textures)
        materials = {slot.material for obj in (blue, red) for slot in obj.material_slots
                     if slot.material.get('Finish', '').startswith(finish)}
        for material in materials:
            connect(material, image, key)
        report.update(atlas_report, part=part, finish=finish, source_sha256=references[key]['sha256'],
                      new_materials=sorted(material.name for material in materials))
        reports[key] = report
        print(key, report['feature'], report['new_charts'], report.get('samples_per_band', report.get('color_samples')), flush=True)
    for scene in bpy.data.scenes:
        key = next((key for key, row in source.items() if row['scene'] == scene.name), 'SSF_Overview')
        scene.render.filepath = f'//Previews/{key}_B19.png'
        assert scene[STYLE_PROPERTY] == 1.
    bpy.context.window.scene = bpy.data.scenes[source['CloningCenter']['scene']]
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(DEST), compress=True, relative_remap=False)
    manifest = {'base': 'B18', 'reference': 'A5', 'models': reports, 'visual_review': 'Pending user review in Blender'}
    (ROOT / 'B19_Reference_Finishes_Manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n',
                                                            encoding='utf-8')


if __name__ == '__main__':
    build()
