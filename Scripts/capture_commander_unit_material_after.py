import json

import unreal


SCREENSHOT_PATH = "D:/UE5.7/test1/Progress/CommanderUnitMaterialsAfter.png"
OUTPUT_PATH = "D:/UE5.7/test1/Progress/CommanderUnitMaterialsAfterCapture.json"

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_game_world()
result = {
    "world": world.get_path_name() if world else None,
    "screenshot": SCREENSHOT_PATH,
    "requested": False,
}
if world:
    unreal.SystemLibrary.execute_console_command(
        world,
        'HighResShot 1920x1080 filename="{}"'.format(SCREENSHOT_PATH),
    )
    result["requested"] = True

with open(OUTPUT_PATH, "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)
