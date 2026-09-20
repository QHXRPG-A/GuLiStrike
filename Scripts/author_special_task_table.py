"""Author the initial task workbook through excelize-cli; never replace populated user data."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
XLSX = Path(r"D:\UE5.7\excelize-cli\bin\xlsx.exe")
DEST = ROOT / "data/Excel/GuLiStrikeSpecialTasks.xlsx"


def run(*args):
    return subprocess.check_output([str(XLSX), *map(str, args)], text=True, encoding="utf-8")


if DEST.exists():
    existing = json.loads(run("read", DEST, "--sheet", "Tasks", "--range", "A1:I6", "--format", "json"))
    if any(any(str(cell).strip() for cell in row) for row in existing.get("rows", [])):
        raise SystemExit("SpecialTasks already populated; keeping authored data.")
else:
    run("new", DEST, "--sheet", "Tasks")

columns = ["id", "name", "Note", "DisplayName", "TaskTag", "ApplicableUnitIds", "AutoActivate", "LifetimePolicy", "ExecutorClass"]
types = ["int", "str", "str", "str", "str", "str", "bool", "str", "softclass"]
rows = [columns, types, ["Optional" if name == "Note" else "Necessary" for name in columns],
        [1, "Mining", "持续自动采矿；手动队列优先", "采矿", "Task.Special.Mining", "3", True, "Persistent", "/Script/GuLiStrike.GuLiMiningSpecialTaskExecutor"],
        [2, "Construction", "全图最近可达我方工地；允许协作", "建造", "Task.Special.Construction", "4", True, "Persistent", "/Script/GuLiStrike.GuLiConstructionSpecialTaskExecutor"],
        [3, "StrongholdAdvance", "初始授予一次；接管永久移除；未中断连续推进", "据点推进", "Task.Special.StrongholdAdvance", "1,2", True, "InitialOnce", "/Script/GuLiStrike.GuLiStrongholdAdvanceSpecialTaskExecutor"]]
cells = [{"cell": f"{chr(65+c)}{r+1}", "value": value,
          "type": "bool" if isinstance(value, bool) else "int" if isinstance(value, int) else "string"}
         for r, row in enumerate(rows) for c, value in enumerate(row)]
with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", suffix=".json", delete=False) as file:
    json.dump(cells, file, ensure_ascii=False)
    temp = file.name
try:
    run("write", DEST, "--sheet", "Tasks", "--data-file", temp)
finally:
    os.unlink(temp)
run("style", DEST, "--sheet", "Tasks", "--range", "A1:I1", "--bold", "--bg", "D9E1F2", "--wrap")
run("col-width", DEST, "--sheet", "Tasks", "--col", "A", "--to", "I", "--width", "24")
run("col-width", DEST, "--sheet", "Tasks", "--col", "I", "--width", "65")
print("Authored three special tasks with excelize-cli.")
