"""Import the authored outpost and update the existing building resource in place.

Run with a normal source-built UnrealEditor process and -ExecutePythonScript,
outside an MCP task-graph callback. This preserves the existing mesh object path,
updates only the Outpost catalog row, and never saves unrelated editor packages.
"""
from pathlib import Path
import hashlib
import json
import math
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
SOURCE = ROOT/'ArtSource/Buildings/OutpostMonument'
FBX = SOURCE/'Exports/SM_OutpostMonument_v01.fbx'
BASE = '/Game/GuLiStrike/Buildings'
MESH_PATH = BASE+'/Meshes/SM_OutpostPlaceholder'
SOURCE_MESH_PATH = BASE+'/Meshes/Source/SM_OutpostMonument_v01'
MATERIAL_PATH = BASE+'/Materials/M_OutpostConcrete'
TEXTURE_DIR = BASE+'/Textures/Outpost'
CATALOG_PATH = BASE+'/DA_GuLiBuildingCatalog'
BACKUP_PATH = BASE+'/Legacy/SM_OutpostCube20m_PreMonument'
OWNER = 'GuLiStrike.OutpostMonument.20260905'
EXPECTED_CM = (7103.08685, 6619.80515, 30000.0)


def vec(value):
    return [float(x) for x in value.to_tuple()]


def dimensions(mesh):
    return vec(mesh.get_bounds().box_extent*2)


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        raise RuntimeError('Unable to save '+asset.get_path_name())


def meta(asset, key, value):
    unreal.EditorAssetLibrary.set_metadata_tag(asset, key, str(value))


def import_task(filename, target, options=None, factory=None):
    folder, name = target.rsplit('/', 1)
    unreal.EditorAssetLibrary.make_directory(folder)
    task = unreal.AssetImportTask()
    for key, value in {
        'filename':str(filename), 'destination_path':folder,
        'destination_name':name, 'automated':True, 'async_':False,
        'replace_existing':True, 'replace_existing_settings':True, 'save':False,
    }.items():
        task.set_editor_property(key, value)
    if options is not None:
        task.set_editor_property('options', options)
    if factory is not None:
        task.set_editor_property('factory', factory)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    paths = list(task.get_editor_property('imported_object_paths'))
    if not paths:
        raise RuntimeError('Import returned no assets: '+str(filename))
    asset = unreal.load_asset(target)
    if asset is None:
        raise RuntimeError('Importer did not produce the requested asset: '+target+'; got '+repr(paths))
    return asset


def build_material():
    textures = {}
    for key, source_name, compression, srgb in (
        ('base', 'T_OutpostConcrete_BaseColor', unreal.TextureCompressionSettings.TC_DEFAULT, True),
        ('roughness', 'T_OutpostConcrete_Roughness', unreal.TextureCompressionSettings.TC_GRAYSCALE, False),
        ('normal', 'T_OutpostConcrete_NormalDX', unreal.TextureCompressionSettings.TC_NORMALMAP, False),
    ):
        filename = SOURCE/'Textures'/(source_name+'.png')
        target = TEXTURE_DIR+'/'+source_name
        texture = import_task(filename, target)
        texture.set_editor_property('srgb', srgb)
        texture.set_editor_property('compression_settings', compression)
        if key == 'normal':
            texture.set_editor_property('flip_green_channel', False)
        meta(texture, 'GuLi.Outpost.Owner', OWNER)
        save(texture)
        textures[key] = texture
    material = unreal.load_asset(MATERIAL_PATH)
    if material is None:
        folder, name = MATERIAL_PATH.rsplit('/', 1)
        unreal.EditorAssetLibrary.make_directory(folder)
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, folder, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError('Unable to create concrete material')
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property('two_sided', False)
    for key, y, sampler, prop, output in (
        ('base', -230, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, unreal.MaterialProperty.MP_BASE_COLOR, 'RGB'),
        ('roughness', 20, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, unreal.MaterialProperty.MP_ROUGHNESS, 'R'),
        ('normal', 270, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, unreal.MaterialProperty.MP_NORMAL, 'RGB'),
    ):
        node = unreal.MaterialEditingLibrary.create_material_expression(
            material, unreal.MaterialExpressionTextureSampleParameter2D, -380, y)
        node.set_editor_property('parameter_name', {'base':'ConcreteBaseColor','roughness':'ConcreteRoughness','normal':'ConcreteNormal'}[key])
        node.set_editor_property('texture', textures[key])
        node.set_editor_property('sampler_type', sampler)
        if not unreal.MaterialEditingLibrary.connect_material_property(node, output, prop):
            raise RuntimeError('Unable to connect concrete '+key)
    meta(material, 'GuLi.Outpost.Owner', OWNER)
    meta(material, 'GuLi.Outpost.TileMetres', 4)
    unreal.MaterialEditingLibrary.recompile_material(material)
    save(material)
    return material, textures


