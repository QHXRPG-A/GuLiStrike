"""Move the requested packs with Unreal APIs in the already running editor.

Run through Scripts/ue_exec.py, using runpy.run_path and calling preflight(),
move_pack(pack_id), or validate_pack(pack_id). No PIE, scene switching, global
save-all, native build, editor restart, or filesystem search is performed.
"""

from __future__ import annotations

from collections import Counter
import json
from pathlib import Path
import shutil
import traceback
import warnings

import unreal

warnings.filterwarnings("ignore", category=DeprecationWarning)
PROJECT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
REPORT_DIR = PROJECT / "data/ExternalAssets/migrations/20261026"
UNREGISTERED_SOURCE_PACKAGES = {
    "/Game/Mech_Constructor_Spiders/Blueprints/Pawn_Buggy_Chassis/MyCarGame",
    "/Game/Mech_Constructor_Spiders/Maps/Demo_Scene",
    "/Game/Mech_Constructor_Spiders/MyCarGame",
}
UNLOADABLE_REGISTERED_SOURCE_PACKAGES = {
    "/Game/Mech_Constructor_Spiders/Blueprints/Pawn_Buggy_Chassis/Wheel_Buggy_Tire",
}


def write_report(name: str, value: object) -> None:
    (REPORT_DIR / name).write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


def configs() -> list[dict]:
    return json.loads((REPORT_DIR / "extraction_report.json").read_text(encoding="utf-8"))


def config_for(pack_id: str) -> dict:
    return next(p for p in configs() if p["id"] == pack_id)


def dirty_packages() -> dict:
    return {
        "content": sorted(p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()),
        "maps": sorted(p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()),
    }


def editor_world() -> str:
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    return world.get_path_name() if world else ""


def inventory(root: str) -> dict:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = list(registry.get_assets_by_path(root, recursive=True, include_only_on_disk_assets=True))
    classes = Counter(str(a.asset_class_path.asset_name) for a in assets)
    return {
        "asset_count": len(assets),
        "package_count": len({str(a.package_name) for a in assets}),
        "classes": dict(sorted(classes.items())),
        "packages": sorted({str(a.package_name) for a in assets}),
        "non_redirectors": sorted({str(a.package_name) for a in assets if str(a.asset_class_path.asset_name) != "ObjectRedirector"}),
        "redirectors": sorted({str(a.package_name) for a in assets if str(a.asset_class_path.asset_name) == "ObjectRedirector"}),
        "maps": sorted({str(a.package_name) for a in assets if str(a.asset_class_path.asset_name) == "World"}),
    }


def dependency_options():
    return unreal.AssetRegistryDependencyOptions(
        include_hard_package_references=True,
        include_soft_package_references=True,
        include_searchable_names=False,
        include_hard_management_references=True,
        include_soft_management_references=True,
    )


def dependency_report(packages: list[str], target_root: str) -> dict:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    options = dependency_options()
    source_roots = [p["source_root"] for p in configs()]
    stale = {}
    missing_game = {}
    external = set()
    native_modules = set()
    plugin_mounts = set()
    by_package = {}
    for package in packages:
        dependencies = sorted(str(d) for d in (registry.get_dependencies(package, options) or []))
        by_package[package] = dependencies
        old = [d for d in dependencies if any(d.casefold().startswith(root.casefold() + "/") for root in source_roots)]
        if old:
            stale[package] = old
        missing = []
        for dependency in dependencies:
            if dependency.startswith("/Script/"):
                native_modules.add(dependency)
            elif not dependency.startswith(("/Game/", "/Engine/")):
                plugin_mounts.add(dependency.split("/")[1] if dependency.startswith("/") else dependency)
            if not dependency.casefold().startswith(target_root.casefold() + "/"):
                external.add(dependency)
            if dependency.startswith("/Game/") and not unreal.EditorAssetLibrary.does_asset_exist(dependency):
                missing.append(dependency)
        if missing:
            missing_game[package] = missing
    return {
        "old_path_dependencies": stale,
        "missing_game_dependencies": missing_game,
        "external_dependencies": sorted(external),
        "native_modules": sorted(native_modules),
        "plugin_mounts": sorted(plugin_mounts),
        "by_package": by_package,
    }


