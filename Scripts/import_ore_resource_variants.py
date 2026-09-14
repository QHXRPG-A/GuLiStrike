"""Import the ore FBX delivery with a source-built UE Editor startup script.

Launch UnrealEditor.exe GuLiStrike.uproject -ExecutePythonScript=<this file>.
Do not run FBX import synchronously inside a network/MCP game-thread callback.
Only assets under /Game/GuLiStrike/Resources/Ores with this owner are updated.
"""

import hashlib
import json
import traceback
from pathlib import Path

import unreal

ROOT = Path('D:/UE5.7/test1')
BASE = '/Game/GuLiStrike/Resources/Ores'
OWNER = 'GuLiStrike.OreUEImport.v01'
TAG = 'GuLi.Ore.Owner'
SOURCE = ROOT / 'ArtSource/Resources/Ores/UEExport/export_manifest.json'
OUT = ROOT / 'outputs/ore-ue-import-20260910'
REPORT_PATH = OUT / 'ue_import_report.json'


def save(asset):
    unreal.EditorAssetLibrary.set_metadata_tag(asset, TAG, OWNER)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError('Could not save ' + asset.get_path_name())


def check_owner(path):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return None
    asset = unreal.load_asset(path)
    if not asset or unreal.EditorAssetLibrary.get_metadata_tag(asset, TAG) != OWNER:
        raise RuntimeError('Existing asset is not owned by this import: ' + path)
    return asset


def expr(material, cls, x, y, **properties):
    node = unreal.MaterialEditingLibrary.create_material_expression(material, cls, x, y)
    for name, value in properties.items():
        node.set_editor_property(name, value)
    return node


def link(source, output, target, input_name):
    normalize = lambda text: ''.join(c.lower() for c in str(text) if c.isalnum())
    inputs = unreal.MaterialEditingLibrary.get_material_expression_input_names(target)
    matches = [str(n) for n in inputs if normalize(n) == normalize(input_name)]
    if len(matches) != 1:
        raise RuntimeError('Material input missing: ' + input_name + ' from ' + str(inputs))
    if not unreal.MaterialEditingLibrary.connect_material_expressions(source, output, target, matches[0]):
        raise RuntimeError('Material connection failed: ' + input_name)


def output(node, pin, property_name):
    if not unreal.MaterialEditingLibrary.connect_material_property(node, pin, property_name):
        raise RuntimeError('Material output connection failed: ' + str(property_name))


