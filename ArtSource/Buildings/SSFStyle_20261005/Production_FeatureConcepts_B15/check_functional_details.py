"""Verify the B15 UVs and material connections from the saved project."""

import hashlib
import json
import sys
from pathlib import Path

import bpy
import numpy as np


sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bake_functional_details import B14, BASELINE, DEST, ROOT, STYLE_PROPERTY, UV, read_json
from bake_surface_detail import geometry_digest, linked
from check_surface_detail import INK_ATTRIBUTES, attribute_values, check_material


def reads_node(input_socket, target):
    pending = [link.from_node for link in input_socket.links]
    seen = set()
    while pending:
        node = pending.pop()
        if node == target:
            return True
        if node not in seen:
            pending.extend(link.from_node for socket in node.inputs for link in socket.links)
            seen.add(node)
    return False


def check():
    assert Path(bpy.data.filepath) == DEST
    manifest_path = ROOT / 'B15_Functional_Detail_Manifest.json'
    manifest = read_json(manifest_path)
    baseline = read_json(B14 / 'B14_Surface_Detail_Manifest.json')['models']
    source = read_json(BASELINE / 'B6_Material_Manifest.json')['models']
    assert set(manifest['models']) == set(source) - {'AirBase'}
    meshes = [bpy.data.objects[row['objects'][0]].data.name for row in source.values()]
    with bpy.data.libraries.load(str(B14 / 'SSF_A5_SurfaceDetail_B14.blend')) as (_, loaded):
        loaded.meshes = meshes
    total_faces = 0
    for (key, row), old_mesh in zip(source.items(), loaded.meshes, strict=True):
        scene = bpy.data.scenes[row['scene']]
        bpy.context.window.scene = scene
        bpy.context.view_layer.update()
        blue, red = (bpy.data.objects[name] for name in row['objects'])
        mesh = blue.data
        assert red.data == mesh
        assert len(mesh.polygons) == row['triangles'] == len(old_mesh.polygons)
        old_render_uv = next(layer.name for layer in old_mesh.uv_layers if layer.active_render)
        assert next(layer.name for layer in mesh.uv_layers if layer.active_render) == old_render_uv
        for name in INK_ATTRIBUTES:
            assert np.array_equal(attribute_values(mesh.attributes[name]), attribute_values(old_mesh.attributes[name]))
        classes = [item.value for item in mesh.attributes['SSF Original Material Index'].data]
        copy = blue.copy()
        copy.data = mesh.copy()
        old = blue.copy()
        old.data = old_mesh
        if UV in copy.data.uv_layers:
            copy.data.uv_layers.remove(copy.data.uv_layers[UV])
        assert geometry_digest(copy, classes) == geometry_digest(old, classes)
        assert (ROOT / '..' / 'Production_FeatureConcepts_B14' / baseline[key]['atlas']).is_file()
        group = bpy.data.node_groups[f'{key} | A5 Surface Detail']
        base_atlas = next(node.image for node in group.nodes if node.type == 'TEX_IMAGE')
        assert hashlib.sha256(base_atlas.packed_file.data).hexdigest() == baseline[key]['atlas_sha256']
        materials = {slot.material for obj in (blue, red) for slot in obj.material_slots if 'Finish' in slot.material}
        for material in materials:
            check_material(material, group)
        if key == 'AirBase':
            assert mesh.uv_layers.get(UV) is None
            continue
        info = manifest['models'][key]
        width, height = info['size']
        atlas_path = ROOT / 'Textures' / info['atlas']
        assert hashlib.sha256(atlas_path.read_bytes()).hexdigest() == info['atlas_sha256']
        assert len(info['features']) == {'CloningCenter': 8, 'CommandCenter': 1, 'MilitaryFactory': 2,
                                        'Reactor': 1, 'StrategyCenter': 3}[key]
        new_materials = set(info['materials'])
        for material in materials:
            applied = [node for node in material.node_tree.nodes if node.type == 'TEX_IMAGE' and
                       node.label == 'A5 functional details registered to the LOD0 part']
            assert len(applied) == (material.name in new_materials)
            if not applied:
                continue
            tex = applied[0]
            assert linked(tex.inputs['Vector']).uv_map == UV
            image = tex.image
            assert image.packed_file.data == atlas_path.read_bytes()
            assert image.alpha_mode == 'CHANNEL_PACKED' and image.colorspace_settings.name == 'Non-Color'
            assert list(image.size) == [width, height]
            values = np.empty(width * height * 4, dtype=np.float32)
            image.pixels.foreach_get(values)
            values = values.reshape(height, width, 4)
            assert not values[-8:, :8].any()
            expected = {'CloningCenter': (0, 1), 'CommandCenter': (0,), 'MilitaryFactory': (2,),
                        'Reactor': (2,), 'StrategyCenter': (0, 2)}[key]
            assert tuple(np.flatnonzero(values[..., :3].max(axis=(0, 1)) > .2)) == expected
            assert values[..., 3].max() == 1.
            detail = next(node for node in material.node_tree.nodes if node.label == 'A5 reference detail before lighting')
            assert reads_node(detail.inputs['Base Color'], tex)
        coords = np.empty(len(mesh.loops) * 2, dtype=np.float32)
        mesh.uv_layers[UV].uv.foreach_get('vector', coords)
        coords = coords.reshape(-1, 2)
        assert np.isfinite(coords).all() and (coords >= 0).all() and (coords <= 1).all()
        neutral = [.5 / width, 1 - .5 / height]
        touched = [face for face in mesh.polygons if not np.allclose(coords[face.loop_start], neutral)]
        assert len(touched) == info['faces']
        assert all(classes[face.index] == 0 and blue.material_slots[face.material_index].material.name in new_materials
                   for face in touched)
        info['readback'] = {'shared_faction_mesh': True, 'same_atlas_on_both_teams': True, 'original_uvs_and_ink': True}
        total_faces += len(touched)
        print(key, len(touched), 'functional UV faces', flush=True)
    assert len(bpy.data.scenes) == 7 and all(scene[STYLE_PROPERTY] == 1 and scene.render.engine == 'BLENDER_EEVEE'
                                            for scene in bpy.data.scenes)
    assert sum(row['triangles'] for row in source.values()) == 24746
    manifest['readback'] = {'functional_faces': total_faces, 'triangles_per_faction': 24746,
                            'active_lighting_bands': [.40, .68, 1.], 'b14_textures_packed': 6}
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
    print('B15 READBACK COMPLETE', manifest['readback'], flush=True)


if __name__ == '__main__':
    check()
