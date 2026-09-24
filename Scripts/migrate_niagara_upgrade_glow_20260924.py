"""Move the staged UE 5.1 Niagara pack through Unreal's asset APIs.

Run in a standalone GuLiStrike UnrealEditor process with -ExecutePythonScript.
The report is written before the process exits, including on failure.
"""

from __future__ import annotations

import collections
import json
import os
import traceback

import unreal


SOURCE = "/Game/NiagaraUpgradeGlow"
TARGET = "/Game/Assets/VFX/NiagaraUpgradeGlow"
EXPECTED_PACKAGES = 202
REPORT_PATH = os.path.join(
    unreal.Paths.project_dir(),
    "TestResults",
    "AssetMigration20260924",
    "niagara_editor_report.json",
)


def scan(registry) -> None:
    registry.scan_paths_synchronous([SOURCE, TARGET], True)
    registry.wait_for_completion()


def inventory(registry, root: str) -> dict:
    assets = list(registry.get_assets_by_path(root, recursive=True))
    classes = collections.Counter(str(asset.asset_class_path.asset_name) for asset in assets)
    return {
        "asset_count": len(assets),
        "package_count": len({str(asset.package_name) for asset in assets}),
        "classes": dict(sorted(classes.items())),
        "packages": sorted({str(asset.package_name) for asset in assets}),
        "non_redirectors": sorted(
            str(asset.package_name)
            for asset in assets
            if str(asset.asset_class_path.asset_name) != "ObjectRedirector"
        ),
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


def dirty_package_names() -> dict:
    return {
        "content": sorted(
            package.get_path_name()
            for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
            if package
        ),
        "maps": sorted(
            package.get_path_name()
            for package in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
            if package
        ),
    }


def main() -> None:
    report = {
        "source": SOURCE,
        "target": TARGET,
        "expected_packages": EXPECTED_PACKAGES,
        "renamed": False,
        "saved": False,
        "passed": False,
    }
    try:
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        scan(registry)
        report["before"] = {
            "source": inventory(registry, SOURCE),
            "target": inventory(registry, TARGET),
            "dirty": dirty_package_names(),
        }
        if report["before"]["dirty"]["content"] or report["before"]["dirty"]["maps"]:
            raise RuntimeError("Editor has dirty packages before migration")
        if report["before"]["source"]["package_count"] != EXPECTED_PACKAGES:
            raise RuntimeError("Staged source package count differs from archive inventory")
        if report["before"]["target"]["asset_count"]:
            raise RuntimeError("Destination has existing assets; refusing to overwrite")

        report["renamed"] = bool(unreal.EditorAssetLibrary.rename_directory(SOURCE, TARGET))
        if not report["renamed"]:
            raise RuntimeError("Unreal rename_directory returned False")
        scan(registry)
        report["after_rename"] = {
            "source": inventory(registry, SOURCE),
            "target": inventory(registry, TARGET),
        }
        if report["after_rename"]["target"]["package_count"] != EXPECTED_PACKAGES:
            raise RuntimeError("Destination package count differs from archive inventory")
        if report["after_rename"]["source"]["non_redirectors"]:
            raise RuntimeError("Non-redirector assets remain at the original package path")

        report["saved"] = bool(
            unreal.EditorAssetLibrary.save_directory(
                TARGET, only_if_is_dirty=False, recursive=True
            )
        )
        if not report["saved"]:
            raise RuntimeError("Unreal save_directory returned False")
        scan(registry)
        report["after_save"] = {
            "source": inventory(registry, SOURCE),
            "target": inventory(registry, TARGET),
            "dirty": dirty_package_names(),
        }

        options = dependency_options()
        old_dependencies = {}
        missing_game_dependencies = {}
        load_failures = []
        for package_name in report["after_save"]["target"]["packages"]:
            if unreal.EditorAssetLibrary.load_asset(package_name) is None:
                load_failures.append(package_name)
            dependencies = sorted(
                str(value) for value in (registry.get_dependencies(package_name, options) or [])
            )
            stale = [value for value in dependencies if value.startswith(SOURCE + "/")]
            missing = [
                value
                for value in dependencies
                if value.startswith("/Game/")
                and not value.startswith(TARGET + "/")
                and not value.startswith(SOURCE + "/")
                and not unreal.EditorAssetLibrary.does_asset_exist(value)
            ]
            if stale:
                old_dependencies[package_name] = stale
            if missing:
                missing_game_dependencies[package_name] = missing
        report["old_path_dependencies"] = old_dependencies
        report["missing_game_dependencies"] = missing_game_dependencies
        report["load_failures"] = load_failures
        report["passed"] = (
            report["after_save"]["target"]["package_count"] == EXPECTED_PACKAGES
            and not report["after_save"]["source"]["non_redirectors"]
            and not old_dependencies
            and not missing_game_dependencies
            and not load_failures
        )
    except Exception:
        report["error"] = traceback.format_exc()
        unreal.log_error(report["error"])
    finally:
        os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
        with open(REPORT_PATH, "w", encoding="utf-8") as stream:
            json.dump(report, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
        print("NIAGARA_UPGRADE_GLOW_MIGRATION_REPORT_WRITTEN")
        unreal.SystemLibrary.quit_editor()


main()
