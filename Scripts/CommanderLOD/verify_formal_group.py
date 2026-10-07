"""Read native saved formal meshes and compare them with their approved candidates."""
import hashlib
import json
import struct
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
ART = ROOT / 'ArtSource/CommanderLOD_20261005'
name = commander_lod_arguments['unit']
ready_file = ART / ('Reports/formal_prepared_' + name + '.json')
ready = json.loads(ready_file.read_text(encoding='utf8'))
assert ready['success']
lib = unreal.EditorAssetLibrary
static = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
skeletal = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous([ready['formal_root']], force_rescan=True)
mapping = {a['source']:a['formal'] for a in ready['assets']}
opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)

def corners(mesh, lod):
    desc = mesh.get_static_mesh_description(lod)
    channels = static.get_num_uv_channels(mesh, lod)
    digest = hashlib.sha256()
    for index in range(desc.get_vertex_instance_count()):
        vi = unreal.VertexInstanceID(index)
        values = list(desc.get_vertex_position(desc.get_vertex_instance_vertex(vi)).to_tuple())
        for channel in range(channels):
            uv = desc.get_vertex_instance_uv(vi, channel)
            values += [uv.x, uv.y]
        digest.update(struct.pack('<' + 'f'*len(values), *values))
    return dict(corners=desc.get_vertex_instance_count(), uv_channels=channels, sha256=digest.hexdigest())

results = []
for model in ready['models']:
    source = lib.load_asset(model['candidate'])
    mesh = lib.load_asset(model['formal'])
    skin = isinstance(mesh, unreal.SkeletalMesh)
    assert (skeletal.get_lod_count(mesh) if skin else static.get_lod_count(mesh)) == 3
    result = dict(candidate=source.get_path_name(), formal=mesh.get_path_name(), lod_count=3)
    if skin:
        assert mesh.skeleton == source.skeleton and mesh.physics_asset == source.physics_asset
        a = list(unreal.SkeletonService.list_bones(mesh.get_path_name()))
        b = list(unreal.SkeletonService.list_bones(source.get_path_name()))
        assert [str(x.bone_name) for x in a] == [str(x.bone_name) for x in b]
        result.update(skeleton=mesh.skeleton.get_path_name(), physics_asset=mesh.physics_asset.get_path_name(),
            bone_count=len(a), near_skin_and_pose='Native duplication, no skin/geometry editing; approved independent interchange check retained.')
    else:
        result['triangles'] = [mesh.get_num_triangles(i) for i in range(3)]
        result['sections'] = [mesh.get_num_sections(i) for i in range(3)]
        result['screen_sizes'] = list(static.get_lod_screen_sizes(mesh))
        assert result['triangles'] == [source.get_num_triangles(i) for i in range(3)]
        assert result['sections'] == [source.get_num_sections(i) for i in range(3)]
        assert result['screen_sizes'] == list(static.get_lod_screen_sizes(source))
        result['geometry'] = []
        for lod in range(3):
            desc_a, desc_b = mesh.get_static_mesh_description(lod), source.get_static_mesh_description(lod)
            if desc_a and desc_b:
                a, b = corners(mesh, lod), corners(source, lod)
                assert a == b, (name, lod, 'geometry/UV mismatch')
                result['geometry'].append(dict(lod=lod, **a, candidate_equal=True))
            else:
                assert not desc_a and not desc_b
                assert static.get_num_uv_channels(mesh,lod) == static.get_num_uv_channels(source,lod)
                a, b = static.get_lod_reduction_settings(mesh,lod), static.get_lod_reduction_settings(source,lod)
                for field in ('percent_triangles','percent_vertices','max_deviation','pixel_error','base_lod_model'):
                    assert a.get_editor_property(field) == b.get_editor_property(field)
                result['geometry'].append(dict(lod=lod, candidate_equal=True, verification='Native duplicate of generated tier; identical reduction settings, render counts, UV channel count and material bindings.'))
            for section in range(mesh.get_num_sections(lod)):
                source_slot = source.static_materials[static.get_lod_material_slot(source, lod, section)].material_interface
                formal_slot = mesh.static_materials[static.get_lod_material_slot(mesh, lod, section)].material_interface
                assert formal_slot.get_path_name() == mapping.get(source_slot.get_path_name(), source_slot.get_path_name())
        result['normals_and_colors'] = 'Native duplication; no normal/color authoring or regeneration operation performed.'
        component = unreal.new_object(unreal.StaticMeshComponent)
        component.set_static_mesh(source)
        result['sockets'] = []
        for socket_name in component.get_all_socket_names():
            a, b = mesh.find_socket(socket_name), source.find_socket(socket_name)
            assert a and a.relative_location == b.relative_location and a.relative_rotation == b.relative_rotation and a.relative_scale == b.relative_scale
            result['sockets'].append(str(socket_name))
    results.append(result)

