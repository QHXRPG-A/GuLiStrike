"""Read back mark samples from the packed PNG on their exact saved LOD0 faces."""

import json
import sys
from collections import Counter
from pathlib import Path

import bpy
import numpy as np


sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bake_surface_marks import B13, B15, B16, BASELINE, DEST, MARKS, REST, ROOT, SCALE, STYLE_PROPERTY, UV_NAME, read_json, reference_projection
from bake_surface_marks import original_parts, projected_ownership
from check_registered_detail import verify_images, verify_original_mesh, verify_uv


def verify_marks(obj, row, camera, image):
    mesh = obj.data
    projected = reference_projection([attr.vector.copy() for attr in mesh.attributes[REST].data], camera)
    points = projected[:, :2] * SCALE
    classes = [0 if value.value == 0 else 2 for value in mesh.attributes['SSF Original Material Index'].data]
    owner = projected_ownership(projected, mesh.polygons, classes)
    parts = original_parts(obj)
    width, height = image.size
    values = np.empty(width * height * 4, dtype=np.float32)
    image.pixels.foreach_get(values)
    values = values.reshape(height, width, 4)[::-1]
    max_error = 0.
    probe_count = 0
    for group in row['marks']:
        for mark in group['marks']:
            assert mark['b17_host_fraction'] == 1. and mark['texture_samples'] > 0
            assert len(mark['atlas_probes']) == 12
            for probe in mark['atlas_probes']:
                x, y = probe['screen_sample']
                index = probe['face']
                assert owner[y, x] == index
                face = mesh.polygons[index]
                part = Counter(parts[i] for i in face.vertices).most_common(1)[0][0]
                assert part == group['host']
                assert obj.material_slots[face.material_index].material.get('Finish', '').startswith(group['finish'])
                uv = np.array(mesh.uv_layers[UV_NAME].uv[face.loop_start].vector)
                uv[1] = 1 - uv[1]
                offset = uv * (width, height) - points[face.vertices[0]]
                atlas_x, atlas_y = np.floor(np.array([x, y]) + .5 + offset).astype(int)
                assert 0 <= atlas_x < width and 0 <= atlas_y < height
                pixel = values[atlas_y, atlas_x, :2]
                max_error = max(max_error, float(np.max(np.abs(pixel - probe['rg']))))
                probe_count += 1
    assert max_error < .004, max_error
    return probe_count, round(max_error, 6)


def check():
    assert Path(bpy.data.filepath) == DEST
    path = ROOT / 'B17_Surface_Marks_Manifest.json'
    manifest = read_json(path)
    records = manifest['models']
    source = read_json(BASELINE / 'B6_Material_Manifest.json')['models']
    edges = read_json(B13 / 'B13_Edge_Ink_Manifest.json')['models']
    functional = read_json(B15 / 'B15_Functional_Detail_Manifest.json')['models']
    baseline = read_json(B16 / 'B16_Registered_Detail_Manifest.json')['models']
    assert set(records) == set(MARKS)
    with bpy.data.libraries.load(str(B16 / 'SSF_A5_RegisteredDetail_B16.blend')) as (_, previous):
        previous.meshes = [bpy.data.objects[row['objects'][0]].data.name for row in source.values()]
    probes = 0
    for (key, model), old_mesh in zip(source.items(), previous.meshes, strict=True):
        blue, red = (bpy.data.objects[name] for name in model['objects'])
        assert blue.data == red.data and len(blue.data.polygons) == model['triangles']
        classes = verify_original_mesh(blue, old_mesh)
        row = records.get(key, baseline[key])
        camera = edges[key]['reference_camera']
        uv_error = verify_uv(blue.data, row, camera, classes)
        bpy.context.window.scene = bpy.data.scenes[model['scene']]
        bpy.context.view_layer.update()
        group = bpy.data.node_groups[f'{key} | A5 Surface Detail']
        materials = {slot.material for obj in (blue, red) for slot in obj.material_slots if 'Finish' in slot.material}
        verify_images(key, row, materials, group, functional, ROOT if key in records else B16)
        if key in records:
            assert row['reference_sha256'] == baseline[key]['reference_sha256']
            image = next(node.image for node in group.nodes if node.type == 'TEX_IMAGE')
            count, error = verify_marks(blue, row, camera, image)
            row['readback'] = {'checked_atlas_samples': count, 'max_rg_quantization_error': error,
                               'max_uv_roundtrip_px': uv_error}
            probes += count
            print(key, count, 'source mark atlas samples read back', flush=True)
    assert probes == 132
    assert len(bpy.data.scenes) == 7 and all(scene[STYLE_PROPERTY] == 1 and scene.render.engine == 'BLENDER_EEVEE'
                                            for scene in bpy.data.scenes)
    total = sum(row['triangles'] for row in source.values())
    assert total == 24746
    manifest['readback'] = {'mark_count': 11, 'atlas_samples': probes, 'triangles_per_faction': total,
                            'b15_functional_textures_preserved': len(functional), 'active_lighting_bands': [.4, .68, 1.]}
    previews = []
    for scene in sorted(bpy.data.scenes, key=lambda row: row.name):
        bpy.context.window.scene = scene
        bpy.ops.render.render(write_still=True)
        preview = Path(bpy.path.abspath(scene.render.filepath))
        with preview.open('rb') as handle:
            assert handle.read(8) == b'\x89PNG\r\n\x1a\n'
        assert preview.stat().st_size > 1000
        previews.append(preview.relative_to(ROOT).as_posix())
    manifest['previews'] = previews
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('B17 READBACK COMPLETE', manifest['readback'], flush=True)


if __name__ == '__main__':
    check()
