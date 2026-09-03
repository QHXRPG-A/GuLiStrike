"""Deploy and wire a runtime-safe copy of UE's SpiralBlur-Texture function.

The stock engine function uses the editor-only TargetIcon texture on a preview
TextureObject node.  A material that calls the function can therefore retain an
illegal cook dependency even when its TextureObject input is connected.  This
script duplicates the function into the project, replaces only that preview
texture, rewires the two calls in M_Flowmap_Additive, and verifies both packages.
"""

from __future__ import annotations

import json
import os
import traceback

import unreal


SOURCE_FUNCTION_PACKAGE = (
    "/Engine/Functions/Engine_MaterialFunctions01/Texturing/SpiralBlur-Texture"
)
SOURCE_FUNCTION_OBJECT = SOURCE_FUNCTION_PACKAGE + ".SpiralBlur-Texture"
DEST_FUNCTION_PACKAGE = (
    "/Game/GuLiStrike/Materials/Functions/MF_SpiralBlurTexture_RuntimeSafe"
)
DEST_FUNCTION_OBJECT = (
    DEST_FUNCTION_PACKAGE + ".MF_SpiralBlurTexture_RuntimeSafe"
)
MATERIAL_PACKAGE = (
    "/Game/Assets/Environments/Scifi_Skies/Materials/Main_Materials/"
    "M_Flowmap_Additive"
)
MATERIAL_OBJECT = MATERIAL_PACKAGE + ".M_Flowmap_Additive"
TARGET_ICON_OBJECT = "/Engine/EditorMaterials/TargetIcon.TargetIcon"
TARGET_ICON_PACKAGE = "/Engine/EditorMaterials/TargetIcon"
SAFE_TEXTURE_OBJECT = "/Engine/EngineMaterials/DefaultDiffuse.DefaultDiffuse"
REPORT_PATH = os.path.join(
    unreal.Paths.project_dir(),
    "TestResults",
    "WingmanPlan",
    "CookRepair",
    "runtime_safe_spiral_blur_deployment.json",
)


def _path(obj: unreal.Object) -> str:
    return str(unreal.SystemLibrary.get_path_name(obj))


def _name(obj: unreal.Object) -> str:
    return str(unreal.SystemLibrary.get_object_name(obj))


def _objects_below(object_path: str) -> list[unreal.Object]:
    return [
        obj
        for obj in unreal.ObjectIterator()
        if _path(obj).startswith(object_path)
    ]


def _dependency_options() -> unreal.AssetRegistryDependencyOptions:
    options = unreal.AssetRegistryDependencyOptions()
    options.include_soft_package_references = True
    options.include_hard_package_references = True
    options.include_searchable_names = True
    options.include_soft_management_references = True
    options.include_hard_management_references = True
    return options


def _dependencies(
    registry: unreal.AssetRegistry, package_path: str
) -> list[str]:
    return sorted(
        str(value)
        for value in registry.get_dependencies(
            unreal.Name(package_path), _dependency_options()
        )
    )


def _package_filename(package_path: str) -> str:
    if package_path.startswith("/Game/"):
        relative = package_path.removeprefix("/Game/").replace("/", os.sep)
        return os.path.join(
            unreal.Paths.project_content_dir(), relative + ".uasset"
        )
    raise ValueError(f"unsupported package root: {package_path}")


