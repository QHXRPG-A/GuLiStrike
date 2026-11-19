"""Verify the saved B16 atlases, UV alignment and existing material stages."""

import hashlib
import json
import sys
from pathlib import Path

import bpy
import numpy as np


sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bake_registered_detail import B13, B14, B15, BASELINE, DEST, REST, ROOT, SCALE, STYLE_PROPERTY, UV_NAME, read_json, reference_projection
from bake_surface_detail import geometry_digest
from check_surface_detail import INK_ATTRIBUTES, attribute_values, check_material
sys.path.insert(0, str(B15))
from bake_functional_details import UV as FUNCTIONAL_UV
from check_functional_details import reads_node


def verify_original_mesh(obj, old_mesh):
    mesh = obj.data
    classes = [value.value for value in mesh.attributes['SSF Original Material Index'].data]
    assert len(mesh.polygons) == len(old_mesh.polygons)
    active = next(layer.name for layer in mesh.uv_layers if layer.active_render)
    assert active == next(layer.name for layer in old_mesh.uv_layers if layer.active_render)
    copy = obj.copy()
    copy.data = mesh.copy()
    baseline = obj.copy()
    baseline.data = old_mesh.copy()
    for proxy in (copy, baseline):
        proxy.data.uv_layers.remove(proxy.data.uv_layers[UV_NAME])
    assert geometry_digest(copy, classes) == geometry_digest(baseline, classes)
    for name in INK_ATTRIBUTES:
        assert np.array_equal(attribute_values(mesh.attributes[name]), attribute_values(old_mesh.attributes[name]))
    return classes


def verify_uv(mesh, row, camera, classes):
    w, h = row['atlas_size_px']
    points = reference_projection([value.vector.copy() for value in mesh.attributes[REST].data], camera)
    atlas_uv = np.empty(len(mesh.loops) * 2, dtype=np.float32)
    mesh.uv_layers[UV_NAME].uv.foreach_get('vector', atlas_uv)
    atlas_uv = atlas_uv.reshape(-1, 2)
    assert (atlas_uv >= 0).all() and (atlas_uv <= 1).all()
    neutral = np.array([.5 / w, 1 - .5 / h])
    mapped = [face for face in mesh.polygons if not np.allclose(atlas_uv[face.loop_start], neutral)]
    assert len(mapped) == row['mapped_faces'] and all(classes[face.index] == 0 for face in mapped)
    errors = []
    for face in mapped:
        atlas_points = atlas_uv[list(face.loop_indices)].astype(float)
        atlas_points[:, 1] = 1 - atlas_points[:, 1]
        atlas_points *= (w, h)
        projected = points[list(face.vertices), :2] * SCALE
        offsets = atlas_points - projected
        errors.append(float(np.max(np.linalg.norm(offsets - offsets[0], axis=1))))
    assert max(errors) < .002, max(errors)
    return round(max(errors), 6)


def verify_images(key, row, materials, group, functional, texture_root=ROOT):
    path = texture_root / row['atlas']
    image = next(node.image for node in group.nodes if node.type == 'TEX_IMAGE')
    assert image.packed_file.data == path.read_bytes()
    assert hashlib.sha256(image.packed_file.data).hexdigest() == row['atlas_sha256']
    assert list(image.size) == row['atlas_size_px'] and image.colorspace_settings.name == 'Non-Color'
    for material in materials:
        check_material(material, group)
        functional_tex = [node for node in material.node_tree.nodes if node.type == 'TEX_IMAGE' and
                          node.label == 'A5 functional details registered to the LOD0 part']
        expected = functional.get(key)
        assert len(functional_tex) == (expected is not None and material.name in expected['materials'])
        if functional_tex:
            tex = functional_tex[0]
            assert tex.inputs['Vector'].links[0].from_node.uv_map == FUNCTIONAL_UV
            assert hashlib.sha256(tex.image.packed_file.data).hexdigest() == expected['atlas_sha256']
            detail = next(node for node in material.node_tree.nodes if node.label == 'A5 reference detail before lighting')
            assert reads_node(detail.inputs['Base Color'], tex)


def check():
    assert Path(bpy.data.filepath) == DEST
    manifest_path = ROOT / 'B16_Registered_Detail_Manifest.json'
    manifest = read_json(manifest_path)
    source = read_json(BASELINE / 'B6_Material_Manifest.json')['models']
    edge_models = read_json(B13 / 'B13_Edge_Ink_Manifest.json')['models']
    functional = read_json(B15 / 'B15_Functional_Detail_Manifest.json')['models']
    baseline = read_json(B14 / 'B14_Surface_Detail_Manifest.json')['models']
    old_scene = B15 / 'SSF_A5_FunctionalDetail_B15.blend'
    with bpy.data.libraries.load(str(old_scene)) as (_, old):
        old.meshes = [bpy.data.objects[row['objects'][0]].data.name for row in source.values()]
    total = 0
    samples = 0
    before = after = 0.
    for (key, record), old_mesh in zip(source.items(), old.meshes, strict=True):
        row = manifest['models'][key]
        assert row['atlas_sha256'] != baseline[key]['atlas_sha256']
        assert row['reference_sha256'] == baseline[key]['reference_sha256']
        blue, red = (bpy.data.objects[name] for name in record['objects'])
        assert blue.data == red.data and len(blue.data.polygons) == record['triangles']
        classes = verify_original_mesh(blue, old_mesh)
        row['max_uv_roundtrip_px'] = verify_uv(blue.data, row, edge_models[key]['reference_camera'], classes)
        bpy.context.window.scene = bpy.data.scenes[record['scene']]
        bpy.context.view_layer.update()
        group = bpy.data.node_groups[f'{key} | A5 Surface Detail']
        materials = {slot.material for obj in (blue, red) for slot in obj.material_slots if 'Finish' in slot.material}
        verify_images(key, row, materials, group, functional)
        total += record['triangles']
        assert row['mean_after_px'] < row['mean_before_px']
        samples += row['validation_samples']
        before += row['validation_samples'] * row['mean_before_px']
        after += row['validation_samples'] * row['mean_after_px']
        print(key, row['mapped_faces'], row['max_uv_roundtrip_px'], row['mean_before_px'], row['mean_after_px'], flush=True)
    assert total == 24746
    assert len(bpy.data.scenes) == 7 and all(scene[STYLE_PROPERTY] == 1 and scene.render.engine == 'BLENDER_EEVEE'
                                            for scene in bpy.data.scenes)
    manifest['readback'] = {'triangles_per_faction': total, 'validation_samples': samples,
                            'mean_before_px': round(before / samples, 3), 'mean_after_px': round(after / samples, 3),
                            'active_lighting_bands': [.40, .68, 1.], 'b15_functional_textures_preserved': len(functional)}
    previews = []
    for scene in sorted(bpy.data.scenes, key=lambda row: row.name):
        bpy.context.window.scene = scene
        bpy.ops.render.render(write_still=True)
        path = Path(bpy.path.abspath(scene.render.filepath))
        with path.open('rb') as handle:
            assert handle.read(8) == b'\x89PNG\r\n\x1a\n'
        assert path.stat().st_size > 1000
        previews.append(path.relative_to(ROOT).as_posix())
    manifest['previews'] = previews
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('B16 READBACK COMPLETE', manifest['readback'], flush=True)


if __name__ == '__main__':
    check()
