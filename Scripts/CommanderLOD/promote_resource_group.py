"""Copy one approved resource group to formal storage; no table switch here."""
import hashlib
import json
import sys
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
ART = ROOT / 'ArtSource/CommanderLOD_20261005'
sys.path.insert(0, str(ROOT / 'Scripts/CommanderLOD'))
from common import REVIEW_PACKAGE

approval = json.loads((ART / 'approval_B.json').read_text(encoding='utf8'))
assert approval['approval_B'] == 'approved'
assert approval['content_sha256'] == '761bc5edd06b08c598771943b46ebcbd7bc8285b42d345e234fea0907d02783d'
name = commander_lod_arguments['unit']
group = next(g for g in json.loads((ART / 'Reports/resource_groups.json').read_text(encoding='utf8'))['groups'] if g['name'] == name)
base = '/Game/GuLiStrike/Commander/Units/' + name + '/LOD_3Tier_v1'
lib = unreal.EditorAssetLibrary
edit = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
static = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
skeletal = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
data_lib = unreal.SubobjectDataBlueprintFunctionLibrary
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert editor.get_game_world() is None
opts = unreal.AssetRegistryDependencyOptions(include_hard_package_references=True,
    include_soft_package_references=True, include_searchable_names=False,
    include_soft_management_references=False, include_hard_management_references=False)
owner = 'CommanderLOD.3Tier.v1.' + name
sources = {}
mapping = {}

def package(path):
    return path.split('.')[0]

def is_render_dependency(asset):
    return isinstance(asset, (unreal.MaterialInterface, unreal.MaterialFunction,
        unreal.Texture2D, unreal.MaterialParameterCollection))

def used_slots(mesh):
    return sorted({static.get_lod_material_slot(mesh, lod, section)
        for lod in range(3) for section in range(mesh.get_num_sections(lod))})

def add(source):
    key = package(source)
    if key in sources:
        return
    asset = lib.load_asset(key)
    assert asset, key
    sources[key] = asset
    if key.startswith(REVIEW_PACKAGE + '/' + name + '/'):
        suffix = key[len(REVIEW_PACKAGE + '/' + name):]
        target = base + suffix
        if isinstance(asset, unreal.Blueprint):
            target = base + '/BP_' + name + '_3Tier'
    elif name == 'DefaultSoldier' and key.startswith('/Game/GuLiStrike/Robots/RSGMech/'):
        target = base + key[len('/Game/GuLiStrike/Robots/RSGMech'):]
        if target.endswith('/MI_Pioneer_LOD3'):
            target = target.rsplit('/', 1)[0] + '/MI_Pioneer_LOD2'
    else:
        target = base + '/Dependencies/' + key[len('/Game/'):]
    assert target not in mapping.values(), (key, target)
    mapping[key] = target
    if isinstance(asset, unreal.StaticMesh):
        dependencies = [asset.static_materials[i].material_interface.get_path_name()
            for i in used_slots(asset) if asset.static_materials[i].material_interface]
    elif isinstance(asset, unreal.SkeletalMesh):
        dependencies = [s.material_interface.get_path_name() for s in asset.materials if s.material_interface]
    else:
        dependencies = [str(p) for p in registry.get_dependencies(key, opts)]
    for dependency in dependencies:
        if not dependency.startswith('/Game/'):
            continue
        child = lib.load_asset(dependency)
        assert child, dependency
        if package(dependency).startswith(REVIEW_PACKAGE + '/' + name + '/') or is_render_dependency(child):
            add(dependency)

for source in lib.list_assets(REVIEW_PACKAGE + '/' + name, True, False):
    add(source)
for binding in group['component_bindings']:
    for material in binding['materials']:
        if material:
            add(material)

copies = {}
report = dict(success=False, name=name, id=group['id'], approval_B='approved',
    approved_content_sha256=approval['content_sha256'], formal_root=base,
    state='preparing', switched=False, assets=[], models=[], animation=None)
output = ART / ('Reports/formal_prepared_' + name + '.json')

def mapped_object(asset):
    if asset is None:
        return None
    key = package(asset.get_path_name())
    return copies.get(key, asset)

def save(asset):
    lib.set_metadata_tag(asset, 'GuLi.Owner', owner)
    lib.set_metadata_tag(asset, 'GuLi.ApprovedVersion', approval['version'])
    lib.set_metadata_tag(asset, 'GuLi.ApprovedSHA256', approval['content_sha256'])
    lib.set_metadata_tag(asset, 'GuLi.CommanderLOD', 'LOD0 Near; LOD1 Middle; LOD2 Far; B approved')
    assert lib.save_loaded_asset(asset, False), asset.get_path_name()

