"""Read-only validation for the construction sample in the live editor."""
import gc
import json
from pathlib import Path

import unreal


MAP = "/Game/Maps/LVL_CommanderMassPrototype"
ASSETS = [
    "/Game/GuLiStrike/Buildings/Construction/M_ConstructionHologram",
    "/Game/GuLiStrike/Buildings/Construction/M_ConstructionFinishGlow",
    "/Game/GuLiStrike/Buildings/Construction/M_BuildingPlacementGrid",
    "/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects",
    "/Game/GuLiStrike/Data/DT_GuLiStrikeGameTexts_Texts",
    "/Game/GuLiStrike/Buildings/Construction/NS_ConstructionComplete",
    "/Game/GuLiStrike/Buildings/Construction/NS_ConstructionTopLoop",
    "/Game/GuLiStrike/Buildings/Construction/NS_ConstructionLaser_Purple",
    "/Game/GuLiStrike/Buildings/Construction/Shapes/DA_ConstructionShapeCatalog",
]


def run():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_editor_world()
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    labels = {
        actor.get_actor_label()
        for actor in actors
        if "BuildingConstructionReview" in [str(tag) for tag in actor.tags]
    }
    required_labels = {
        "ConstructionReview_Placement",
        "ConstructionReview_Progress",
        "ConstructionReview_Mining",
    }
    required_labels.update(f"ConstructionReview_Type{i}" for i in range(1, 7))
    asset_checks = {
        path: unreal.EditorAssetLibrary.does_asset_exist(path)
        for path in ASSETS
    }
    vfx_table = unreal.load_asset(ASSETS[3])
    vfx_rows = {
        str(row)
        for row in unreal.DataTableFunctionLibrary.get_data_table_row_names(vfx_table)
    }
    required_vfx_rows = {
        "BuildingConstructionHologram",
        "BuildingConstructionFinish",
        "BuildingPlacementGrid",
        "BuildingConstructionComplete",
        "BuildingConstructionTopLoop",
        "BuildingConstructionLaser",
    }
    shapes = unreal.load_asset(ASSETS[-1]).shapes
    shape_checks = {i: bool(i in shapes and shapes[i].wall_mesh and shapes[i].base_mesh
        and any(not c.hole and len(c.points) >= 3 for c in shapes[i].contours)) for i in range(1, 7)}
    dirty = []
    try:
        dirty = [str(package) for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    except Exception:
        pass
    result = {
        "success": all(asset_checks.values())
        and required_labels.issubset(labels)
        and required_vfx_rows.issubset(vfx_rows)
        and all(shape_checks.values())
        and world.get_path_name() == MAP + ".LVL_CommanderMassPrototype"
        and editor.get_game_world() is None,
        "map": world.get_path_name(),
        "game_mode": str(world.get_world_settings().get_editor_property("default_game_mode")),
        "asset_checks": asset_checks,
        "shape_checks": shape_checks,
        "review_labels": sorted(labels),
        "required_vfx_rows": {name: name in vfx_rows for name in sorted(required_vfx_rows)},
        "dirty_content_packages": dirty,
        "play_started": editor.get_game_world() is not None,
    }
    return result


def main():
    try:
        result = run()
    except Exception as error:
        result = {"success": False, "error": str(error)}
    output = Path("D:/UE5.7/test1/outputs/construction-vfx/validation.json")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=True))


main()
gc.collect()