def make_material(name, values):
    folder = BASE + '/Materials'
    mat = check_owner(folder + '/' + name)
    if mat is None:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
        if mat is None:
            raise RuntimeError('Material creation failed: ' + name)
    else:
        unreal.MaterialEditingLibrary.delete_all_material_expressions(mat)
    unreal.EditorAssetLibrary.set_metadata_tag(mat, TAG, OWNER)
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property('two_sided', False)
    mat.set_editor_property('used_with_instanced_static_meshes', True)
    crystal = name.endswith('_Crystal')
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_CLEAR_COAT if crystal else unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property('use_material_attributes', True)
    attributes = expr(mat, unreal.MaterialExpressionMakeMaterialAttributes, 180, 0)
    output(attributes, '', unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
    color = expr(mat, unreal.MaterialExpressionVertexColor, -650, -150)
    link(color, '', attributes, 'BaseColor')
    for y, key, prop in (
        (20, 'roughness', 'Roughness'),
        (130, 'metallic', 'Metallic'),
        (240, 'specular', 'Specular'),
    ):
        scalar = expr(mat, unreal.MaterialExpressionScalarParameter, -330, y,
                      parameter_name=key.title(), default_value=float(values[key]))
        link(scalar, '', attributes, prop)
    if crystal:
        strength = expr(mat, unreal.MaterialExpressionScalarParameter, -650, 370,
                        parameter_name='EmissionStrength', default_value=float(values['emission_strength']))
        weight = expr(mat, unreal.MaterialExpressionMultiply, -330, 360)
        link(color, 'A', weight, 'A')
        link(strength, '', weight, 'B')
        emission = expr(mat, unreal.MaterialExpressionMultiply, -100, 360)
        link(color, '', emission, 'A')
        link(weight, '', emission, 'B')
        link(emission, '', attributes, 'EmissiveColor')
        for y, key, prop in (
            (500, 'coat_weight', 'ClearCoat'),
            (610, 'coat_roughness', 'ClearCoatRoughness'),
        ):
            scalar = expr(mat, unreal.MaterialExpressionScalarParameter, -330, y,
                          parameter_name=key, default_value=float(values[key]))
            link(scalar, '', attributes, prop)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.set_metadata_tag(mat, 'GuLi.Ore.ColorContract', 'Vertex RGB=authored facet color; A=local glow weight')
    save(mat)
    return mat


def make_options():
    options = unreal.FbxImportUI()
    for key, value in dict(
        automated_import_should_detect_type=False, import_as_skeletal=False,
        mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH,
        original_import_type=unreal.FBXImportType.FBXIT_STATIC_MESH,
        import_mesh=True, import_animations=False, import_materials=False,
        import_textures=False, create_physics_asset=False,
    ).items():
        options.set_editor_property(key, value)
    data = options.get_editor_property('static_mesh_import_data')
    for key, value in dict(
        convert_scene=True, convert_scene_unit=True, force_front_x_axis=False,
        import_uniform_scale=1.0, combine_meshes=True, build_nanite=False,
        auto_generate_collision=True, one_convex_hull_per_ucx=True,
        generate_lightmap_u_vs=True, transform_vertex_to_absolute=True,
        remove_degenerates=True, reorder_material_to_fbx_order=True,
        vertex_color_import_option=unreal.VertexColorImportOption.REPLACE,
        normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,
    ).items():
        data.set_editor_property(key, value)
    return options


def import_unit(row, materials):
    folder = BASE + '/Meshes/' + row['ore_type']
    name = row['asset_name']
    path = folder + '/' + name
    existing = check_owner(path)
    digest = hashlib.sha256(Path(row['fbx']).read_bytes()).hexdigest()
    if digest != row['sha256']:
        raise RuntimeError('FBX differs from the export manifest: ' + row['fbx'])
    task = unreal.AssetImportTask()
    for key, value in dict(
        filename=row['fbx'], destination_path=folder, destination_name=name,
        automated=True, async_=False, replace_existing=existing is not None,
        replace_existing_settings=True, save=False,
        options=make_options(), factory=unreal.FbxFactory(),
    ).items():
        task.set_editor_property(key, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    mesh = unreal.load_asset(path)
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError('Static mesh import failed: ' + path + ' ' + str(task.imported_object_paths))
    slots = list(mesh.get_editor_property('static_materials'))
    slot_names = [str(s.get_editor_property('material_slot_name')) for s in slots]
    if slot_names != row['material_slots']:
        raise RuntimeError('Crystal/rock slot order mismatch: ' + name + ' ' + str(slot_names))
    for index, material_name in enumerate(slot_names):
        mesh.set_material(index, materials[material_name])
    mesh.set_editor_property('light_map_resolution', 128)
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    for key, value in {
        'GuLi.Ore.Type': row['ore_type'], 'GuLi.Ore.Family': str(row['family']),
        'GuLi.Ore.FamilyName': row['family_name'], 'GuLi.Ore.State': row['state'],
        'GuLi.Ore.SourceSHA256': digest, 'GuLi.Ore.SourceObject': row['name'],
        'GuLi.Ore.Pivot': 'Ground origin; centimetres',
    }.items():
        unreal.EditorAssetLibrary.set_metadata_tag(mesh, key, value)
    bounds = mesh.get_bounds()
    size = [float(bounds.box_extent.x * 2), float(bounds.box_extent.y * 2), float(bounds.box_extent.z * 2)]
    expected = [d * 100.0 for d in row['dimensions_m']]
    if any(abs(a-b) > 0.1 for a,b in zip(sorted(size[:2]), sorted(expected[:2]))) or abs(size[2]-expected[2]) > 0.1:
        raise RuntimeError('Unit/axis conversion failed: ' + name + ' ' + str(size) + ' vs ' + str(expected))
    bottom = float(bounds.origin.z - bounds.box_extent.z)
    if abs(bottom) > 0.1:
        raise RuntimeError('Ground pivot was offset: ' + name)
    triangles = mesh.get_num_triangles(0)
    if triangles != row['triangles']:
        raise RuntimeError('Triangle count changed: ' + name + ' ' + str(triangles) + ' vs ' + str(row['triangles']))
    if not subsystem.has_vertex_colors(mesh):
        raise RuntimeError('Vertex colors missing: ' + name)
    primitive_collisions = subsystem.get_simple_collision_count(mesh)
    convex_collisions = subsystem.get_convex_collision_count(mesh)
    collision_count = primitive_collisions + convex_collisions
    if collision_count <= 0:
        raise RuntimeError('No simple collision was created: ' + name)
    save(mesh)
    return mesh, {
        'name': name, 'path': mesh.get_path_name(), 'ore_type': row['ore_type'],
        'family': row['family'], 'state': row['state'], 'triangles': triangles,
        'dimensions_cm': size, 'bottom_z_cm': bottom,
        'material_slots': slot_names, 'materials': [mesh.get_material(i).get_path_name() for i in range(len(slots))],
        'has_vertex_colors': True, 'uv_channels': subsystem.get_num_uv_channels(mesh, 0),
        'simple_collisions': collision_count, 'primitive_collisions': primitive_collisions,
        'convex_collisions': convex_collisions,
        'nanite': bool(mesh.get_editor_property('nanite_settings').get_editor_property('enabled')),
        'saved': True,
    }


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    report = {'success': False, 'engine': unreal.SystemLibrary.get_engine_version(),
              'engine_directory': unreal.Paths.convert_relative_path_to_full(unreal.Paths.engine_dir()),
              'project_file': unreal.Paths.convert_relative_path_to_full(unreal.Paths.get_project_file_path()),
              'destination': BASE, 'models': [], 'materials': {}}
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    cvar = 'Interchange.FeatureFlags.Import.Enable'
    previous = unreal.SystemLibrary.get_console_variable_int_value(cvar)
    try:
        if Path(report['project_file']).resolve() != (ROOT/'GuLiStrike.uproject').resolve():
            raise RuntimeError('Wrong UE project.')
        if 'UnrealEngine-5.7' not in report['engine_directory']:
            raise RuntimeError('This delivery requires the source-built UE 5.7 editor.')
        manifest = json.loads(SOURCE.read_text(encoding='utf-8'))
        if manifest.get('owner') != OWNER or manifest.get('asset_count') != 24:
            raise RuntimeError('Unexpected export manifest.')
        for row in manifest['models']:
            check_owner(BASE + '/Meshes/' + row['ore_type'] + '/' + row['asset_name'])
        for name in manifest['materials']:
            check_owner(BASE + '/Materials/' + name)
        for folder in (BASE+'/Materials', BASE+'/Meshes/Blue', BASE+'/Meshes/Red'):
            unreal.EditorAssetLibrary.make_directory(folder)
        unreal.SystemLibrary.execute_console_command(world, cvar + ' 0')
        materials = {name: make_material(name, values) for name, values in manifest['materials'].items()}
        report['materials'] = {name: mat.get_path_name() for name, mat in materials.items()}
        meshes = []
        for row in manifest['models']:
            mesh, record = import_unit(row, materials)
            meshes.append(mesh)
            report['models'].append(record)
            REPORT_PATH.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
            unreal.log('Ore import: %d/24 %s' % (len(meshes), mesh.get_name()))
        report.update(success=True, asset_count=len(meshes), material_count=len(materials))
        unreal.EditorAssetLibrary.sync_browser_to_objects([m.get_path_name() for m in meshes])
        unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([meshes[0], meshes[12]])
        scripting = getattr(unreal, 'EditorPythonScripting', None)
        if scripting:
            scripting.set_keep_python_script_alive(True)
            report['editor_kept_open'] = True
    except Exception:
        report['error'] = traceback.format_exc()
        unreal.log_error(report['error'])
    finally:
        unreal.SystemLibrary.execute_console_command(world, cvar + ' ' + str(previous))
        report['interchange_restored_to'] = previous
        REPORT_PATH.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        unreal.log('ORE_IMPORT_RESULT ' + json.dumps({'success': report['success'], 'models': len(report['models']), 'report': str(REPORT_PATH)}))
    return report


if __name__ == '__main__':
    main()