try:
    for key, source in sources.items():
        target = mapping[key]
        if lib.does_asset_exist(target):
            asset = lib.load_asset(target)
            assert lib.get_metadata_tag(asset, 'GuLi.Owner') == owner, ('unowned target', target)
            assert lib.get_metadata_tag(asset, 'GuLi.ApprovedSHA256') == approval['content_sha256']
        else:
            lib.make_directory(target.rsplit('/', 1)[0])
            asset = lib.duplicate_asset(key, target)
            assert asset, (key, target)
            lib.set_metadata_tag(asset, 'GuLi.SourceAsset', source.get_path_name())
            save(asset)
        copies[key] = asset

    # First fix nested functions, then material graphs; all approved expressions stay intact.
    for kind in [unreal.MaterialFunction, unreal.Material]:
        for key, asset in copies.items():
            if not isinstance(asset, kind):
                continue
            expressions = [o for o in unreal.ObjectIterator(unreal.MaterialExpression) if o.get_outer() == asset]
            for expression in expressions:
                if isinstance(expression, unreal.MaterialExpressionMaterialFunctionCall):
                    function = expression.get_editor_property('material_function')
                    expression.set_editor_property('material_function', mapped_object(function))
                try:
                    texture = expression.get_editor_property('texture')
                except Exception:
                    texture = None
                if texture is not None:
                    expression.set_editor_property('texture', mapped_object(texture))
                if isinstance(expression, unreal.MaterialExpressionCollectionParameter):
                    expression.set_editor_property('collection', mapped_object(expression.get_editor_property('collection')))
            if isinstance(asset, unreal.MaterialFunction):
                edit.update_material_function(asset)
            else:
                edit.recompile_material(asset)
            save(asset)
    for key, asset in copies.items():
        if isinstance(asset, unreal.MaterialInstanceConstant):
            edit.set_material_instance_parent(asset, mapped_object(asset.parent))
            params = list(asset.texture_parameter_values)
            for parameter in params:
                parameter.set_editor_property('parameter_value', mapped_object(parameter.parameter_value))
            asset.set_editor_property('texture_parameter_values', params)
            save(asset)

    for key, asset in copies.items():
        source = sources[key]
        if isinstance(asset, unreal.StaticMesh):
            indices = used_slots(source)
            selected = []
            for index in indices:
                original_slot = source.static_materials[index]
                slot = original_slot.copy()
                slot.set_editor_property('material_interface', mapped_object(original_slot.material_interface))
                slot.set_editor_property('overlay_material_interface', mapped_object(original_slot.get_editor_property('overlay_material_interface')))
                selected.append(slot)
            asset.set_editor_property('static_materials', selected)
            for lod in range(3):
                for section in range(source.get_num_sections(lod)):
                    old_index = static.get_lod_material_slot(source, lod, section)
                    static.set_lod_material_slot(asset, indices.index(old_index), lod, section)
            assert static.get_lod_count(asset) == 3
            assert [asset.get_num_triangles(i) for i in range(3)] == [source.get_num_triangles(i) for i in range(3)]
            assert list(static.get_lod_screen_sizes(asset)) == list(static.get_lod_screen_sizes(source))
            assert [static.get_num_uv_channels(asset, i) for i in range(3)] == [static.get_num_uv_channels(source, i) for i in range(3)]
            component = unreal.new_object(unreal.StaticMeshComponent)
            component.set_static_mesh(source)
            for socket in component.get_all_socket_names():
                original = source.find_socket(socket)
                actual = asset.find_socket(socket)
                assert actual and original.relative_location == actual.relative_location
                assert original.relative_rotation == actual.relative_rotation and original.relative_scale == actual.relative_scale
            report['models'].append(dict(candidate=source.get_path_name(), formal=asset.get_path_name(), lod_count=3,
                triangles=[asset.get_num_triangles(i) for i in range(3)],
                sections=[asset.get_num_sections(i) for i in range(3)],
                material_slots=[s.material_interface.get_path_name() for s in asset.static_materials],
                screen_sizes=list(static.get_lod_screen_sizes(asset)),
                uv_channels=[static.get_num_uv_channels(asset, i) for i in range(3)],
                lod0_geometry_operation='Native duplication; no geometric, UV, normal or socket authoring operation.'))
            save(asset)
        elif isinstance(asset, unreal.SkeletalMesh):
            slots = list(asset.materials)
            for slot in slots:
                slot.set_editor_property('material_interface', mapped_object(slot.material_interface))
            asset.set_editor_property('materials', slots)
            assert skeletal.get_lod_count(asset) == 3
            assert asset.skeleton == source.skeleton and asset.physics_asset == source.physics_asset
            report['models'].append(dict(candidate=source.get_path_name(), formal=asset.get_path_name(), lod_count=3,
                skeleton=asset.skeleton.get_path_name(), physics_asset=asset.physics_asset.get_path_name(),
                bone_count=len(unreal.SkeletonService.list_bones(asset.get_path_name())),
                lod0_geometry_operation='Native duplication; approved skin weights and reference pose unchanged.'))
            save(asset)
        elif isinstance(asset, unreal.GuLiVATDefinition):
            for field in ['bone_position', 'bone_rotation', 'hit_material', 'wreck_material', 'phase_material']:
                asset.set_editor_property(field, mapped_object(asset.get_editor_property(field)))
            assert asset.is_valid_definition()
            clips = asset.get_editor_property('clips')
            bones = asset.get_editor_property('bones')
            assert len(clips) == len(source.get_editor_property('clips')) and len(bones) == len(source.get_editor_property('bones'))
            report['animation'] = dict(asset=asset.get_path_name(), valid=True, bones=len(bones),
                clips=[str(c.get_editor_property('name')) for c in clips],
                textures=[asset.get_editor_property(f).get_path_name() for f in ['bone_position', 'bone_rotation']])
            save(asset)

    for key, asset in copies.items():
        if not isinstance(asset, unreal.Blueprint):
            continue
        target = package(asset.get_path_name())
        changed = set()
        for handle in sub.k2_gather_subobject_data_for_blueprint(asset):
            obj = data_lib.get_object(data_lib.get_data(handle))
            if not isinstance(obj, unreal.MeshComponent) or obj.get_path_name() in changed:
                continue
            assert obj.get_path_name().startswith(target + '.'), obj.get_path_name()
            skin = isinstance(obj, unreal.SkeletalMeshComponent)
            field = 'skeletal_mesh_asset' if skin else 'static_mesh'
            mesh = obj.get_editor_property(field)
            if mesh:
                obj.set_editor_property(field, mapped_object(mesh))
            for index, material in enumerate(obj.get_materials()):
                if material:
                    obj.set_material(index, mapped_object(material))
            changed.add(obj.get_path_name())
        unreal.BlueprintEditorLibrary.compile_blueprint(asset)
        save(asset)
        actor = actors.spawn_actor_from_class(asset.generated_class(), unreal.Vector(0, 0, -60000), transient=True)
        try:
            actual_bindings = []
            for component in actor.get_components_by_class(unreal.MeshComponent):
                if not component.is_visible():
                    continue
                field = 'skeletal_mesh_asset' if isinstance(component, unreal.SkeletalMeshComponent) else 'static_mesh'
                mesh = component.get_editor_property(field)
                if not mesh:
                    continue
                expected = next(c for c in group['component_bindings'] if c['component'] == component.get_name())
                assert mesh == mapped_object(lib.load_asset(expected['mesh']))
                relative = unreal.MathLibrary.make_relative_transform(component.get_world_transform(), actor.get_actor_transform())
                assert max(abs(a-b) for a, b in zip(relative.translation.to_tuple(), expected['location'])) < .01
                assert max(abs(a-b) for a, b in zip(relative.scale3d.to_tuple(), expected['scale'])) < .001
                wanted_materials = [mapped_object(lib.load_asset(p)).get_path_name() if p else None for p in expected['materials']]
                actual_materials = [m.get_path_name() if m else None for m in component.get_materials()]
                assert actual_materials == wanted_materials, (component.get_name(), actual_materials, wanted_materials)
                actual_bindings.append(dict(component=component.get_name(), mesh=mesh.get_path_name(), materials=actual_materials))
            assert len(actual_bindings) == 8
            report['component_bindings'] = actual_bindings
            report['presentation_class'] = asset.generated_class().get_path_name()
        finally:
            actors.destroy_actor(actor)

    for asset in copies.values():
        save(asset)
    registry.scan_paths_synchronous([base], force_rescan=True)
    for key, asset in copies.items():
        dependencies = [str(p) for p in (registry.get_dependencies(package(asset.get_path_name()), opts) or [])]
        assert not any(p.startswith(REVIEW_PACKAGE + '/') for p in dependencies), (asset.get_path_name(), dependencies)
        report['assets'].append(dict(source=sources[key].get_path_name(), formal=asset.get_path_name(),
            class_name=asset.get_class().get_name(), dependencies=dependencies))
    report['model_asset'] = copies[package(group['candidate_models'][0])].get_path_name() if group['original_model'] else ''
    report['vat_definition'] = report['animation']['asset'] if report['animation'] else ''
    report['runtime_fields_available'] = hasattr(unreal, 'GuLiVertexVATLOD')
    report['state'] = 'prepared_verified'
    if name == 'BiZhiMao':
        report['state'] = 'art_prepared_native_configuration_pending'
        report['vertex_configuration'] = str(ART / 'BiZhiMao/vertex_metadata.json')
    report['success'] = True
except Exception:
    report['error'] = traceback.format_exc()
    raise
finally:
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True, name=name,
    state=report['state'], asset_count=len(report['assets']), formal_root=base,
    model_asset=report['model_asset'], vat_definition=report['vat_definition'],
    presentation_class=report.get('presentation_class', ''), formal_references_switched=False)))
