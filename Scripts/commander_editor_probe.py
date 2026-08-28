import json

import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
result = {
    "level_methods": [name for name in dir(level) if "play" in name.lower() or "sim" in name.lower()],
    "editor_methods": [name for name in dir(editor) if "play" in name.lower() or "world" in name.lower()],
    "editor_world": editor.get_editor_world().get_path_name() if editor.get_editor_world() else None,
    "game_world": editor.get_game_world().get_path_name() if editor.get_game_world() else None,
}
with open("D:/UE5.7/test1/Progress/CommanderEditorProbe.json", "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)
