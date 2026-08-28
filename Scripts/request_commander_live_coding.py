import json

import unreal


OUTPUT_PATH = "D:/UE5.7/test1/Progress/CommanderLiveCodingRequest.json"

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_editor_world() or editor.get_game_world()
result = {
    "world": world.get_path_name() if world else None,
    "requested": False,
    "command": "LiveCoding.Compile",
}
if world:
    unreal.SystemLibrary.execute_console_command(world, result["command"])
    result["requested"] = True

with open(OUTPUT_PATH, "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)
