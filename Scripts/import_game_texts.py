"""Import the game-copy workbook through the standard pipeline and retain full round-trip evidence."""
import json
import runpy
from pathlib import Path
import unreal

project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
    raise RuntimeError("Stop PIE before importing game text; runtime table replacement is unsupported")
runpy.run_path(str(project / "Scripts/import_data_to_engine.py"),
              init_globals={"GULI_TABLE_FILTER": {"DT_GuLiStrikeGameTexts_Texts"}})
report = json.loads((project / "data/tmp_import_report.json").read_text(encoding="utf-8"))
if report["errors"] or len(report["tables"]) != 1 or not report["tables"][0].get("imported"):
    raise RuntimeError(json.dumps(report, ensure_ascii=False))
dest = project / "outputs/commander-ui-20260920/game-text-import.json"
dest.parent.mkdir(parents=True, exist_ok=True)
dest.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print("GAME_TEXT_IMPORT_OK", dest)
