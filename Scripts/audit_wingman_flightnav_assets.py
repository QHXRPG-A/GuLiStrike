"""Read-only editor audit for the wingman source mesh and FlightNav target maps.

Run through Scripts/ue_exec.py while the Unreal Editor is open.  The script uses
the Asset Registry and public UObject properties only; it never reads package
files as binary data.
"""

import json
import os

import unreal


PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUTPUT_PATH = os.path.join(
    PROJECT_DIR, "TestResults", "WingmanPlan", "AssetAudit", "asset_audit.json"
)
TARGET_MAPS = (
    "/Game/Maps/LVL_CommanderMassPrototype",
    "/Game/Maps/LVL_Main",
    "/Game/Maps/LVL_ShipTest",
)
SEARCH_PATHS = (
    "/Game/Assets/Arma/fly-01",
    "/Game/GuLiStrike/Wingman",
    "/Game/GuLiStrike/Navigation/Baked",
)


def _asset_class(asset_data):
    try:
        return str(asset_data.asset_class_path.asset_name)
    except Exception:
        return str(asset_data.asset_class)


def _asset_record(asset_data):
    return {
        "asset_name": str(asset_data.asset_name),
        "package_name": str(asset_data.package_name),
        "package_path": str(asset_data.package_path),
        "class": _asset_class(asset_data),
    }


def _vector(value):
    return {"x": value.x, "y": value.y, "z": value.z}


def _mesh_record(asset_data):
    record = _asset_record(asset_data)
    record["load_ok"] = False
    try:
        asset = asset_data.get_asset()
        record["load_ok"] = asset is not None
        if asset is None:
            return record

        bounds = asset.get_bounding_box() if hasattr(asset, "get_bounding_box") else None
        if bounds is not None:
            extent = bounds.max - bounds.min
            dimensions = _vector(extent)
            dominant_axis = max(dimensions, key=lambda key: abs(dimensions[key]))
            record["local_bounds"] = {
                "min": _vector(bounds.min),
                "max": _vector(bounds.max),
                "dimensions": dimensions,
                "dominant_axis": dominant_axis.upper(),
            }

        if isinstance(asset, unreal.StaticMesh):
            record["mesh_kind"] = "StaticMesh"
            try:
                record["lod_count"] = unreal.EditorStaticMeshLibrary.get_lod_count(asset)
            except Exception as exc:
                record["lod_error"] = str(exc)
            try:
                record["simple_collision_count"] = (
                    unreal.EditorStaticMeshLibrary.get_simple_collision_count(asset)
                )
            except Exception as exc:
                record["collision_error"] = str(exc)
            try:
                body_setup = asset.get_editor_property("body_setup")
                record["has_body_setup"] = body_setup is not None
                if body_setup is not None:
                    record["collision_trace_flag"] = str(
                        body_setup.get_editor_property("collision_trace_flag")
                    )
            except Exception as exc:
                record["body_setup_error"] = str(exc)
            try:
                static_materials = list(asset.get_editor_property("static_materials"))
                record["material_slots"] = len(static_materials)
                record["materials"] = [
                    {
                        "slot_name": str(material.get_editor_property("material_slot_name")),
                        "imported_slot_name": str(
                            material.get_editor_property("imported_material_slot_name")
                        ),
                        "asset": (
                            str(material.get_editor_property("material_interface").get_path_name())
                            if material.get_editor_property("material_interface") is not None
                            else None
                        ),
                    }
                    for material in static_materials
                ]
                record["missing_material_slots"] = sum(
                    material["asset"] is None for material in record["materials"]
                )
            except Exception as exc:
                record["material_error"] = str(exc)
        elif isinstance(asset, unreal.SkeletalMesh):
            record["mesh_kind"] = "SkeletalMesh"
            try:
                record["material_slots"] = len(asset.get_editor_property("materials"))
            except Exception as exc:
                record["material_error"] = str(exc)
    except Exception as exc:
        record["load_error"] = str(exc)
    return record


registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.wait_for_completion()

result = {
    "schema": 1,
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "project_dir": PROJECT_DIR,
    "maps": [],
    "search_paths": {},
    "mesh_candidates": [],
}

editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
editor_world = editor_subsystem.get_editor_world() if editor_subsystem else None
game_world = editor_subsystem.get_game_world() if editor_subsystem else None
dirty_packages = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
dirty_packages.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
result["editor_state"] = {
    "editor_world": str(editor_world.get_path_name()) if editor_world else None,
    "pie_or_sie_active": game_world is not None,
    "dirty_packages": sorted({str(package.get_name()) for package in dirty_packages}),
}

for package_name in TARGET_MAPS:
    assets = registry.get_assets_by_package_name(unreal.Name(package_name), True)
    result["maps"].append(
        {
            "package_name": package_name,
            "exists": len(assets) > 0,
            "assets": [_asset_record(asset) for asset in assets],
        }
    )

all_candidates = []
for path in SEARCH_PATHS:
    assets = registry.get_assets_by_path(unreal.Name(path), True)
    records = [_asset_record(asset) for asset in assets]
    result["search_paths"][path] = records
    for asset in assets:
        class_name = _asset_class(asset)
        asset_name = str(asset.asset_name).lower()
        package_name = str(asset.package_name).lower()
        if class_name in ("StaticMesh", "SkeletalMesh") or any(
            token in asset_name or token in package_name
            for token in ("space_mouse", "wingman", "fly-01")
        ):
            all_candidates.append(asset)

seen_packages = set()
for candidate in all_candidates:
    package_name = str(candidate.package_name)
    if package_name in seen_packages:
        continue
    seen_packages.add(package_name)
    result["mesh_candidates"].append(_mesh_record(candidate))

os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
with open(OUTPUT_PATH, "w", encoding="utf-8") as output_file:
    json.dump(result, output_file, ensure_ascii=False, indent=2, sort_keys=True)

unreal.log("Wingman/FlightNav asset audit written to " + OUTPUT_PATH)
