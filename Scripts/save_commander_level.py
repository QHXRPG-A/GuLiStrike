import json

import unreal

saved = bool(unreal.EditorLevelLibrary.save_current_level())
with open("D:/UE5.7/test1/Progress/CommanderLevelSave.json", "w", encoding="utf-8") as stream:
    json.dump({"saved": saved}, stream, indent=2)
