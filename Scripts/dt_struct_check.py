import json

import unreal

OUT = "D:/UE_5.7/test1/Data/tmp_dt_struct_check.json"
result = {}
for name in ("DT_GuLiStrikeShip_Parts", "DT_GuLiStrikeShip_Tuning"):
    dt = unreal.load_object(None, f"/Game/GuLiStrike/Data/{name}.{name}")
    if not dt:
        result[name] = "LOAD FAILED"
        continue
    rs = dt.get_editor_property("row_struct")
    result[name] = str(rs.get_path_name()) if rs else "NULL STRUCT"
    rows = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
    result[name + "#rows"] = [str(r) for r in rows]
with open(OUT, "w", encoding="utf-8") as f:
    json.dump(result, f, ensure_ascii=False, indent=1)
result  # noqa
