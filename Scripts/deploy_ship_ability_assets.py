"""Compatibility entry for the component skill asset workflow; Wingman tuning is retained."""
import runpy
from pathlib import Path
import unreal

runpy.run_path(str(Path(unreal.Paths.project_dir()) / "Scripts/author_component_skill_assets.py"))["author"]()
