"""Build low LODs from explicit FBX triangles, never through a LOD copy.
The existing formal mesh is the only asset changed. No map or play session.
"""
import unreal
import json
from pathlib import Path
ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
ART = ROOT / 'ArtSource/QuadrupedMiner_20261113'
BASE = '/Game/GuLiStrike/Commander/Units/ElectromagneticMiner/QuadrupedMiner_20261113'
META = json.loads((ART / 'Reports/vat_metadata.json').read_text(encoding='utf8'))
READBACK = json.loads((ART / 'Reports/vat_readback.json').read_text(encoding='utf8'))
mesh = unreal.load_asset(BASE + '/Meshes/SM_QuadrupedMiner_VAT')
sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
for lod in range(3):
    build = sub.get_lod_build_settings(mesh, lod)
    for key, value in dict(use_full_precision_u_vs=True, remove_degenerates=False,
        recompute_normals=False, recompute_tangents=False, generate_lightmap_u_vs=False).items():
        build.set_editor_property(key, value)
    sub.set_lod_build_settings(mesh, lod, build)
# Do not retain a description backed by the asset being rebuilt. All three
# descriptions are independent mesh data created before the single build call.
descriptions = []
report = {'success': False, 'lods': [], 'source': 'explicit FBX triangles; independent descriptions for all LODs'}
for lod in range(3):
    data = json.loads((ART / f'Reports/explicit_lod{lod}.json').read_text(encoding='utf8'))
    assert data['lod'] == lod and len(data['positions']) == data['position_ids'] * 3
    desc = mesh.create_static_mesh_description()
    vertices = []
    for index in range(data['position_ids']):
        vertex = desc.create_vertex()
        desc.set_vertex_position(vertex, unreal.Vector(*data['positions'][index * 3:index * 3 + 3]))
        vertices.append(vertex)
    groups = {}
    for slot, name in enumerate(data['material_names']):
        group = desc.create_polygon_group()
        desc.set_polygon_group_material_slot_name(group, f'LOD{lod}_SourceSlot{slot}')
        groups[slot] = group
    for triangle in data['triangles']:
        # Explicitly triangulated source polygons: no procedural triangulation.
        # Mirroring Blender Y to UE Y reverses winding, just as FBX import does.
        instances = []
        for loop_index, vertex_index in reversed(list(zip(triangle['loops'], triangle['vertices']))):
            instances.append(desc.create_vertex_instance(vertices[vertex_index]))
        for corner, loop_index in enumerate(reversed(triangle['loops'])):
            for channel in range(3):
                uv = data['uvs'][channel][loop_index * 2:loop_index * 2 + 2]
                if channel == 2:
                    uv[1] = 1.0 - uv[1]
                desc.set_vertex_instance_uv(instances[corner], unreal.Vector2D(*uv), channel)
        polygon, _, _ = desc.create_polygon(groups[triangle['material']])
        desc.set_polygon_vertex_instances(polygon, instances)
    assert desc.get_vertex_count() == data['position_ids']
    assert desc.get_polygon_count() == len(data['triangles'])
    descriptions.append(desc)
    report['lods'].append({'lod': lod, 'description_vertices': desc.get_vertex_count(),
        'description_triangles': desc.get_polygon_count(), 'pre_export_triangles': data['evaluated_triangles_before_export']})
mesh.build_from_static_mesh_descriptions(descriptions, False, False)
sub.set_lod_screen_sizes(mesh, META['screen_sizes'])
assert unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)
for lod in range(3):
    row = {'lod': lod, 'ue_triangles': mesh.get_num_triangles(lod), 'ue_vertices': mesh.get_num_vertices(lod),
        'sections': mesh.get_num_sections(lod), 'material_slots': [sub.get_lod_material_slot(mesh, lod, i) for i in range(mesh.get_num_sections(lod))],
        'expected_triangles': READBACK['lods'][lod]['triangles']}
    assert row['ue_triangles'] == row['expected_triangles'], row
    report.setdefault('readback', []).append(row)
report['success'] = True
(ART / 'Reports/explicit_lod_build.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
print(json.dumps(report, ensure_ascii=False))
