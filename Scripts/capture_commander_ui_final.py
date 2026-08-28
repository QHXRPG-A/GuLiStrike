import json

import unreal


OUT = "D:/UE5.7/test1/Progress/CommanderUI-Final.png"
RESULT = "D:/UE5.7/test1/Progress/CommanderUI-Final.json"
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_game_world()
result = {"world": world.get_path_name() if world else None, "output": OUT, "requested": False}
if world:
    unreal.SystemLibrary.execute_console_command(
        world,
        'HighResShot 1920x1080 filename="{}"'.format(OUT),
    )
    result["requested"] = True
with open(RESULT, "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)
