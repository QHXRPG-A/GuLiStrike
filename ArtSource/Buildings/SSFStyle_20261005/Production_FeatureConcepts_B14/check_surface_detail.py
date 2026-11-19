"""Read the saved B14 material bindings and optionally render user review images."""

import hashlib
import json
import sys
from pathlib import Path

import bpy
import numpy as np


sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bake_surface_detail import (B13, BASELINE, DEST, ROOT, STYLE_PROPERTY, UV_NAME,
                                 geometry_digest, linked, read_json)


INK_ATTRIBUTES = ('SSF A5 Edge Distance', 'SSF A5 Edge Start', 'SSF A5 Edge End')


def attribute_values(attribute):
    values = np.empty(len(attribute.data) * 3, dtype=np.float32)
    attribute.data.foreach_get('vector', values)
    return values


def check_material(material, group):
    output = next(node for node in material.node_tree.nodes if node.type == 'OUTPUT_MATERIAL' and node.is_active_output)
    mode = linked(output.inputs['Surface'])
    assert mode.label == 'Pure material / three bands'
    control = linked(mode.inputs[0])
    driver = next(fcurve.driver for fcurve in material.node_tree.animation_data.drivers
                  if fcurve.data_path == control.outputs[0].path_from_id('default_value'))
    assert driver.variables[0].targets[0].data_path == f'["{STYLE_PROPERTY}"]'
    assert mode.inputs[0].links[0].from_socket.default_value == 1.
    physical = linked(mode.inputs[1])
    emission = linked(linked(mode.inputs[2]).inputs[0])
    ink = linked(emission.inputs['Color'])
    assert ink.label == 'A5 ink along original model edges' and ink.inputs[0].is_linked
    toon = linked(ink.inputs[1])
    if toon.blend_type == 'ADD':
        toon = linked(toon.inputs[1])
    assert toon.blend_type == 'MULTIPLY'
    detail = linked(toon.inputs[1])
    assert detail == linked(physical.inputs['Base Color'])
    assert detail.node_tree == group and detail.inputs['Base Color'].is_linked
    bands = linked(toon.inputs[2])
    shadow_mid = linked(bands.inputs[0])
    mid = linked(shadow_mid.inputs[1])
    high = linked(bands.inputs[1])
    base = shadow_mid.inputs[0].default_value
    step1, step2 = mid.inputs[1].default_value, high.inputs[1].default_value
    assert np.allclose([base, base + step1, base + step1 + step2], [.40, .68, 1.])
    assert np.allclose([linked(branch.inputs[0]).inputs[1].default_value for branch in (mid, high)], [.33, .69])