def preflight() -> dict:
    if Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.get_project_file_path())).resolve() != PROJECT / "GuLiStrike.uproject":
        raise RuntimeError("Unexpected live project")
    if unreal.EditorLevelLibrary.get_pie_worlds(False):
        raise RuntimeError("PIE is active; migration has not begun")
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    paths = [p[k] for p in configs() for k in ("source_root", "target_root")]
    registry.scan_paths_synchronous(paths, True)
    report = {"engine_version": unreal.SystemLibrary.get_engine_version(), "editor_world": editor_world(), "dirty_before": dirty_packages(), "packs": [], "passed": False}
    for config in configs():
        source = inventory(config["source_root"])
        target = inventory(config["target_root"])
        expected = sorted(e["source_package"] for e in config["entries"] if e["relative_file"].endswith((".uasset", ".umap")))
        missing_registry = sorted(set(expected) - set(source["packages"]))
        allowed_unregistered = sorted(set(expected) & UNREGISTERED_SOURCE_PACKAGES)
        entry = {"id": config["id"], "source": source, "target": target, "expected_packages_match": expected == source["packages"], "unregistered_source_packages": missing_registry}
        entry["dependencies_before"] = dependency_report(source["packages"], config["source_root"])
        report["packs"].append(entry)
        if (set(expected) != set(source["packages"]) | set(allowed_unregistered) or missing_registry != allowed_unregistered or target["asset_count"]):
            write_report("editor_preflight.json", report)
            raise RuntimeError(f"Package inventory mismatch or existing destination: {config['id']}")
    report["passed"] = True
    write_report("editor_preflight.json", report)
    print(json.dumps({"preflight_passed": True, "packages": sum(p["source"]["package_count"] for p in report["packs"]), "world": report["editor_world"], "dirty_before": report["dirty_before"]}, ensure_ascii=False))
    return report


