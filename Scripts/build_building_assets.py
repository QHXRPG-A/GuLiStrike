"""Build the approved GuLiStrike MVP building assets in an editor process.

The script is intentionally idempotent and only saves assets under its two managed
target roots. Fab source packages are loaded read-only and never modified.
"""

import json
import math
import runpy
import traceback
from pathlib import Path

import unreal


OWNER = "GuLiStrike.BuildingMVP.20260903"
MISSILE_SOURCE_ROOT = "/Game/Fab/Missile_Turret"
MISSILE_TARGET_ROOT = "/Game/Assets/Props/Buildings/Missile_Turret"
SENTRY_SOURCE_ROOT = "/Game/Fab/Stylized_Turrets_-_Tower_Defense"
SENTRY_TARGET_ROOT = "/Game/Assets/Props/Buildings/Stylized_Turrets_Tower_Defense"
GAMEPLAY_ROOT = "/Game/GuLiStrike/Buildings"

MISSILE_MESH = (
    f"{MISSILE_TARGET_ROOT}/missile_turret/StaticMeshes/missile_turret"
)
MISSILE_SOURCE_MESH = (
    f"{MISSILE_SOURCE_ROOT}/missile_turret/StaticMeshes/missile_turret"
)
SENTRY_MESH = f"{SENTRY_TARGET_ROOT}/Stylized_Turrets_A_a"
SENTRY_SOURCE_MESH = f"{SENTRY_SOURCE_ROOT}/Stylized_Turrets_A_a"
SENTRY_MATERIAL = f"{SENTRY_TARGET_ROOT}/Stylized_Turrets_A_mat"
SENTRY_TEXTURE = f"{SENTRY_TARGET_ROOT}/Stylized_Turrets_A"
OUTPOST_MESH = f"{GAMEPLAY_ROOT}/Meshes/SM_OutpostPlaceholder"
OUTPOST_MATERIAL = f"{GAMEPLAY_ROOT}/Materials/M_OutpostConcrete"
PREVIEW_MATERIAL = f"{GAMEPLAY_ROOT}/Materials/M_BuildingPlacementPreview"
CATALOG_PATH = f"{GAMEPLAY_ROOT}/DA_GuLiBuildingCatalog"

EXPECTED_DIMENSIONS = {
    "missile": (2519.46, 2728.75, 3520.77),
    "sentry": (1935.79, 3866.78, 1675.19),
    "outpost": (7103.08685, 6619.80515, 30000.0),
}


class BuildingAssetError(RuntimeError):
    pass


def _asset(path):
    value = unreal.load_asset(path)
    if value is None:
        raise BuildingAssetError(f"Unable to load asset: {path}")
    return value


def _copy_directory_once(source, target, sentinel_asset):
    if unreal.EditorAssetLibrary.does_asset_exist(sentinel_asset):
        return False
    if unreal.EditorAssetLibrary.does_directory_exist(target):
        if not unreal.EditorAssetLibrary.delete_directory(target):
            raise BuildingAssetError(
                f"Unable to remove incomplete managed directory: {target}"
            )
    parent = target.rsplit("/", 1)[0]
    unreal.EditorAssetLibrary.make_directory(parent)
    if not unreal.EditorAssetLibrary.duplicate_directory(source, target):
        raise BuildingAssetError(f"Unable to duplicate {source} to {target}")
    if not unreal.EditorAssetLibrary.save_directory(target, False, True):
        raise BuildingAssetError(f"Unable to save duplicated directory: {target}")
    if not unreal.EditorAssetLibrary.does_asset_exist(sentinel_asset):
        raise BuildingAssetError(
            f"Duplicated directory is missing sentinel asset: {sentinel_asset}"
        )
    return True


def _save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        raise BuildingAssetError(f"Unable to save {asset.get_path_name()}")


def _set_metadata(asset, key, value):
    unreal.EditorAssetLibrary.set_metadata_tag(asset, key, str(value))