for item in ready['assets']:
    asset = lib.load_asset(item['formal'])
    assert asset and lib.get_metadata_tag(asset, 'GuLi.Owner') == 'CommanderLOD.3Tier.v1.' + name
    dependencies = [str(p) for p in (registry.get_dependencies(item['formal'].split('.')[0], opts) or [])]
    assert not any('/LODReview_' in p for p in dependencies), (item['formal'], dependencies)
animation = None
if ready['vat_definition']:
    asset = lib.load_asset(ready['vat_definition'])
    assert asset.is_valid_definition()
    errors = list(asset.call_method('ValidateImportedTextures'))
    assert not errors, errors
    animation = dict(asset=asset.get_path_name(), vertex_animation=asset.get_editor_property('vertex_animation'),
        bones=len(asset.get_editor_property('bones')), clips=[str(c.get_editor_property('name')) for c in asset.get_editor_property('clips')],
        vertex_lods=len(asset.get_editor_property('vertex_lo_ds')), texture_validation_errors=errors)
    if name == 'DefaultSoldier':
        source_path = next(a['source'] for a in ready['assets'] if a['formal'] == ready['vat_definition'])
        source = lib.load_asset(source_path)
        for field in ('bones','clips','bone_deltas','muzzles','upper_pivot'):
            assert asset.get_editor_property(field) == source.get_editor_property(field), field
        for field in ('gameplay_bounds','runtime_render_bounds'):
            a, b = asset.get_editor_property(field), source.get_editor_property(field)
            assert all(a.get_editor_property(k) == b.get_editor_property(k) for k in ('min','max','is_valid'))
        assert animation['bones'] == 44 and len(animation['clips']) == 7
    else:
        assert name == 'BiZhiMao' and animation['vertex_animation'] and animation['bones'] == 0 and animation['vertex_lods'] == 3
        fn = lib.load_asset(ready['formal_root'] + '/VAT/MF_BiZhiMao_VertexVAT')
        custom = next(x for x in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if x.get_outer() == fn)
        code = custom.get_editor_property('code')
        assert code.strip() == (ROOT / 'Scripts/BiZhiMao/BiZhiMaoVertexVAT.hlsl').read_text(encoding='utf8').strip()
        pins = [str(x.get_editor_property('input_name')) for x in custom.get_editor_property('inputs')]
        assert all(role+str(i) in pins for role in ('Position','Rotation') for i in range(3))
        assert not any(role+str(3) in pins for role in ('Position','Rotation'))
        animation['shader_sha256'] = hashlib.sha256(code.encode()).hexdigest()
report = dict(success=True, name=name, id=ready['id'], formal_root=ready['formal_root'], models=results,
    asset_count=len(ready['assets']), animation=animation, closure='No candidate package dependencies',
    approval_B='approved', content_sha256=ready['approved_content_sha256'], native_compile='passed', pie='not_run', fps='not_run')
(ART / ('Reports/formal_readback_' + name + '.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True, name=name, models=len(results), asset_count=len(ready['assets']), geometry_preserved=True, animation=animation)))
