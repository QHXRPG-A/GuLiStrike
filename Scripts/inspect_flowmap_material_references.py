"""Inspect reflected objects in M_Flowmap_Additive for the TargetIcon reference."""

from __future__ import annotations

import json
import os
import traceback

import unreal


MATERIAL_OBJECT_PATH = (
    "/Game/Assets/Environments/Scifi_Skies/Materials/Main_Materials/"
    "M_Flowmap_Additive.M_Flowmap_Additive"
)
SPIRAL_FUNCTION_OBJECT_PATH = (
    "/Engine/Functions/Engine_MaterialFunctions01/Texturing/"
    "SpiralBlur-Texture.SpiralBlur-Texture"
)
NEEDLE = "targeticon"
REPORT_PATH = os.path.join(
    unreal.Paths.project_dir(),
    "TestResults",
    "WingmanPlan",
    "CookRepair",
    "flowmap_reflection_audit.json",
)


def _safe_string(value) -> str:
    try:
        return str(value)
    except Exception:  # noqa: BLE001 - diagnostic only
        return "<unprintable>"


def _path(obj: unreal.Object) -> str:
    return str(unreal.SystemLibrary.get_path_name(obj))


def _name(obj: unreal.Object) -> str:
    return str(unreal.SystemLibrary.get_object_name(obj))


def _object_record(obj: unreal.Object) -> dict:
    record = {
        "class": _name(obj.get_class()),
        "name": _name(obj),
        "path": _path(obj),
        "properties": [],
        "matching_properties": [],
    }
    candidate_names = set(dir(obj))
    candidate_names.update(
        {
            "material_function",
            "function_inputs",
            "function_outputs",
            "texture",
            "expression_collection",
            "editor_only_data",
            "cached_expression_data",
            "material_graph",
            "preview_mesh",
            "thumbnail_info",
        }
    )
    for name in sorted(candidate_names):
        if name.startswith("_"):
            continue
        try:
            value = obj.get_editor_property(name)
        except Exception:  # noqa: BLE001 - most dir entries are methods
            continue
        text = _safe_string(value)
        record["properties"].append(
            {"property": name, "value": text[:2000]}
        )
        if NEEDLE in text.lower():
            record["matching_properties"].append(
                {"property": name, "value": text}
            )
    return record


def main() -> None:
    report = {
        "material_object_path": MATERIAL_OBJECT_PATH,
        "objects": [],
        "expressions": [],
        "passed": False,
    }
    try:
        material = unreal.EditorAssetLibrary.load_asset(MATERIAL_OBJECT_PATH)
        if material is None:
            raise RuntimeError(f"failed to load material: {MATERIAL_OBJECT_PATH}")

        objects = [material]
        try:
            objects.extend(
                obj
                for obj in unreal.ObjectIterator()
                if _path(obj).startswith(MATERIAL_OBJECT_PATH)
            )
        except Exception as error:  # noqa: BLE001 - retain partial diagnostic
            report["outer_walk_error"] = repr(error)

        seen_paths = set()
        for obj in objects:
            if obj is None:
                continue
            path = _path(obj)
            if path in seen_paths:
                continue
            seen_paths.add(path)
            report["objects"].append(_object_record(obj))

        expressions = [
            obj
            for obj in objects
            if obj is not None
            and _name(obj.get_class()).startswith("MaterialExpression")
        ]
        report["expressions"] = []
        for expression in expressions:
            expression_record = {
                "class": _name(expression.get_class()),
                "name": _name(expression),
                "path": _path(expression),
            }
            try:
                expression_record["input_names"] = list(
                    unreal.MaterialEditingLibrary.get_material_expression_input_names(
                        expression
                    )
                )
                expression_record["connected_inputs"] = [
                    _path(value)
                    for value in unreal.MaterialEditingLibrary.get_inputs_for_material_expression(
                        material, expression
                    )
                    if value is not None
                ]
            except Exception as error:  # noqa: BLE001 - retain partial diagnostic
                expression_record["input_audit_error"] = repr(error)
            report["expressions"].append(expression_record)

        report["used_textures"] = [
            _path(texture)
            for texture in unreal.MaterialEditingLibrary.get_used_textures(material)
            if texture is not None
        ]

        spiral_function = unreal.EditorAssetLibrary.load_asset(
            SPIRAL_FUNCTION_OBJECT_PATH
        )
        if spiral_function is None:
            raise RuntimeError(
                f"failed to load material function: {SPIRAL_FUNCTION_OBJECT_PATH}"
            )
        spiral_objects = [
            obj
            for obj in unreal.ObjectIterator()
            if _path(obj).startswith(SPIRAL_FUNCTION_OBJECT_PATH)
        ]
        report["spiral_function"] = {
            "object_path": SPIRAL_FUNCTION_OBJECT_PATH,
            "objects": [_object_record(obj) for obj in spiral_objects],
        }
        report["spiral_function"]["matching_objects"] = [
            item
            for item in report["spiral_function"]["objects"]
            if item["matching_properties"]
        ]
        report["matching_objects"] = [
            item for item in report["objects"] if item["matching_properties"]
        ]
        report["passed"] = True
    except Exception:  # noqa: BLE001 - preserve full UE-side diagnostic
        report["error"] = traceback.format_exc()

    os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
    with open(REPORT_PATH, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")

    unreal.log(
        "GuLi flowmap reflection audit: "
        + json.dumps(
            {
                "passed": report["passed"],
                "object_count": len(report["objects"]),
                "expression_count": len(report["expressions"]),
                "matching_object_count": len(report.get("matching_objects", [])),
                "report": REPORT_PATH,
            },
            ensure_ascii=False,
        )
    )
    if not report["passed"]:
        raise RuntimeError(f"flowmap reflection audit failed; see {REPORT_PATH}")


main()