def check(render):
    assert Path(bpy.data.filepath) == DEST
    source = read_json(BASELINE / 'B6_Material_Manifest.json')['models']
    edges = read_json(B13 / 'B13_Edge_Ink_Manifest.json')['models']
    manifest_path = ROOT / 'B14_Surface_Detail_Manifest.json'
    manifest = read_json(manifest_path)
    records = manifest['models']
    assert set(source) == set(records) == set(edges)
    assert len(bpy.data.scenes) == 7
    assert all(scene[STYLE_PROPERTY] == 1. and scene.render.engine == 'BLENDER_EEVEE' for scene in bpy.data.scenes)
    mesh_names = [bpy.data.objects[source[key]['objects'][0]].data.name for key in records]
    baseline_file = B13 / 'SSF_A5_ModelEdgeInk_B13.blend'
    with bpy.data.libraries.load(str(baseline_file)) as (_, dest):
        dest.meshes = mesh_names
    total_triangles = 0
    total_faces = 0
    total_zones = 0
    total_marks = 0
    for (key, row), baseline_mesh in zip(records.items(), dest.meshes, strict=True):
        blue, red = (bpy.data.objects[name] for name in source[key]['objects'])
        mesh = blue.data
        assert mesh == red.data and mesh != baseline_mesh
        assert len(mesh.polygons) == len(baseline_mesh.polygons) == row['triangles'] == source[key]['triangles']
        classes = [value.value for value in mesh.attributes['SSF Original Material Index'].data]
        original = blue.copy()
        original.data = mesh.copy()
        original.data.uv_layers.remove(original.data.uv_layers[UV_NAME])
        assert geometry_digest(original, classes) == row['geometry_original_uvs_weights_sha256']
        assert next(layer.name for layer in mesh.uv_layers if layer.active_render) == next(
            layer.name for layer in baseline_mesh.uv_layers if layer.active_render)
        for name in INK_ATTRIBUTES:
            assert np.array_equal(attribute_values(mesh.attributes[name]), attribute_values(baseline_mesh.attributes[name]))
        bpy.data.meshes.remove(baseline_mesh)

        width, height = row['atlas_size_px']
        uv = np.empty(len(mesh.loops) * 2, dtype=np.float32)
        mesh.uv_layers[UV_NAME].uv.foreach_get('vector', uv)
        uv = uv.reshape(-1, 2)
        assert np.isfinite(uv).all() and (uv >= 0).all() and (uv <= 1).all()
        neutral = [.5 / width, 1 - .5 / height]
        painted = [face.index for face in mesh.polygons if not np.allclose(uv[face.loop_start], neutral)]
        assert len(painted) == row['painted_faces'] and all(classes[i] == 0 for i in painted)
        assert all(zone['samples'] > 0 for zone in row['zones'])
        assert all(mark['detail_samples_on_host'] > 0 for zone in row['zones'] for mark in zone['surface_marks'])

        group = bpy.data.node_groups[f'{key} | A5 Surface Detail']
        texture = next(node for node in group.nodes if node.type == 'TEX_IMAGE')
        assert linked(texture.inputs['Vector']).uv_map == UV_NAME
        image = texture.image
        path = ROOT / row['atlas']
        assert image.packed_file.data == path.read_bytes()
        assert hashlib.sha256(image.packed_file.data).hexdigest() == row['atlas_sha256']
        assert list(image.size) == [width, height] and image.colorspace_settings.name == 'Non-Color'
        pixels = np.empty(width * height * 4, dtype=np.float32)
        image.pixels.foreach_get(pixels)
        pixels = pixels.reshape(height, width, 4)
        assert pixels[..., :2].max(axis=(0, 1)).min() > .25
        assert not pixels[-8:, :8, :3].any() and not pixels[..., 2].any()
        assert row['reference_sha256'] == edges[key]['reference_sha256']
        scene = bpy.data.scenes[source[key]['scene']]
        bpy.context.window.scene = scene
        bpy.context.view_layer.update()
        materials = {slot.material for obj in (blue, red) for slot in obj.material_slots
                     if slot.material and 'Finish' in slot.material}
        assert len(materials) == edges[key]['materials']
        for material in materials:
            check_material(material, group)
        row['readback'] = {'source_mesh_and_original_uvs': 'verified', 'ink_attributes': 'unchanged',
                           'same_texture_for_both_teams': True, 'active_lighting_bands': [.4, .68, 1.]}
        total_triangles += row['triangles']
        total_faces += len(painted)
        total_zones += len(row['zones'])
        total_marks += sum(len(zone['surface_marks']) for zone in row['zones'])
        print(f'{key} | {row["triangles"]} triangles | {len(painted)} mapped faces | {len(materials)} styled materials', flush=True)
    assert (total_triangles, total_zones, total_marks) == (24746, 37, 11)
    manifest['readback'] = {'triangles_one_set': total_triangles, 'painted_faces': total_faces,
                            'registered_zones': total_zones, 'surface_marks': total_marks}
    if render:
        previews = []
        for scene in sorted(bpy.data.scenes, key=lambda item: item.name):
            bpy.context.window.scene = scene
            bpy.ops.render.render(write_still=True)
            path = Path(bpy.path.abspath(scene.render.filepath))
            assert path.stat().st_size > 1000
            with path.open('rb') as handle:
                assert handle.read(8) == b'\x89PNG\r\n\x1a\n'
            previews.append(path.relative_to(ROOT).as_posix())
        manifest['previews'] = previews
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('B14 READBACK COMPLETE', manifest['readback'], flush=True)


if __name__ == '__main__':
    check('--render' in sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else False)