def _mesh_dimensions(mesh):
    bounds = mesh.get_bounds()
    return (
        float(bounds.box_extent.x) * 2.0,
        float(bounds.box_extent.y) * 2.0,
        float(bounds.box_extent.z) * 2.0,
    )


def _mesh_definition_geometry(mesh):
    bounds = mesh.get_bounds()
    origin = bounds.origin
    extent = bounds.box_extent
    return {
        "extent": unreal.Vector(float(extent.x), float(extent.y), float(extent.z)),
        "visual_offset": unreal.Vector(
            -float(origin.x),
            -float(origin.y),
            float(extent.z) - float(origin.z),
        ),
    }


def _near_dimensions(actual, expected, tolerance=12.0):
    return all(math.isclose(a, e, abs_tol=tolerance, rel_tol=0.002) for a, e in zip(actual, expected))


def _configure_mesh_build_scale(source_mesh, mesh, scale, nanite_enabled):
    baking_library = getattr(unreal, "GuLiBuildingAssetBakingLibrary", None)
    if baking_library is None:
        raise BuildingAssetError("GuLi building asset baking reflection type is unavailable")
    bake_result = baking_library.bake_render_geometry_scale(
        source_mesh,
        mesh,
        unreal.Vector(float(scale), float(scale), float(scale)),
        bool(nanite_enabled),
    )
    if not bake_result:
        raise BuildingAssetError(f"Unable to bake {mesh.get_path_name()}")

    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    lod_count = int(mesh.get_num_lods())
    if lod_count <= 0:
        raise BuildingAssetError(f"Mesh has no LODs: {mesh.get_path_name()}")
    # The commandlet intentionally has no StaticMeshEditorSubsystem. The reflected
    # C++ baker owns the write contract; when the subsystem exists in a full editor,
    # keep this immediate read-back as an additional diagnostic. BuildingAssetTests
    # always verify every serialized SourceModel directly in C++.
    if subsystem is not None:
        for lod_index in range(lod_count):
            applied = subsystem.get_lod_build_settings(mesh, lod_index)
            applied_scale = applied.get_editor_property("build_scale3d")
            if not all(
                math.isclose(float(component), float(scale), abs_tol=0.001)
                for component in (applied_scale.x, applied_scale.y, applied_scale.z)
            ):
                raise BuildingAssetError(
                    f"Unable to set LOD{lod_index} build settings: {mesh.get_path_name()}"
                )

    nanite = mesh.get_editor_property("nanite_settings")
    if bool(nanite.get_editor_property("enabled")) != bool(nanite_enabled):
        raise BuildingAssetError(
            f"Unable to set Nanite state: {mesh.get_path_name()}"
        )
    _set_metadata(mesh, "GuLi.Building.Owner", OWNER)
    _set_metadata(mesh, "GuLi.Building.BakedScale", scale)
    _save(mesh)


def _rebind_copied_materials(source_mesh, target_mesh, source_root, target_root):
    """Point duplicated mesh slots at duplicated materials, never Fab originals."""
    source_slots = list(source_mesh.get_editor_property("static_materials"))
    target_slots = list(target_mesh.get_editor_property("static_materials"))
    if len(source_slots) != len(target_slots):
        raise BuildingAssetError(
            f"Static material slot count changed while duplicating {target_mesh.get_path_name()}"
        )
    for slot_index, source_slot in enumerate(source_slots):
        source_material = source_slot.get_editor_property("material_interface")
        if source_material is None:
            raise BuildingAssetError(
                f"Source material slot {slot_index} is empty: {source_mesh.get_path_name()}"
            )
        source_path = source_material.get_path_name().split(".", 1)[0]
        if not source_path.startswith(source_root + "/"):
            target_material = source_material
        else:
            target_path = target_root + source_path[len(source_root):]
            target_material = _asset(target_path)
        target_mesh.set_material(slot_index, target_material)
    _save(target_mesh)


