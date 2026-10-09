"""Author and capture isolated material comparisons in one normal editor session."""
import runpy
from pathlib import Path
import unreal

scripts=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))/'Scripts/SceneUI'
runpy.run_path(str(scripts/'author_color_review.py'))
runpy.run_path(str(scripts/'capture_color_review.py'),init_globals={'PREPARE_SCENE_AFTER_CAPTURE':True})