def move_pack(pack_id: str) -> dict:
    config = config_for(pack_id)
    report = {"id": pack_id, "source_root": config["source_root"], "target_root": config["target_root"], "renamed": False, "saved": False, "passed": False}
    try:
        if unreal.EditorLevelLibrary.get_pie_worlds(False):
            raise RuntimeError("PIE is active; migration has not begun")
        baseline = json.loads((REPORT_DIR / "editor_preflight.json").read_text(encoding="utf-8"))
        if not baseline["passed"]:
            raise RuntimeError("Preflight has not passed")
        report["world_before"] = editor_world()
        report["dirty_before"] = dirty_packages()
        owned_roots = [p[k].casefold() + "/" for p in configs() for k in ("source_root", "target_root")]
        unrelated_dirty = [name for values in report["dirty_before"].values() for name in values if not any(name.casefold().startswith(root) for root in owned_roots)]
        if unrelated_dirty:
            raise RuntimeError(f"Editor has unrelated dirty packages; leaving them untouched: {unrelated_dirty}")
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        registry.scan_paths_synchronous([config["source_root"], config["target_root"]], True)
        source = inventory(config["source_root"])
        target = inventory(config["target_root"])
        baseline_pack = next(p for p in baseline["packs"] if p["id"] == pack_id)
        expected_compatible = set(baseline_pack["source"]["packages"]) - UNLOADABLE_REGISTERED_SOURCE_PACKAGES
        if not expected_compatible.issubset(source["packages"]) or set(source["packages"]) - set(baseline_pack["source"]["packages"]) or target["asset_count"]:
            raise RuntimeError("Source count changed or destination already exists")
        report["preserved_unregistered_files"] = []
        preserved_packages = set(baseline_pack["unregistered_source_packages"]) | UNLOADABLE_REGISTERED_SOURCE_PACKAGES
        for entry in config["entries"]:
            if entry["source_package"] not in preserved_packages:
                continue
            original = PROJECT / "Content" / entry["source_package"].removeprefix("/Game/")
            original = original.with_suffix(Path(entry["relative_file"]).suffix)
            package = unreal.find_object(None, entry["source_package"])
            if package is not None:
                # Release only these imported packages' file handles before
                # relocating their preserved original bytes.
                unreal.EditorLoadingAndSavingUtils.fully_load_packages([package])
            backup = REPORT_DIR / "unregistered-originals" / pack_id / entry["relative_file"]
            backup.parent.mkdir(parents=True, exist_ok=True)
            if not backup.exists():
                shutil.copy2(original, backup)
            if backup.stat().st_size != entry["bytes"]:
                raise RuntimeError("Unregistered-file backup size mismatch")
            # The PhysX TireConfig cannot load in UE5.7, so it cannot participate
            # in an Unreal rename. Preserve it and the three unregistered files
            # verbatim, and move all compatible assets through the editor.
            if original.exists():
                if not original.resolve().is_relative_to((PROJECT / "Content" / config["source_root"].removeprefix("/Game/")).resolve()) or original.stat().st_size != entry["bytes"]:
                    raise RuntimeError("Unregistered staging file changed")
                original.unlink()
            report["preserved_unregistered_files"].append(entry["relative_file"])
        if pack_id == "mech-constructor-spiders-tanks":
            # Use explicit objects: the registry can still contain the removed
            # TireConfig until the filesystem watcher runs on a later tick.
            rename_data = []
            source_asset_data = {str(a.package_name): a for a in registry.get_assets_by_path(config["source_root"], recursive=True, include_only_on_disk_assets=True)}
            report["normalized_object_names"] = []
            for old_package in sorted(expected_compatible):
                # Some original files contain an asset whose object name differs
                # from the filename (e.g. Projectile_Grenade_3 contains _1).
                # AssetData uses the real object path instead of guessing it.
                asset = source_asset_data[old_package].get_asset()
                if asset is None:
                    raise RuntimeError(f"Unexpected compatible-asset load failure: {old_package}")
                new_package = config["target_root"] + old_package[len(config["source_root"]):]
                new_directory, new_name = new_package.rsplit("/", 1)
                if asset.get_name() != new_name:
                    report["normalized_object_names"].append({"source_object": asset.get_path_name(), "target_object": new_package + "." + new_name})
                rename_data.append(unreal.AssetRenameData(asset=asset, new_package_path=new_directory, new_name=new_name))
            report["rename_method"] = "AssetTools.rename_assets_compatible_batch"
            report["compatible_rename_count"] = len(rename_data)
            report["renamed"] = bool(unreal.AssetToolsHelpers.get_asset_tools().rename_assets(rename_data))
        else:
            report["rename_method"] = "EditorAssetLibrary.rename_directory"
            report["renamed"] = bool(unreal.EditorAssetLibrary.rename_directory(config["source_root"], config["target_root"]))
        write_report(pack_id + "_move.json", report)
        if not report["renamed"]:
            raise RuntimeError("Unreal rename_directory returned False")
        # Save the compatible assets before restoring the unloadable TireConfig.
        # This operation is restricted to the newly imported destination.
        report["saved"] = bool(unreal.EditorAssetLibrary.save_directory(config["target_root"], only_if_is_dirty=False, recursive=True))
        if not report["saved"]:
            raise RuntimeError("Saving the explicit destination returned False")
        for relative in report["preserved_unregistered_files"]:
            final_file = PROJECT / "Content" / config["target_root"].removeprefix("/Game/") / relative
            if not final_file.resolve().is_relative_to((PROJECT / "Content" / config["target_root"].removeprefix("/Game/")).resolve()):
                raise RuntimeError("Unregistered destination escaped the pack")
            if final_file.exists():
                raise RuntimeError(f"Unregistered destination appeared: {final_file}")
            final_file.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(REPORT_DIR / "unregistered-originals" / pack_id / relative, final_file)
        for entry in config["entries"]:
            if entry["relative_file"] not in report["preserved_unregistered_files"]:
                continue
            original = (PROJECT / "Content" / entry["source_package"].removeprefix("/Game/")).with_suffix(Path(entry["relative_file"]).suffix)
            final_file = PROJECT / "Content" / config["target_root"].removeprefix("/Game/") / entry["relative_file"]
            if final_file.stat().st_size != entry["bytes"]:
                raise RuntimeError("Final unregistered file size mismatch")
            if original.exists():
                if not original.resolve().is_relative_to((PROJECT / "Content" / config["source_root"].removeprefix("/Game/")).resolve()) or original.stat().st_size != entry["bytes"]:
                    raise RuntimeError("Original unregistered file changed; leaving it in place")
                original.unlink()
        registry.scan_paths_synchronous([config["source_root"], config["target_root"]], True)
        report["source_after"] = inventory(config["source_root"])
        report["target_after"] = inventory(config["target_root"])
        report["dirty_after"] = dirty_packages()
        report["world_after"] = editor_world()
        expected = sorted(p.replace(config["source_root"] + "/", config["target_root"] + "/", 1) for p in baseline_pack["source"]["packages"])
        report["expected_packages_match"] = report["target_after"]["packages"] == expected
        report["passed"] = bool(report["expected_packages_match"] and not report["source_after"]["non_redirectors"] and report["world_before"] == report["world_after"])
    except Exception:
        report["error"] = traceback.format_exc()
    write_report(pack_id + "_move.json", report)
    print(json.dumps({k: report[k] for k in ("id", "renamed", "saved", "passed")}, ensure_ascii=False))
    if not report["passed"]:
        raise RuntimeError(report.get("error", "Migration readback failed; see report"))
    return report