def _create_or_load_material(path, blend_mode, shading_model, two_sided=False):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        material = _asset(path)
    else:
        directory, name = path.rsplit("/", 1)
        unreal.EditorAssetLibrary.make_directory(directory)
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, directory, unreal.Material, unreal.MaterialFactoryNew()
        )
        if material is None:
            raise BuildingAssetError(f"Unable to create material: {path}")
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    material.set_editor_property("blend_mode", blend_mode)
    material.set_editor_property("shading_model", shading_model)
    material.set_editor_property("two_sided", bool(two_sided))
    _set_metadata(material, "GuLi.Building.Owner", OWNER)
    return material


def _build_preview_material():
    material = _create_or_load_material(
        PREVIEW_MATERIAL,
        unreal.BlendMode.BLEND_TRANSLUCENT,
        unreal.MaterialShadingModel.MSM_UNLIT,
        True,
    )
    tint = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, -320, -60
    )
    tint.set_editor_property("parameter_name", "TintColor")
    tint.set_editor_property("default_value", unreal.LinearColor(0.04, 1.0, 0.12, 1.0))
    opacity = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -320, 100
    )
    opacity.set_editor_property("parameter_name", "Opacity")
    opacity.set_editor_property("default_value", 0.38)
    if not unreal.MaterialEditingLibrary.connect_material_property(
        tint, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    ):
        raise BuildingAssetError("Unable to connect preview TintColor")
    if not unreal.MaterialEditingLibrary.connect_material_property(
        opacity, "", unreal.MaterialProperty.MP_OPACITY
    ):
        raise BuildingAssetError("Unable to connect preview Opacity")
    unreal.MaterialEditingLibrary.recompile_material(material)
    _save(material)
    return material


def _build_outpost_assets():
    # Rebuilding the catalog must retain the authored 300 m monument instead of
    # silently restoring the old Engine Cube. Run asset creation from a normal
    # editor callback/startup script, outside the MCP task-graph callback.
    project_dir = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    importer = runpy.run_path(str(project_dir / "Scripts/import_outpost_monument.py"),
                              run_name="outpost_import")
    result = importer["install"](update_building_catalog=False)
    return _asset(result["material"]), _asset(result["mesh"])


def _repair_sentry_material(mesh):
    material = _asset(SENTRY_MATERIAL)
    texture = _asset(SENTRY_TEXTURE)
    # UE 5.7.4's editor implementation applies this change but always returns false.
    # Verify the effective value instead of trusting that return value.
    unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(
        material, "DiffuseColorMap", texture
    )
    unreal.MaterialEditingLibrary.update_material_instance(material)
    applied_texture = (
        unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(
            material, "DiffuseColorMap"
        )
    )
    if applied_texture != texture:
        raise BuildingAssetError(
            "DiffuseColorMap did not resolve to copied Stylized_Turrets_A texture"
        )
    mesh.set_material(0, material)
    _set_metadata(material, "GuLi.Building.Owner", OWNER)
    _set_metadata(material, "GuLi.Building.Repair", "DiffuseColorMap=Stylized_Turrets_A")
    _save(material)
    _save(mesh)
    return material, texture


def _create_or_load_catalog(preview_material, missile_mesh, sentry_mesh, outpost_mesh):
    catalog_class = getattr(unreal, "GuLiBuildingCatalog", None)
    definition_class = getattr(unreal, "GuLiBuildingDefinition", None)
    building_enum = getattr(unreal, "GuLiBuildingType", None)
    if catalog_class is None or definition_class is None or building_enum is None:
        raise BuildingAssetError("Compiled GuLi building reflection types are unavailable")

    if unreal.EditorAssetLibrary.does_asset_exist(CATALOG_PATH):
        catalog = _asset(CATALOG_PATH)
    else:
        unreal.EditorAssetLibrary.make_directory(GAMEPLAY_ROOT)
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", catalog_class)
        catalog = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            CATALOG_PATH.rsplit("/", 1)[-1],
            GAMEPLAY_ROOT,
            catalog_class,
            factory,
        )
        if catalog is None:
            raise BuildingAssetError(f"Unable to create catalog: {CATALOG_PATH}")

    definitions = []
    rows = (
        (building_enum.MISSILE_TURRET, "防空炮", missile_mesh),
        (building_enum.SENTRY_TURRET, "哨戒炮", sentry_mesh),
        (building_enum.OUTPOST, "据点", outpost_mesh),
    )
    for building_type, display_name, mesh in rows:
        geometry = _mesh_definition_geometry(mesh)
        definition = definition_class()
        definition.set_editor_property("type", building_type)
        definition.set_editor_property("display_name", display_name)
        definition.set_editor_property("mesh", mesh)
        definition.set_editor_property("collision_extent", geometry["extent"])
        definition.set_editor_property("visual_offset", geometry["visual_offset"])
        definitions.append(definition)

    catalog.set_editor_property("definitions", definitions)
    catalog.set_editor_property("preview_material", preview_material)
    _set_metadata(catalog, "GuLi.Building.Owner", OWNER)
    _set_metadata(catalog, "GuLi.Building.Schema", 1)
    _save(catalog)
    return catalog


