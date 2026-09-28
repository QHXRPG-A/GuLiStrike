"""Compatibility entry: read and validate existing StateTrees; never regenerate them.

For the explicit one-time V2 migration use migrate_commander_state_trees_v2.py.
"""
import runpy
from pathlib import Path
import unreal

root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
runpy.run_path(str(root / 'Scripts/inspect_commander_state_trees.py'), run_name='__main__')
