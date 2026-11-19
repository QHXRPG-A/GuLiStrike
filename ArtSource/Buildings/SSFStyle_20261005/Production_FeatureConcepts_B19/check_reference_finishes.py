"""Read back the new B19 functional masks and preserve every B18 chart."""

import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

import bpy
import numpy as np


sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bake_reference_finishes import (B13, B18, BASELINE, DEST, FIXTURES, FUNCTIONAL_UV, REST, ROOT, SCALE,
                                    STYLE_PROPERTY, atlas_nodes, extract_charts, original_parts, pixels,
                                    read_json, reference_projection)
from check_isolated_atlases import check_mesh
from check_surface_detail import check_material
from check_functional_details import reads_node


def check():
    assert Path(bpy.data.filepath) == DEST
    path = ROOT / 'B19_Reference_Finishes_Manifest.json'
    manifest = read_json(path)
    source = read_json(BASELINE / 'B6_Material_Manifest.json')['models']
    edges = read_json(B13 / 'B13_Edge_Ink_Manifest.json')['models']
    baseline = read_json(B18 / 'B18_Isolated_Atlases_Manifest.json')['models']
    assert set(FIXTURES) == set(manifest['models'])
    with bpy.data.libraries.load(str(B18 / 'SSF_A5_IsolatedAtlases_B18.blend')) as (_, old):
        old.meshes = [bpy.data.objects[row['objects'][0]].data.name for row in source.values()]
    new_total = 0
    preserved_total = 0
    for (key, model), old_mesh in zip(source.items(), old.meshes, strict=True):
        blue, red = (bpy.data.objects[name] for name in model['objects'])
        mesh = blue.data
        assert blue.data == red.data and len(mesh.polygons) == model['triangles']
        classes = check_mesh(blue, old_mesh)
        scene = bpy.data.scenes[model['scene']]
        bpy.context.window.scene = scene
        bpy.context.view_layer.update()
        group = bpy.data.node_groups[f'{key} | A5 Surface Detail']
        materials = {slot.material for obj in (blue, red) for slot in obj.material_slots if 'Finish' in slot.material}
        for material in materials:
            check_material(material, group)
        atlases = {name: (layer, nodes) for name, layer, nodes, _ in atlas_nodes(blue, red, key)}
        surface_layer, surface_nodes = atlases['surface']
        assert hashlib.sha256(surface_nodes[0].image.packed_file.data).hexdigest() == baseline[key]['surface']['atlas_sha256']
        old_uv = np.empty(len(mesh.loops) * 2, dtype=np.float32)
        new_uv = np.empty_like(old_uv)
        old_mesh.uv_layers[surface_layer].uv.foreach_get('vector', old_uv)
        mesh.uv_layers[surface_layer].uv.foreach_get('vector', new_uv)
        assert np.array_equal(old_uv, new_uv)
        if key not in FIXTURES:
            layer, nodes = atlases['functional']
            assert all(hashlib.sha256(node.image.packed_file.data).hexdigest() == baseline[key]['functional']['atlas_sha256']
                       for node in nodes)
            old_mesh.uv_layers[layer].uv.foreach_get('vector', old_uv)
            mesh.uv_layers[layer].uv.foreach_get('vector', new_uv)
            assert np.array_equal(old_uv, new_uv)
            continue

        row = manifest['models'][key]
        host, finish = FIXTURES[key]
        vertex_parts = original_parts(blue)
        changed_materials = {material for material in materials if material.get('Finish', '').startswith(finish)}
        assert {material.name for material in changed_materials} == set(row['new_materials'])
        changed_nodes = []
        for material in changed_materials:
            detail = next(node for node in material.node_tree.nodes if node.label == 'A5 reference detail before lighting')
            node = next(node for node in material.node_tree.nodes if node.type == 'TEX_IMAGE' and
                        node.label in ('A5 reference finish on the original LOD0 host',
                                       'A5 functional details registered to the LOD0 part'))
            assert node.inputs['Vector'].links[0].from_node.uv_map == FUNCTIONAL_UV
            assert reads_node(detail.inputs['Base Color'], node)
            changed_nodes.append(node)
        images = {node.image for node in changed_nodes + atlases.get('functional', (None, []))[1]}
        assert len(images) == 1
        image = images.pop()
        assert image.alpha_mode == 'CHANNEL_PACKED' and image.colorspace_settings.name == 'Non-Color'
        assert image.packed_file.data == (ROOT / row['atlas']).read_bytes()
        assert hashlib.sha256(image.packed_file.data).hexdigest() == row['atlas_sha256']
        assert list(image.size) == row['size']
        rest = [value.vector.copy() for value in mesh.attributes[REST].data]
        projected = reference_projection(rest, edges[key]['reference_camera'])[:, :2] * SCALE
        all_pixels = pixels(image)
        assert not all_pixels[0, 0].any()
        charts = {tile.face: tile for tile in extract_charts(mesh, FUNCTIONAL_UV, all_pixels, projected, {}, required_margin=0)}
        assert set(charts) == set(map(int, row['faces']))
        new_faces = set(row['new_faces'])
        assert len(new_faces) == row['new_charts']
        expected_channel = {'AirBase': 0, 'CloningCenter': 2, 'CommandCenter': 1}[key]
        mask_count = 0
        for index in new_faces:
            face = mesh.polygons[index]
            part = Counter(vertex_parts[i] for i in face.vertices).most_common(1)[0][0]
            assert classes[index] == 0 and part == host
            assert blue.material_slots[face.material_index].material.get('Finish', '').startswith(finish)
            tile = charts[index].texels
            assert not tile[..., [channel for channel in range(3) if channel != expected_channel]].any()
            if key != 'AirBase':
                assert not tile[..., 3].any()
            mask_count += int(np.count_nonzero(tile[..., expected_channel] > 51))
        assert mask_count > 0
        if key == 'AirBase':
            assert len(row['source_bands_px']) == 7 and min(row['samples_per_band']) >= 12
            assert FUNCTIONAL_UV not in old_mesh.uv_layers
        else:
            record = baseline[key]['functional']
            source_path = B18 / 'Textures' / record['file']
            assert hashlib.sha256(source_path.read_bytes()).hexdigest() == record['atlas_sha256']
            prior = bpy.data.images.load(str(source_path), check_existing=False)
            prior.colorspace_settings.name = 'Non-Color'
            prior.alpha_mode = 'CHANNEL_PACKED'
            old_pixels = pixels(prior)
            bpy.data.images.remove(prior)
            previous = extract_charts(old_mesh, FUNCTIONAL_UV, old_pixels, projected, {}, required_margin=0)
            assert len(previous) == row['preserved_charts']
            assert set(charts) - new_faces == {tile.face for tile in previous}
            for tile in previous:
                assert np.array_equal(charts[tile.face].texels, tile.texels)
            preserved_total += len(previous)
        for tile in charts.values():
            block = 2 ** row['faces'][str(tile.face)]
            assert np.all((tile.old_crop[:2] - block // 2) % block == 0)
        assert not any(result['peak_foreign_signal'] for result in row['mip_readback'].values())
        row['readback'] = {'new_mask_samples_above_point_two': mask_count, 'new_charts_on_host': len(new_faces),
                           'preserved_functional_charts': row['preserved_charts']}
        new_total += len(new_faces)
        print(key, host, len(new_faces), mask_count, 'verified', flush=True)
    assert sum(row['triangles'] for row in source.values()) == 24746
    assert len(bpy.data.scenes) == 7 and all(scene[STYLE_PROPERTY] == 1. and scene.render.engine == 'BLENDER_EEVEE'
                                            for scene in bpy.data.scenes)
    previews = []
    for scene in sorted(bpy.data.scenes, key=lambda item: item.name):
        bpy.context.window.scene = scene
        bpy.ops.render.render(write_still=True)
        preview = Path(bpy.path.abspath(scene.render.filepath))
        with preview.open('rb') as handle:
            assert handle.read(8) == b'\x89PNG\r\n\x1a\n'
        assert preview.stat().st_size > 1000
        previews.append(preview.relative_to(ROOT).as_posix())
    manifest['previews'] = previews
    manifest['readback'] = {'new_charts': new_total, 'preserved_functional_charts_in_repacked_atlases': preserved_total,
                            'triangles_per_faction': 24746, 'light_bands': [.4, .68, 1.]}
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('B19 READBACK COMPLETE', manifest['readback'], flush=True)


if __name__ == '__main__':
    check()
