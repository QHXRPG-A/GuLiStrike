"""Compatibility entry: regenerate the current SC2-inspired commander console."""
from pathlib import Path
import runpy
import unreal
root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
runpy.run_path(str(root / "Scripts/build_commander_sc2_ui.py"), run_name="__main__")