def main() -> None:
    report = {
        "source_function": SOURCE_FUNCTION_OBJECT,
        "destination_function": DEST_FUNCTION_OBJECT,
        "material": MATERIAL_OBJECT,
        "target_icon": TARGET_ICON_OBJECT,
        "safe_texture": SAFE_TEXTURE_OBJECT,
        "created_destination": False,
        "updated_preview_texture_count": 0,
        "rewired_call_count": 0,
        "passed": False,
    }

    try:
        if not unreal.EditorAssetLibrary.does_asset_exist(
            DEST_FUNCTION_PACKAGE
        ):
            duplicated = unreal.EditorAssetLibrary.duplicate_asset(
                SOURCE_FUNCTION_PACKAGE, DEST_FUNCTION_PACKAGE
            )
            if duplicated is None:
                raise RuntimeError(
                    "failed to duplicate SpiralBlur-Texture into the project"
                )
            report["created_destination"] = True

        runtime_function = unreal.EditorAssetLibrary.load_asset(
            DEST_FUNCTION_OBJECT
        )
        if runtime_function is None:
            raise RuntimeError(
                f"failed to load destination function: {DEST_FUNCTION_OBJECT}"
            )
        safe_texture = unreal.EditorAssetLibrary.load_asset(SAFE_TEXTURE_OBJECT)
        if safe_texture is None:
            raise RuntimeError(
                f"failed to load runtime-safe texture: {SAFE_TEXTURE_OBJECT}"
            )

        preview_nodes = []
        for obj in _objects_below(DEST_FUNCTION_OBJECT):
            if _name(obj.get_class()) != "MaterialExpressionTextureObject":
                continue
            try:
                texture = obj.get_editor_property("texture")
            except Exception:  # noqa: BLE001 - unrelated texture node
                continue
            if texture is not None and _path(texture) == TARGET_ICON_OBJECT:
                obj.modify()
                obj.set_editor_property("texture", safe_texture)
                preview_nodes.append(_path(obj))

        runtime_function.modify()
        unreal.MaterialEditingLibrary.update_material_function(
            runtime_function, None
        )
        if not unreal.EditorAssetLibrary.save_loaded_asset(
            runtime_function, only_if_is_dirty=False
        ):
            raise RuntimeError(
                f"failed to save destination function: {DEST_FUNCTION_OBJECT}"
            )
        report["updated_preview_texture_count"] = len(preview_nodes)
        report["updated_preview_nodes"] = preview_nodes

        material = unreal.EditorAssetLibrary.load_asset(MATERIAL_OBJECT)
        if material is None:
            raise RuntimeError(f"failed to load material: {MATERIAL_OBJECT}")

        rewired_nodes = []
        for obj in _objects_below(MATERIAL_OBJECT):
            if _name(obj.get_class()) != "MaterialExpressionMaterialFunctionCall":
                continue
            try:
                called_function = obj.get_editor_property("material_function")
            except Exception:  # noqa: BLE001 - malformed function call node
                continue
            if called_function is not None and _path(called_function) in {
                SOURCE_FUNCTION_OBJECT,
                DEST_FUNCTION_OBJECT,
            }:
                obj.modify()
                obj.set_editor_property("material_function", runtime_function)
                rewired_nodes.append(_path(obj))

        material.modify()
        unreal.MaterialEditingLibrary.recompile_material(material)
        if not unreal.EditorAssetLibrary.save_loaded_asset(
            material, only_if_is_dirty=False
        ):
            raise RuntimeError(f"failed to save material: {MATERIAL_OBJECT}")
        report["rewired_call_count"] = len(rewired_nodes)
        report["rewired_nodes"] = rewired_nodes

        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        registry.scan_modified_asset_files(
            [
                unreal.Paths.convert_relative_path_to_full(
                    _package_filename(DEST_FUNCTION_PACKAGE)
                ),
                unreal.Paths.convert_relative_path_to_full(
                    _package_filename(MATERIAL_PACKAGE)
                ),
            ]
        )
        registry.wait_for_completion()
        report["function_dependencies"] = _dependencies(
            registry, DEST_FUNCTION_PACKAGE
        )
        report["material_dependencies"] = _dependencies(
            registry, MATERIAL_PACKAGE
        )
        report["function_has_target_icon"] = (
            TARGET_ICON_PACKAGE in report["function_dependencies"]
        )
        report["material_has_target_icon"] = (
            TARGET_ICON_PACKAGE in report["material_dependencies"]
        )
        report["material_uses_runtime_safe_function"] = (
            DEST_FUNCTION_PACKAGE in report["material_dependencies"]
        )
        report["material_uses_stock_spiral_function"] = (
            SOURCE_FUNCTION_PACKAGE in report["material_dependencies"]
        )
        report["passed"] = all(
            (
                report["updated_preview_texture_count"] == 1,
                report["rewired_call_count"] == 2,
                not report["function_has_target_icon"],
                not report["material_has_target_icon"],
                report["material_uses_runtime_safe_function"],
                not report["material_uses_stock_spiral_function"],
            )
        )
    except Exception:  # noqa: BLE001 - preserve full UE-side diagnostic
        report["error"] = traceback.format_exc()

    os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
    with open(REPORT_PATH, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")

    unreal.log(
        "GuLi runtime-safe SpiralBlur deployment: "
        + json.dumps(
            {
                "updated_preview_texture_count": report[
                    "updated_preview_texture_count"
                ],
                "rewired_call_count": report["rewired_call_count"],
                "passed": report["passed"],
                "report": REPORT_PATH,
            },
            ensure_ascii=False,
        )
    )
    if not report["passed"]:
        raise RuntimeError(
            f"runtime-safe SpiralBlur deployment failed; see {REPORT_PATH}"
        )


main()