def import_mesh(material):
    mesh = unreal.load_asset(MESH_PATH)
    before = dimensions(mesh) if mesh else None
    digest = hashlib.sha256(FBX.read_bytes()).hexdigest()
    same_source = mesh is not None and unreal.EditorAssetLibrary.get_metadata_tag(
        mesh, 'GuLi.Outpost.FBX_SHA256') == digest
    if not same_source:
        if mesh and not unreal.EditorAssetLibrary.does_asset_exist(BACKUP_PATH):
            unreal.EditorAssetLibrary.make_directory(BACKUP_PATH.rsplit('/', 1)[0])
            backup = unreal.EditorAssetLibrary.duplicate_asset(MESH_PATH, BACKUP_PATH)
            if backup is None:
                raise RuntimeError('Unable to retain the original placeholder')
            save(backup)
        options = unreal.FbxImportUI()
        for key, value in {
            'automated_import_should_detect_type':False,
            'mesh_type_to_import':unreal.FBXImportType.FBXIT_STATIC_MESH,
            'original_import_type':unreal.FBXImportType.FBXIT_STATIC_MESH,
            'import_mesh':True, 'import_as_skeletal':False,
            'import_animations':False, 'import_materials':False,
            'import_textures':False, 'override_full_name':True,
        }.items():
            options.set_editor_property(key, value)
        data = options.get_editor_property('static_mesh_import_data')
        for key, value in {
            'combine_meshes':True, 'convert_scene':True,
            'convert_scene_unit':True, 'force_front_x_axis':False,
            'import_uniform_scale':1.0, 'import_translation':unreal.Vector(0,0,0),
            'import_rotation':unreal.Rotator(0,0,0),
            'auto_generate_collision':False, 'generate_lightmap_u_vs':False,
            'build_nanite':False, 'import_mesh_lods':False,
            'normal_import_method':unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS,
            'transform_vertex_to_absolute':True,
        }.items():
            data.set_editor_property(key, value)
        # Import a clean source asset: FBX's existing-object branch redirects to
        # the old asset's reimport handler before applying the supplied options.
        # The project baker then replaces geometry without breaking references.
        source_mesh = import_task(FBX, SOURCE_MESH_PATH, options, unreal.FbxFactory())
        source_subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        source_settings = source_subsystem.get_lod_build_settings(source_mesh, 0)
        source_settings.set_editor_property('use_full_precision_u_vs', True)
        source_subsystem.set_lod_build_settings(source_mesh, 0, source_settings)
        source_mesh.set_material(0, material)
        save(source_mesh)
        if mesh is None:
            mesh = unreal.EditorAssetLibrary.duplicate_asset(SOURCE_MESH_PATH, MESH_PATH)
        mesh.set_editor_property('static_materials', source_mesh.get_editor_property('static_materials'))
        if not unreal.GuLiBuildingAssetBakingLibrary.bake_render_geometry_scale(
                source_mesh, mesh, unreal.Vector(1,1,1), False):
            raise RuntimeError('Unable to transfer imported outpost geometry')
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if subsystem is None:
        raise RuntimeError('This importer requires the full editor StaticMeshEditorSubsystem')
    settings = subsystem.get_lod_build_settings(mesh, 0)
    settings.set_editor_property('build_scale3d', unreal.Vector(1,1,1))
    settings.set_editor_property('use_full_precision_u_vs', True)
    # Do not inherit the Engine cube's 2x distance-field resolution: on this
    # 300m mesh it needlessly expands the field from about 2.8MB to 20MB.
    settings.set_editor_property('distance_field_resolution_scale', 1.0)
    settings.set_editor_property('generate_lightmap_u_vs', False)
    settings.set_editor_property('recompute_normals', False)
    settings.set_editor_property('recompute_tangents', False)
    subsystem.set_lod_build_settings(mesh, 0, settings)
    nanite = mesh.get_editor_property('nanite_settings')
    nanite.set_editor_property('enabled', False)
    mesh.set_editor_property('nanite_settings', nanite)
    mesh.set_editor_property('light_map_coordinate_index', 1)
    mesh.set_editor_property('light_map_resolution', 256)
    mesh.set_material(0, material)
    actual = dimensions(mesh)
    if not all(math.isclose(a,b,rel_tol=.0005,abs_tol=1) for a,b in zip(actual,EXPECTED_CM)):
        raise RuntimeError('Unexpected imported dimensions: '+repr(actual))
    # Standalone uses get a matching simple box; placed-building visuals remain
    # NoCollision and continue using the authoritative root box from the catalog.
    subsystem.remove_collisions(mesh)
    subsystem.add_simple_collisions(mesh, unreal.ScriptingCollisionShapeType.BOX)
    meta(mesh, 'GuLi.Outpost.Owner', OWNER)
    meta(mesh, 'GuLi.Outpost.FBX_SHA256', digest)
    meta(mesh, 'GuLi.Outpost.Source', 'ArtSource/Buildings/OutpostMonument/Exports/SM_OutpostMonument_v01.fbx')
    meta(mesh, 'GuLi.Outpost.HeightMetres', 300)
    meta(mesh, 'GuLi.Building.BakedScale', 1)
    save(mesh)
    return mesh, before


