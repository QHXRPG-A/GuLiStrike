"""Verify the B18 chart isolation and original texels from the saved project."""

import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

import bpy
import numpy as np


sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from pack_isolated_atlases import B13, B15, B17, BASELINE, DEST, REST, ROOT, SCALE, STYLE_PROPERTY, UV_NAME
from pack_isolated_atlases import FUNCTIONAL_UV, atlas_coords, atlas_nodes, pixels, read_json, reference_projection
from bake_surface_marks import B16
from bake_surface_detail import geometry_digest
from check_surface_detail import INK_ATTRIBUTES, attribute_values, check_material
from check_functional_details import reads_node


def original_atlas(key, name, marks, registered, functional):
    if name == 'functional':
        return B15 / 'Textures' / functional[key]['atlas'], functional[key]['atlas_sha256']
    if key in marks:
        return B17 / marks[key]['atlas'], marks[key]['atlas_sha256']
    return B16 / registered[key]['atlas'], registered[key]['atlas_sha256']


def check_mesh(obj, baseline):
    mesh = obj.data
    classes = [item.value for item in mesh.attributes['SSF Original Material Index'].data]
    active = next(layer.name for layer in mesh.uv_layers if layer.active_render)
    assert active == next(layer.name for layer in baseline.uv_layers if layer.active_render)
    current, previous = obj.copy(), obj.copy()
    current.data = mesh.copy()
    previous.data = baseline.copy()
    for target in (current.data, previous.data):
        for name in (UV_NAME, FUNCTIONAL_UV):
            if target.uv_layers.get(name):
                target.uv_layers.remove(target.uv_layers[name])
    assert geometry_digest(current, classes) == geometry_digest(previous, classes)
    for name in INK_ATTRIBUTES:
        assert np.array_equal(attribute_values(mesh.attributes[name]), attribute_values(baseline.attributes[name]))
    return classes