def validate_pack(pack_id: str) -> dict:
    config = config_for(pack_id)
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([config["source_root"], config["target_root"]], True)
    target = inventory(config["target_root"])
    dependencies = dependency_report(target["packages"], config["target_root"])
    baseline = json.loads((REPORT_DIR / "editor_preflight.json").read_text(encoding="utf-8"))
    baseline_pack = next(p for p in baseline["packs"] if p["id"] == pack_id)
    expected = sorted(p.replace(config["source_root"] + "/", config["target_root"] + "/", 1) for p in baseline_pack["source"]["packages"])
    # Load explicit packages without opening maps or playing the game. All assets
    # were already loaded by rename_directory; this verifies their new paths.
    load_failures = []
    for package in target["non_redirectors"]:
        if unreal.EditorAssetLibrary.load_asset(package) is None:
            load_failures.append(package)
    report = {
        "id": pack_id, "source_root": config["source_root"], "target_root": config["target_root"],
        "target": target, "source_after": inventory(config["source_root"]),
        "expected_packages_match": expected == target["packages"],
        "dependencies": dependencies, "load_failures": load_failures,
        "editor_world": editor_world(), "dirty_after": dirty_packages(),
        "visual_verification": "not_run_user_review", "runtime_verification": "not_run",
    }
    report["unregistered_originals"] = baseline_pack["unregistered_source_packages"]
    report["dependency_integrity_passed"] = not dependencies["old_path_dependencies"] and not dependencies["missing_game_dependencies"] and not load_failures
    expected_load_failures = sorted(p.replace(config["source_root"] + "/", config["target_root"] + "/", 1) for p in UNLOADABLE_REGISTERED_SOURCE_PACKAGES if p.startswith(config["source_root"] + "/"))
    report["expected_source_load_failures"] = expected_load_failures
    report["unexpected_load_failures"] = sorted(set(load_failures) - set(expected_load_failures))
    report["passed"] = bool(report["expected_packages_match"] and not report["source_after"]["non_redirectors"] and not dependencies["old_path_dependencies"] and not report["unexpected_load_failures"])
    write_report(pack_id + "_validation.json", report)
    print(json.dumps({"id": pack_id, "packages": target["package_count"], "classes": target["classes"], "stale": len(dependencies["old_path_dependencies"]), "missing": len(dependencies["missing_game_dependencies"]), "load_failures": len(load_failures), "passed": report["passed"]}, ensure_ascii=False))
    return report
