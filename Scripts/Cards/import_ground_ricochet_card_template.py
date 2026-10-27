"""Compatibility entry; authoring values live only in the artwork Excel catalog."""
import sys
from pathlib import Path
import unreal
root=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(root/'Scripts/Cards'))
import card_artwork_pipeline
import importlib
importlib.reload(card_artwork_pipeline)
card_artwork_pipeline.execute('deploy',[6])
