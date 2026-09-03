"""Resave the known Sci-fi Skies material and audit its editor-only dependency.

Run with UnrealEditor-Cmd and ``-ExecutePythonScript``.  The script deliberately
uses Unreal's asset APIs instead of reading the binary package directly.
"""

from __future__ import annotations

import json
import os
import traceback

import unreal


MATERIAL_OBJECT_PATH = (
    "/Game/Assets/Environments/Scifi_Skies/Materials/Main_Materials/"
    "M_Flowmap_Additive.M_Flowmap_Additive"
)
MATERIAL_PACKAGE_PATH = (
    "/Game/Assets/Environments/Scifi_Skies/Materials/Main_Materials/"
    "M_Flowmap_Additive"
)
STALE_PACKAGE_PATH = "/Engine/EditorMaterials/TargetIcon"
MATERIAL_FILENAME = os.path.join(
    unreal.Paths.project_content_dir(),
    "Assets",
    "Environments",
    "Scifi_Skies",
    "Materials",
    "Main_Materials",
    "M_Flowmap_Additive.uasset",
)
REPORT_PATH = os.path.join(
    unreal.Paths.project_dir(),
    "TestResults",
    "WingmanPlan",
    "CookRepair",
    "flowmap_material_dependency.json",
)


def _dependency_options() -> unreal.AssetRegistryDependencyOptions:
    options = unreal.AssetRegistryDependencyOptions()
    options.include_soft_package_references = True
    options.include_hard_package_references = True
    options.include_searchable_names = True
    options.include_soft_management_references = True
    options.include_hard_management_references = True
    return options


def _dependencies(registry: unreal.AssetRegistry) -> list[str]:
    return sorted(
        str(value)
        for value in registry.get_dependencies(
            unreal.Name(MATERIAL_PACKAGE_PATH), _dependency_options()
        )
    )


def _referencers() -> list[str]:
    return sorted(
        str(value)
        for value in unreal.EditorAssetLibrary.find_package_referencers_for_asset(
            STALE_PACKAGE_PATH, False
        )
    )


def _write_report(report: dict) -> None:
    os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
    with open(REPORT_PATH, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")


def main() -> None:
    report = {
        "material_object_path": MATERIAL_OBJECT_PATH,
        "material_package_path": MATERIAL_PACKAGE_PATH,
        "stale_package_path": STALE_PACKAGE_PATH,
        "saved": False,
        "passed": False,
    }

    try:
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        registry.wait_for_completion()
        report["dependencies_before"] = _dependencies(registry)
        report["stale_dependency_before"] = (
            STALE_PACKAGE_PATH in report["dependencies_before"]
        )
        report["target_icon_referencers_before"] = _referencers()

        material = unreal.EditorAssetLibrary.load_asset(MATERIAL_OBJECT_PATH)
        if material is None:
            raise RuntimeError(f"failed to load material: {MATERIAL_OBJECT_PATH}")

        report["loaded_class"] = material.get_class().get_name()
        report["saved"] = bool(
            unreal.EditorAssetLibrary.save_loaded_asset(
                material, only_if_is_dirty=False
            )
        )
        if not report["saved"]:
            raise RuntimeError(f"failed to save material: {MATERIAL_OBJECT_PATH}")

        registry.scan_modified_asset_files(
            [unreal.Paths.convert_relative_path_to_full(MATERIAL_FILENAME)]
        )
        registry.wait_for_completion()
        report["dependencies_after"] = _dependencies(registry)
        report["stale_dependency_after"] = (
            STALE_PACKAGE_PATH in report["dependencies_after"]
        )
        report["target_icon_referencers_after"] = _referencers()
        report["passed"] = not report["stale_dependency_after"]
    except Exception:  # noqa: BLE001 - preserve the full UE-side diagnostic
        report["error"] = traceback.format_exc()

    _write_report(report)
    unreal.log(
        "GuLi cook dependency repair: "
        + json.dumps(
            {
                "saved": report["saved"],
                "stale_dependency_after": report.get("stale_dependency_after"),
                "passed": report["passed"],
                "report": REPORT_PATH,
            },
            ensure_ascii=False,
        )
    )
    if not report["passed"]:
        raise RuntimeError(
            "M_Flowmap_Additive still references /Engine/EditorMaterials/TargetIcon; "
            f"see {REPORT_PATH}"
        )


main()