def check_atlas(mesh, previous_mesh, layer, record, source_pixels, image, projected, classes, alpha):
    current = pixels(image)
    w, h = image.size
    old_h, old_w = source_pixels.shape[:2]
    assert [w, h] == record['size'] and [old_w, old_h] == record['old_size']
    assert current[0, 0].tolist() == ([0, 0, 0, 0] if alpha else [0, 0, 0, 255])
    old_uv = atlas_coords(previous_mesh, layer, old_w, old_h)
    new_uv = atlas_coords(mesh, layer, w, h)
    assert np.all(new_uv >= 0) and np.all(new_uv <= (w, h))
    occupied = np.zeros((h // 4, w // 4), dtype=bool)
    slots = record['face_mip_levels']
    checked = 0
    for face in mesh.polygons:
        loops = list(face.loop_indices)
        if str(face.index) not in slots:
            assert np.allclose(old_uv[loops], .5) and np.allclose(new_uv[loops], .5)
            continue
        assert classes[face.index] == 0
        points = projected[list(face.vertices)]
        old_offset = np.rint(old_uv[loops[0]] - points[0]).astype(int)
        new_offset = np.rint(new_uv[loops[0]] - points[0]).astype(int)
        assert np.max(np.abs(new_uv[loops] - old_uv[loops] - (new_offset - old_offset))) < .002
        low = np.floor(points.min(axis=0)).astype(int) - 1
        high = np.ceil(points.max(axis=0)).astype(int) + 1
        x0, y0 = low + old_offset
        x1, y1 = high + old_offset
        original = source_pixels[y0:y1, x0:x1]
        new_low = low + new_offset
        block = 2 ** slots[str(face.index)]
        margin = block // 2
        slot_start = new_low - margin
        slot_size = (high - low + 2 * block - 1) // block * block
        slot_end = slot_start + slot_size
        assert np.all(slot_start % block == 0) and np.all(slot_end % block == 0)
        assert np.all(slot_start >= 0) and np.all(slot_end <= (w, h))
        sx, sy = slot_start
        ex, ey = slot_end
        free = occupied[sy//4:ey//4, sx//4:ex//4]
        assert not free.any()
        free[:] = True
        right, bottom = slot_size - (high - low) - margin
        expected = np.pad(original, ((margin, bottom), (margin, right), (0, 0)), mode='edge')
        assert np.array_equal(current[sy:ey, sx:ex], expected), (mesh.name, layer, face.index)
        checked += 1
    assert checked == len(slots) == record['face_count']
    return checked


def check():
    assert Path(bpy.data.filepath) == DEST
    path = ROOT / 'B18_Isolated_Atlases_Manifest.json'
    manifest = read_json(path)
    source = read_json(BASELINE / 'B6_Material_Manifest.json')['models']
    edges = read_json(B13 / 'B13_Edge_Ink_Manifest.json')['models']
    marks = read_json(B17 / 'B17_Surface_Marks_Manifest.json')['models']
    registered = read_json(B16 / 'B16_Registered_Detail_Manifest.json')['models']
    functional = read_json(B15 / 'B15_Functional_Detail_Manifest.json')['models']
    with bpy.data.libraries.load(str(B17 / 'SSF_A5_SurfaceMarks_B17.blend')) as (_, baseline):
        baseline.meshes = [bpy.data.objects[row['objects'][0]].data.name for row in source.values()]
    totals = Counter()
    before = Counter()
    after = Counter()
    for (key, row), old_mesh in zip(source.items(), baseline.meshes, strict=True):
        blue, red = (bpy.data.objects[name] for name in row['objects'])
        mesh = blue.data
        assert red.data == mesh and len(mesh.polygons) == row['triangles']
        classes = check_mesh(blue, old_mesh)
        bpy.context.window.scene = bpy.data.scenes[row['scene']]
        bpy.context.view_layer.update()
        group = bpy.data.node_groups[f'{key} | A5 Surface Detail']
        materials = {slot.material for obj in (blue, red) for slot in obj.material_slots if 'Finish' in slot.material}
        for material in materials:
            check_material(material, group)
            nodes = [node for node in material.node_tree.nodes if node.type == 'TEX_IMAGE' and
                     node.label == 'A5 functional details registered to the LOD0 part']
            expected = functional.get(key)
            assert len(nodes) == (expected is not None and material.name in expected['materials'])
            if nodes:
                detail = next(node for node in material.node_tree.nodes if node.label == 'A5 reference detail before lighting')
                assert reads_node(detail.inputs['Base Color'], nodes[0])
        rest = [attr.vector.copy() for attr in mesh.attributes[REST].data]
        projected = reference_projection(rest, edges[key]['reference_camera'])[:, :2] * SCALE
        for name, layer, nodes, alpha in atlas_nodes(blue, red, key):
            record = manifest['models'][key][name]
            assert record['layer'] == layer
            assert len({node.image for node in nodes}) == 1
            image = nodes[0].image
            assert image.colorspace_settings.name == 'Non-Color' and (not alpha or image.alpha_mode == 'CHANNEL_PACKED')
            assert image.packed_file.data == (ROOT / 'Textures' / record['file']).read_bytes()
            assert hashlib.sha256(image.packed_file.data).hexdigest() == record['atlas_sha256']
            source_path, source_hash = original_atlas(key, name, marks, registered, functional)
            assert hashlib.sha256(source_path.read_bytes()).hexdigest() == source_hash
            original = bpy.data.images.load(str(source_path), check_existing=False)
            original.colorspace_settings.name = 'Non-Color'
            if alpha:
                original.alpha_mode = 'CHANNEL_PACKED'
            old = pixels(original)
            bpy.data.images.remove(original)
            checked = check_atlas(mesh, old_mesh, layer, record, old, image, projected, classes, alpha)
            record['saved_chart_texels_verified'] = checked
            totals['atlases'] += 1
            totals['charts'] += checked
            totals['old_texels'] += int(np.prod(record['old_size']))
            totals['new_texels'] += int(np.prod(record['size']))
            for view, result in record['before'].items():
                before[view] += result['foreign_above_1_of_255']
                after[view] += record['after'][view]['foreign_above_1_of_255']
            print(key, name, checked, 'unchanged original chart crops', flush=True)
        totals['triangles_per_faction'] += row['triangles']
    assert totals['atlases'] == 11 and totals['triangles_per_faction'] == 24746
    assert any(before.values()) and not any(after.values())
    assert len(bpy.data.scenes) == 7 and all(scene[STYLE_PROPERTY] == 1 and scene.render.engine == 'BLENDER_EEVEE'
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
    manifest['readback'] = {**totals, 'mip_samples_above_1_of_255_before': dict(before),
                            'mip_samples_above_1_of_255_after': dict(after), 'light_bands': [.4, .68, 1.]}
    manifest['previews'] = previews
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('B18 READBACK COMPLETE', manifest['readback'], flush=True)


if __name__ == '__main__':
    check()
