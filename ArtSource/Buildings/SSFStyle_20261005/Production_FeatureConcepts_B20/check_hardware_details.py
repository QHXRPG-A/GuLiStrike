"""Read the B20 materials and the exact B19 chart pixels from the saved file."""

import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

import bpy
import numpy as np


sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bake_hardware_details import (B13, B19, BASELINE, DEST, FIXTURES, FUNCTIONAL_UV, REST, ROOT, SCALE,
                                   STYLE_PROPERTY, original_parts, read_json, reference_projection)
from pack_isolated_atlases import atlas_nodes, extract_charts, pixels
from check_isolated_atlases import check_mesh
from check_surface_detail import check_material
from check_functional_details import reads_node
from bake_reference_finishes import B18


def same_uv(a, b, layer):
    values = np.empty(len(a.loops) * 2, dtype=np.float32)
    previous = np.empty_like(values)
    a.uv_layers[layer].uv.foreach_get('vector', values)
    b.uv_layers[layer].uv.foreach_get('vector', previous)
    return np.array_equal(values, previous)


def check_counts(key, row):
    if key == 'MilitaryFactory':
        assert len(row['shutters']) == 4 and all(len(panel['fasteners']) == 4 for panel in row['shutters'])
        assert all(mark['samples'] >= 8 for panel in row['shutters'] for mark in panel['fasteners'])
    elif key == 'Reactor':
        assert row['physical_blade_rows'] == len(row['blades']) == 3 and row['reference_blade_rows'] == 4
        assert all(blade['teal_samples'] >= 18 for blade in row['blades'])
    else:
        assert len(row['latches']) == 5 and {mark['part'] for mark in row['latches']} == set(row['hosts'])


def check():
    assert Path(bpy.data.filepath) == DEST
    path = ROOT / 'B20_Hardware_Details_Manifest.json'
    manifest = read_json(path)
    records = manifest['models']
    source = read_json(BASELINE / 'B6_Material_Manifest.json')['models']
    edges = read_json(B13 / 'B13_Edge_Ink_Manifest.json')['models']
    baseline = read_json(B18 / 'B18_Isolated_Atlases_Manifest.json')['models']
    previous_finishes = read_json(B19 / 'B19_Reference_Finishes_Manifest.json')['models']
    assert set(records) == set(FIXTURES)
    with bpy.data.libraries.load(str(B19 / 'SSF_A5_ReferenceFinishes_B19.blend')) as (_, old):
        old.meshes = [bpy.data.objects[row['objects'][0]].data.name for row in source.values()]
    total_new = total_old = 0
    for (key, model), old_mesh in zip(source.items(), old.meshes, strict=True):
        blue, red = (bpy.data.objects[name] for name in model['objects'])
        mesh = blue.data
        assert mesh == red.data and len(mesh.polygons) == model['triangles']
        classes = check_mesh(blue, old_mesh)
        scene = bpy.data.scenes[model['scene']]
        bpy.context.window.scene = scene
        bpy.context.view_layer.update()
        group = bpy.data.node_groups[f'{key} | A5 Surface Detail']
        atlases = {name: (uv, nodes) for name, uv, nodes, _ in atlas_nodes(blue, red, key)}
        uv, nodes = atlases['surface']
        assert same_uv(mesh, old_mesh, uv)
        assert hashlib.sha256(nodes[0].image.packed_file.data).hexdigest() == baseline[key]['surface']['atlas_sha256']
        materials = {slot.material for obj in (blue, red) for slot in obj.material_slots if 'Finish' in slot.material}
        for material in materials:
            check_material(material, group)
        functional = [node for material in materials for node in material.node_tree.nodes if node.type == 'TEX_IMAGE' and
                      node.label in ('A5 reference finish on the original LOD0 host',
                                     'A5 functional details registered to the LOD0 part',
                                     'A5 fixed hardware on original LOD0 part')]
        assert len({node.image for node in functional}) == 1
        image = functional[0].image
        if key not in records:
            assert same_uv(mesh, old_mesh, FUNCTIONAL_UV)
            assert hashlib.sha256(image.packed_file.data).hexdigest() == previous_finishes[key]['atlas_sha256']
            continue

        row = records[key]
        check_counts(key, row)
        assert image.alpha_mode == 'CHANNEL_PACKED' and image.colorspace_settings.name == 'Non-Color'
        assert image.packed_file.data == (ROOT / row['atlas']).read_bytes()
        assert hashlib.sha256(image.packed_file.data).hexdigest() == row['atlas_sha256']
        assert list(image.size) == row['size']
        changed_materials = set()
        for material in materials:
            for node in material.node_tree.nodes:
                if node.type == 'TEX_IMAGE' and node.label == 'A5 fixed hardware on original LOD0 part':
                    detail = next(item for item in material.node_tree.nodes if item.label == 'A5 reference detail before lighting')
                    assert node.inputs['Vector'].links[0].from_node.uv_map == FUNCTIONAL_UV
                    assert reads_node(detail.inputs['Base Color'], node)
                    changed_materials.add(material.name)
        assert changed_materials == set(row['new_materials'])
        rest = [item.vector.copy() for item in mesh.attributes[REST].data]
        projected = reference_projection(rest, edges[key]['reference_camera'])[:, :2] * SCALE
        packed = pixels(image)
        assert not packed[0, 0].any()
        charts = {tile.face: tile for tile in extract_charts(mesh, FUNCTIONAL_UV, packed, projected, {}, required_margin=0)}
        assert set(charts) == set(map(int, row['faces']))
        new_faces = set(row['new_faces'])
        assert len(new_faces) == row['new_charts']
        parts = original_parts(blue)
        signal = 0
        for index in new_faces:
            face = mesh.polygons[index]
            part = Counter(parts[i] for i in face.vertices).most_common(1)[0][0]
            assert classes[index] == 0 and part in row['hosts']
            assert blue.material_slots[face.material_index].material.get('Finish', '').startswith(row['hosts'][part])
            crop = charts[index].texels
            assert not crop[..., 2].any() and (key == 'Reactor' or not crop[..., 3].any())
            signal += int(np.count_nonzero(crop[..., 0] > 51))
        assert signal > 0
        original = baseline[key]['functional']
        source_path = B18 / 'Textures' / original['file']
        assert hashlib.sha256(source_path.read_bytes()).hexdigest() == original['atlas_sha256']
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
        for tile in charts.values():
            block = 2 ** row['faces'][str(tile.face)]
            assert np.all((tile.old_crop[:2] - block // 2) % block == 0)
        assert not any(result['peak_foreign_signal'] for result in row['mip_readback'].values())
        row['readback'] = {'new_charts_on_hosts': len(new_faces), 'painted_primary_mask_samples': signal,
                           'preserved_chart_count': len(previous)}
        total_new += len(new_faces)
        total_old += len(previous)
        print(key, len(new_faces), signal, 'verified', flush=True)
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
    manifest['readback'] = {'new_charts': total_new, 'preserved_charts_in_repacked_atlases': total_old,
                            'triangles_per_faction': 24746, 'light_bands': [.4, .68, 1.]}
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('B20 READBACK COMPLETE', manifest['readback'], flush=True)


if __name__ == '__main__':
    check()