report = {"success": False, "owner": OWNER}
try:
    report["copied_missile_directory"] = _copy_directory_once(
        MISSILE_SOURCE_ROOT, MISSILE_TARGET_ROOT, MISSILE_MESH
    )
    report["copied_sentry_directory"] = _copy_directory_once(
        SENTRY_SOURCE_ROOT, SENTRY_TARGET_ROOT, SENTRY_MESH
    )

    missile_source = _asset(MISSILE_SOURCE_MESH)
    missile = _asset(MISSILE_MESH)
    sentry = _asset(SENTRY_MESH)
    _rebind_copied_materials(
        missile_source,
        missile,
        MISSILE_SOURCE_ROOT,
        MISSILE_TARGET_ROOT,
    )
    _configure_mesh_build_scale(missile_source, missile, 12.0, True)
    sentry_material, sentry_texture = _repair_sentry_material(sentry)
    _configure_mesh_build_scale(_asset(SENTRY_SOURCE_MESH), sentry, 12.0, False)

    preview = _build_preview_material()
    outpost_material, outpost = _build_outpost_assets()
    catalog = _create_or_load_catalog(preview, missile, sentry, outpost)

    dimensions = {
        "missile": _mesh_dimensions(missile),
        "sentry": _mesh_dimensions(sentry),
        "outpost": _mesh_dimensions(outpost),
    }
    for name, actual in dimensions.items():
        if not _near_dimensions(actual, EXPECTED_DIMENSIONS[name]):
            raise BuildingAssetError(
                f"{name} dimensions {actual} do not match {EXPECTED_DIMENSIONS[name]}"
            )

    missile_materials = list(missile.get_editor_property("static_materials"))
    if len(missile_materials) != 32 or any(
        slot.get_editor_property("material_interface") is None for slot in missile_materials
    ):
        raise BuildingAssetError("Missile mesh must retain 32 non-null materials")
    if any(
        not slot.get_editor_property("material_interface")
        .get_path_name()
        .startswith(MISSILE_TARGET_ROOT + "/")
        for slot in missile_materials
    ):
        raise BuildingAssetError(
            "Missile mesh material slots must reference duplicated target materials"
        )

    report.update(
        {
            "success": True,
            "dimensions_cm": dimensions,
            "missile_material_count": len(missile_materials),
            "missile_mesh": missile.get_path_name(),
            "sentry_mesh": sentry.get_path_name(),
            "sentry_material": sentry_material.get_path_name(),
            "sentry_texture": sentry_texture.get_path_name(),
            "outpost_mesh": outpost.get_path_name(),
            "preview_material": preview.get_path_name(),
            "catalog": catalog.get_path_name(),
        }
    )
except Exception as exc:
    report["error"] = str(exc)
    report["traceback"] = traceback.format_exc()
    unreal.log_error(json.dumps(report, ensure_ascii=False, default=str))
    raise

unreal.log(json.dumps(report, ensure_ascii=False, default=str))
print(json.dumps(report, ensure_ascii=False, default=str))
