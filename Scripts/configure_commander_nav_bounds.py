import json
import traceback

import unreal


OUT = "D:/UE5.7/test1/Progress/CommanderNavBoundsConfigure.json"
result = {"changed": False, "saved": False, "rebuild_requested": False, "errors": []}
try:
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_editor_world()
    if world is None:
        raise RuntimeError("Editor world is not ready")
    bounds_actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.NavMeshBoundsVolume)
    if len(bounds_actors) != 1:
        raise RuntimeError(f"Expected one NavMeshBoundsVolume, found {len(bounds_actors)}")
    bounds = bounds_actors[0]
    bounds.modify()
    bounds.set_actor_scale3d(unreal.Vector(2800.0, 1750.0, 400.0))
    result["changed"] = True
    origin, extent = bounds.get_actor_bounds(False, False)
    result["origin"] = list(origin.to_tuple())
    result["extent"] = list(extent.to_tuple())
    result["saved"] = bool(unreal.EditorLevelLibrary.save_current_level())
    unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
    result["rebuild_requested"] = True
except Exception as exc:  # noqa: BLE001
    result["errors"].append(repr(exc))
    result["traceback"] = traceback.format_exc()
with open(OUT, "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)
