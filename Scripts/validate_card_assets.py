"""Static post-migration checks for /Game/Assets/card.

This compiles imported Blueprint assets, runs registered asset validators, and checks
that project-content dependencies resolve. It does not open maps or start PIE.
"""

import collections
import json
import traceback

import unreal


ROOTS = [
    "/Game/Assets/card/RewardCards",
    "/Game/Assets/card/ParallaxCardMaterial",
]
OLD_ROOTS = ("/Game/RewardCards", "/Game/ParallaxCardMaterial")
REPORT_PATH = "D:/UE5.7/test1/Saved/card_asset_validation_report.json"


def package_names(packages):
    return sorted(package.get_path_name() for package in packages if package)


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


report = {
    "roots": ROOTS,
    "inventory": {},
    "blueprint_compile": [],
    "validation": {"counts": {}, "issues": []},
    "missing_game_dependencies": {},
    "dependencies_on_old_roots": {},
    "errors": [],
}

try:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(ROOTS, True)
    assets = []
    for root in ROOTS:
        root_assets = list(registry.get_assets_by_path(root, recursive=True))
        assets.extend(root_assets)
        report["inventory"][root] = {
            "asset_count": len(root_assets),
            "classes": dict(
                sorted(
                    collections.Counter(
                        str(asset.asset_class_path.asset_name) for asset in root_assets
                    ).items()
                )
            ),
        }

    report["dirty_before"] = {
        "content": package_names(
            unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
        ),
        "maps": package_names(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()),
    }
    if report["dirty_before"]["content"] or report["dirty_before"]["maps"]:
        raise RuntimeError("Editor has dirty packages; static validation aborted")

    blueprint_classes = {"Blueprint", "WidgetBlueprint"}
    for asset_data in sorted(assets, key=lambda item: str(item.package_name)):
        if str(asset_data.asset_class_path.asset_name) not in blueprint_classes:
            continue
        package_name = str(asset_data.package_name)
        item = {"package": package_name, "compiled": False, "saved": False}
        report["blueprint_compile"].append(item)
        try:
            blueprint = asset_data.get_asset()
            unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
            item["compiled"] = True
            item["saved"] = bool(
                unreal.EditorAssetLibrary.save_loaded_asset(
                    blueprint, only_if_is_dirty=True
                )
            )
        except Exception:
            item["error"] = traceback.format_exc()

    registry.scan_paths_synchronous(ROOTS, True)
    assets = []
    for root in ROOTS:
        assets.extend(list(registry.get_assets_by_path(root, recursive=True)))

    validator = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem)
    validation_counts = collections.Counter()
    for asset_data in sorted(assets, key=lambda item: str(item.package_name)):
        package_name = str(asset_data.package_name)
        try:
            result, errors, warnings = validator.is_asset_valid(
                asset_data, unreal.DataValidationUsecase.MANUAL
            )
            result_name = str(result)
            validation_counts[result_name] += 1
            if errors or warnings or "INVALID" in result_name.upper():
                report["validation"]["issues"].append(
                    {
                        "package": package_name,
                        "result": result_name,
                        "errors": [str(item) for item in errors],
                        "warnings": [str(item) for item in warnings],
                    }
                )
        except Exception:
            report["validation"]["issues"].append(
                {"package": package_name, "exception": traceback.format_exc()}
            )
    report["validation"]["counts"] = dict(sorted(validation_counts.items()))

    options = dependency_options()
    for asset_data in sorted(assets, key=lambda item: str(item.package_name)):
        package_name = str(asset_data.package_name)
        dependencies = sorted(
            {str(item) for item in (registry.get_dependencies(package_name, options) or [])}
        )
        missing = [
            dependency
            for dependency in dependencies
            if dependency.startswith("/Game/")
            and not unreal.EditorAssetLibrary.does_asset_exist(dependency)
        ]
        stale = [
            dependency
            for dependency in dependencies
            if dependency.startswith(OLD_ROOTS)
        ]
        if missing:
            report["missing_game_dependencies"][package_name] = missing
        if stale:
            report["dependencies_on_old_roots"][package_name] = stale

    report["dirty_after"] = {
        "content": package_names(
            unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
        ),
        "maps": package_names(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()),
    }
except Exception:
    report["errors"].append(traceback.format_exc())

with open(REPORT_PATH, "w", encoding="utf-8") as stream:
    json.dump(report, stream, ensure_ascii=False, indent=2)

print("CARD_ASSET_VALIDATION_REPORT_WRITTEN")
