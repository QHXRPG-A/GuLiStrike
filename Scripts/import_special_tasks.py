"""Run with UE Python commandlet after native compilation; scoped standard-pipeline import."""
import json
import runpy
from pathlib import Path
import unreal

project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
runpy.run_path(str(project / "Scripts/import_data_to_engine.py"),
              init_globals={"GULI_TABLE_FILTER": {"DT_GuLiStrikeSpecialTasks_Tasks"}})
report = json.loads((project / "data/tmp_import_report.json").read_text(encoding="utf-8"))
if report["errors"] or len(report["tables"]) != 1 or not report["tables"][0].get("imported"):
    raise RuntimeError(json.dumps(report, ensure_ascii=False))
rows = json.loads((project / "data/Json/DT_GuLiStrikeSpecialTasks_Tasks.json").read_text(encoding="utf-8"))
report["native_classes"] = {}
for row in rows:
    cls = unreal.load_class(None, row["ExecutorClass"])
    if not cls:
        raise RuntimeError(f"Failed to load {row['ExecutorClass']}")
    report["native_classes"][row["Name"]] = cls.get_path_name()
dest = project / "TestResults/CommanderOrders/special_task_import.json"
dest.parent.mkdir(parents=True, exist_ok=True)
dest.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print("SPECIAL_TASK_IMPORT_OK", dest)
