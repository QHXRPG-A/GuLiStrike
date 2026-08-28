import json

import unreal

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
result = {
    "was_in_pie": level.is_in_play_in_editor(),
}
if not result["was_in_pie"]:
    level.editor_request_begin_play()
result["requested"] = True
with open("D:/UE5.7/test1/Progress/CommanderPIEStart.json", "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)