def update_catalog(mesh):
    catalog = unreal.load_asset(CATALOG_PATH)
    if catalog is None:
        raise RuntimeError('Existing building catalog is missing')
    definitions = list(catalog.get_editor_property('definitions'))
    rows = []
    updated = 0
    bounds = mesh.get_bounds()
    for definition in definitions:
        if definition.get_editor_property('type') == unreal.GuLiBuildingType.OUTPOST:
            definition.set_editor_property('mesh', mesh)
            definition.set_editor_property('collision_extent', bounds.box_extent)
            definition.set_editor_property('visual_offset', unreal.Vector(
                -bounds.origin.x, -bounds.origin.y, bounds.box_extent.z-bounds.origin.z))
            updated += 1
        rows.append({'type':str(definition.get_editor_property('type')),
                     'mesh':definition.get_editor_property('mesh').get_path_name(),
                     'extent':vec(definition.get_editor_property('collision_extent')),
                     'visual_offset':vec(definition.get_editor_property('visual_offset'))})
    if updated != 1:
        raise RuntimeError('Expected exactly one outpost definition, found '+str(updated))
    catalog.set_editor_property('definitions', definitions)
    meta(catalog, 'GuLi.Outpost.Owner', OWNER)
    save(catalog)
    return rows


def _install_impl(update_building_catalog=True):
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
        raise RuntimeError('End PIE before replacing the building resource')
    material, textures = build_material()
    mesh, before = import_mesh(material)
    rows = update_catalog(mesh) if update_building_catalog else []
    report = {
        'success':True, 'mesh':mesh.get_path_name(), 'material':material.get_path_name(),
        'old_dimensions_cm':before, 'dimensions_cm':dimensions(mesh),
        'origin_cm':vec(mesh.get_bounds().origin), 'catalog_rows':rows,
        'textures':{key:value.get_path_name() for key,value in textures.items()},
        'original_placeholder_backup':BACKUP_PATH,
        'imported_source_mesh':SOURCE_MESH_PATH,
        'asset_path_preserved':True, 'nanite':False, 'lightmap_uv_channel':1,
        'distance_field_resolution_scale':1.0,
    }
    output = ROOT/'outputs/outpost-monument-20260905/ue_import_report.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    return report


def install(update_building_catalog=True):
    # The legacy factories respect the supplied FBX options and stable target
    # name. Keep this override scoped to the import, never change project config.
    cvar = 'Interchange.FeatureFlags.Import.Enable'
    enabled = unreal.SystemLibrary.get_console_variable_int_value(cvar)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, cvar+' 0')
    try:
        return _install_impl(update_building_catalog)
    finally:
        unreal.SystemLibrary.execute_console_command(world, cvar+' '+str(enabled))


if __name__ == '__main__':
    print(json.dumps(install(),ensure_ascii=False,indent=2))
