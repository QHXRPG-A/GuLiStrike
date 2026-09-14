"""Author and bake the canonical resource board in a stopped UE editor process.

Usage:
    UnrealEditor.exe GuLiStrike.uproject /Game/Maps/LVL_CommanderMassPrototype \
        -unattended -NoLiveCoding -ExecutePythonScript=Scripts/build_resource_board_and_ore.py
"""

import json
import traceback

import unreal


OUTPUT = "D:/UE5.7/test1/Progress/ResourceBoardBake.json"


def struct_to_dict(result):
    return {
        "success": bool(result.success),
        "message": str(result.message),
        "issues": [str(value) for value in result.issues],
        "source_hash": str(result.source_hash),
        "layout_hash": str(result.layout_hash),
        "territory_count": int(result.territory_count),
        "cluster_count": int(result.cluster_count),
        "node_count": int(result.node_count),
    }


report = {"success": False, "errors": []}
try:
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_editor_world()
    if world is None:
        raise RuntimeError("Editor world is not available")
    package_name = world.get_outermost().get_name()
    if package_name != "/Game/Maps/LVL_CommanderMassPrototype":
        raise RuntimeError(f"Unexpected loaded map: {package_name}")
    result = unreal.GuLiResourceAuthoringLibrary.prepare_canonical_authoring_and_bake(True, True)
    report = struct_to_dict(result)
except Exception as exc:  # noqa: BLE001
    report["errors"].append(repr(exc))
    report["traceback"] = traceback.format_exc()
finally:
    with open(OUTPUT, "w", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
    unreal.SystemLibrary.quit_editor()
