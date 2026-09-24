"""Move staged card asset roots into /Game/Assets/card in the live UE editor.

The source folders are copied onto disk at their original package paths before this
script runs. Using EditorAssetLibrary.rename_directory performs an Unreal Content
Browser move so package references are updated instead of moving .uasset files by
filesystem operations.
"""

import collections
import json
import traceback

import unreal


REPORT_PATH = "D:/UE5.7/test1/Saved/card_asset_migration_report.json"
MOVES = [
    ("/Game/RewardCards", "/Game/Assets/card/RewardCards"),
    ("/Game/ParallaxCardMaterial", "/Game/Assets/card/ParallaxCardMaterial"),
]
EXPECTED_COUNTS = {
    "/Game/RewardCards": 46,
    "/Game/ParallaxCardMaterial": 58,
}


def package_names(packages):
    return sorted(package.get_path_name() for package in packages if package)


def scan(registry, paths):
    registry.scan_paths_synchronous(paths, True)


def inventory(registry, path):
    assets = list(registry.get_assets_by_path(path, recursive=True))
    classes = collections.Counter(str(asset.asset_class_path.asset_name) for asset in assets)
    return {
        "asset_count": len(assets),
        "classes": dict(sorted(classes.items())),
        "packages": sorted({str(asset.package_name) for asset in assets}),
    }


def dependency_options():
    options = unreal.AssetRegistryDependencyOptions()
    for name in (
        "include_editor_only_package_references",
        "include_game_package_references",
        "include_hard_management_references",
        "include_hard_package_references",
        "include_searchable_names",
        "include_soft_management_references",
        "include_soft_package_references",
    ):
        try:
            options.set_editor_property(name, True)
        except Exception:
            pass
    return options


def repair_reward_name_collision(registry, move_result):
    """Preserve both source packages whose exported object is named T_Obj_01."""
    source_obj = "/Game/RewardCards/Textures/T_Obj_01"
    destination_obj = "/Game/Assets/card/RewardCards/Textures/T_Obj_01"
    destination_gjm = "/Game/Assets/card/RewardCards/Textures/T_Gjm_01"

    if not unreal.EditorAssetLibrary.does_asset_exist(source_obj):
        raise RuntimeError("Expected remaining source texture is missing: " + source_obj)
    if not unreal.EditorAssetLibrary.does_asset_exist(destination_obj):
        raise RuntimeError("Expected first colliding target texture is missing: " + destination_obj)
    if unreal.EditorAssetLibrary.does_asset_exist(destination_gjm):
        raise RuntimeError("Collision repair target already exists: " + destination_gjm)

    options = dependency_options()
    destination_referencers = registry.get_referencers(destination_obj, options) or []
    if destination_referencers:
        raise RuntimeError(
            "Unexpected referencers to first colliding target texture: "
            + ", ".join(sorted(str(item) for item in destination_referencers))
        )

    if not unreal.EditorAssetLibrary.rename_asset(destination_obj, destination_gjm):
        raise RuntimeError("Could not restore the T_Gjm_01 package name")

    # A rename can leave a redirector at destination_obj. It has no referencers and
    # must be removed before the actual T_Obj_01 asset can claim that package path.
    registry.scan_paths_synchronous(["/Game/Assets/card/RewardCards/Textures"], True)
    if unreal.EditorAssetLibrary.does_asset_exist(destination_obj):
        if not unreal.EditorAssetLibrary.delete_asset(destination_obj):
            raise RuntimeError("Could not remove the temporary T_Obj_01 redirector")

    if not unreal.EditorAssetLibrary.rename_asset(source_obj, destination_obj):
        raise RuntimeError("Could not move the actual T_Obj_01 texture")

    move_result["collision_repair"] = {
        "reason": "T_Gjm_01.uasset exports an object also named T_Obj_01",
        "restored_package": destination_gjm,
        "moved_package": destination_obj,
    }


report = {
    "moves": [],
    "preflight": {},
    "postflight": {},
    "errors": [],
}

try:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    all_paths = [path for pair in MOVES for path in pair]
    scan(registry, all_paths)

    dirty_content = package_names(
        unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    )
    dirty_maps = package_names(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    report["preflight"]["dirty_content"] = dirty_content
    report["preflight"]["dirty_maps"] = dirty_maps
    report["preflight"]["inventory"] = {
        path: inventory(registry, path) for path in all_paths
    }

    if dirty_content or dirty_maps:
        raise RuntimeError("Editor has dirty packages; migration aborted to avoid saving unrelated work")

    for source, destination in MOVES:
        scan(registry, [source, destination])
        source_count = inventory(registry, source)["asset_count"]
        destination_count = inventory(registry, destination)["asset_count"]
        expected_count = EXPECTED_COUNTS[source]
        move_result = {
            "source": source,
            "destination": destination,
            "before_source_count": source_count,
            "before_destination_count": destination_count,
            "rename_directory": None,
            "save_directory": False,
        }
        report["moves"].append(move_result)

        if source_count == 0 and destination_count == expected_count:
            move_result["state"] = "already_migrated"
        elif source_count == expected_count and destination_count == 0:
            move_result["state"] = "full_directory_move"
            move_result["rename_directory"] = bool(
                unreal.EditorAssetLibrary.rename_directory(source, destination)
            )
            scan(registry, [source, destination])
            source_count = inventory(registry, source)["asset_count"]
            destination_count = inventory(registry, destination)["asset_count"]
            if (
                source == "/Game/RewardCards"
                and source_count == 1
                and destination_count == expected_count - 1
            ):
                repair_reward_name_collision(registry, move_result)
            elif not move_result["rename_directory"]:
                raise RuntimeError(
                    "Unreal directory move failed: {} -> {}".format(source, destination)
                )
        elif (
            source == "/Game/RewardCards"
            and source_count == 1
            and destination_count == expected_count - 1
        ):
            move_result["state"] = "resume_known_name_collision"
            repair_reward_name_collision(registry, move_result)
        else:
            raise RuntimeError(
                "Unexpected migration state for {}: source={}, destination={}, expected={}".format(
                    source, source_count, destination_count, expected_count
                )
            )

        scan(registry, [source, destination])
        move_result["after_source_count"] = inventory(registry, source)["asset_count"]
        move_result["after_destination_count"] = inventory(registry, destination)["asset_count"]
        if move_result["after_source_count"] != 0:
            raise RuntimeError("Source assets remain after move: " + source)
        if move_result["after_destination_count"] != expected_count:
            raise RuntimeError("Destination asset count mismatch: " + destination)

        move_result["save_directory"] = bool(
            unreal.EditorAssetLibrary.save_directory(
                destination, only_if_is_dirty=False, recursive=True
            )
        )
        if not move_result["save_directory"]:
            raise RuntimeError("Saving moved assets failed: " + destination)

    scan(registry, all_paths)
    report["postflight"]["inventory"] = {
        path: inventory(registry, path) for path in all_paths
    }

    old_roots = tuple(source for source, _ in MOVES)
    unloaded = []
    stale_dependencies = {}
    options = dependency_options()
    for _, destination in MOVES:
        for package_name in report["postflight"]["inventory"][destination]["packages"]:
            if unreal.EditorAssetLibrary.load_asset(package_name) is None:
                unloaded.append(package_name)
            dependencies = registry.get_dependencies(package_name, options) or []
            stale = sorted(
                str(dependency)
                for dependency in dependencies
                if str(dependency).startswith(old_roots)
            )
            if stale:
                stale_dependencies[package_name] = stale

    report["postflight"]["unloaded_assets"] = unloaded
    report["postflight"]["dependencies_on_old_roots"] = stale_dependencies
    report["postflight"]["dirty_content"] = package_names(
        unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    )
    report["postflight"]["dirty_maps"] = package_names(
        unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    )
except Exception:
    report["errors"].append(traceback.format_exc())

with open(REPORT_PATH, "w", encoding="utf-8") as stream:
    json.dump(report, stream, ensure_ascii=False, indent=2)

print("CARD_ASSET_MIGRATION_REPORT_WRITTEN")
