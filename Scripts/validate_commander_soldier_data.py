"""Read back the imported Commander Soldier DataTable and its model asset."""

import json

import unreal


OUT_PATH = "D:/UE5.7/test1/Progress/CommanderSoldierDataValidation.json"
TABLE_PATH = (
    "/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers."
    "DT_GuLiStrikeCommander_Soldiers"
)
MODEL_PATH = (
    "/Game/Commander/Units/SM_CommanderFourFRobot."
    "SM_CommanderFourFRobot"
)
EXPECTED_STRUCT = "/Script/GuLiStrike.GuLiStrikeCommanderSoldiersRow"


result = {
    "table_path": TABLE_PATH,
    "model_path": MODEL_PATH,
    "checks": {},
}

table = unreal.load_object(None, TABLE_PATH)
result["checks"]["table_loaded"] = table is not None
if table:
    row_struct = table.get_editor_property("row_struct")
    result["row_struct"] = row_struct.get_path_name() if row_struct else None
    result["checks"]["row_struct"] = result["row_struct"] == EXPECTED_STRUCT
    row_names = [
        str(name)
        for name in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)
    ]
    result["row_names"] = row_names
    result["checks"]["single_default_row"] = row_names == ["DefaultSoldier"]

    rows = json.loads(
        unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)
    )
    row = next((item for item in rows if item.get("Name") == "DefaultSoldier"), None)
    result["row"] = row
    result["checks"]["movement_speed"] = (
        row is not None and float(row.get("MovementSpeedCmPerSecond", -1)) == 3600.0
    )
    result["checks"]["maximum_health"] = (
        row is not None and int(row.get("MaxHealth", -1)) == 100
    )
    result["checks"]["combat_values"] = row is not None and all(
        float(row.get(key, -1)) == 0.0
        for key in ("AttackPower", "Defense", "AttackRangeCentimeters")
    )
    result["checks"]["model_reference"] = (
        row is not None
        and "/Game/Commander/Units/SM_CommanderFourFRobot" in str(row.get("ModelAsset"))
    )

model = unreal.load_object(None, MODEL_PATH)
result["model_class"] = model.get_class().get_path_name() if model else None
result["checks"]["model_is_static_mesh"] = isinstance(model, unreal.StaticMesh)
result["passed"] = all(result["checks"].values())

with open(OUT_PATH, "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)

if not result["passed"]:
    raise RuntimeError(f"Commander Soldier DataTable validation failed: {result['checks']}")

print("Commander Soldier DataTable validation passed")
