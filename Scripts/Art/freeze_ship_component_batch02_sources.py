"""Freeze Batch 02 source references without saving any UE package."""
import importlib.util
import json
import traceback
from pathlib import Path

PROJECT=Path('D:/UE5.7/test1')
ROOT=PROJECT/'ArtSource/Ships/ShipComponentStyle_20260917/Batch02'
spec=importlib.util.spec_from_file_location('source_capture',PROJECT/'Scripts/Art/freeze_ship_component_style_sources.py')
source=importlib.util.module_from_spec(spec);spec.loader.exec_module(source)
source.ROOT=ROOT
source.SOURCE=ROOT/'Source'
source.REPORT=source.SOURCE/'source_snapshot_v1.json'
source.FBX_DIR=source.SOURCE/'FBX_static_v1'
source.KEYS=('Autocannon','Triple_Barrel_Turret','Single_Barrel_Turret')

if __name__=='__main__':
    try:source.main()
    except Exception:
        ROOT.mkdir(parents=True,exist_ok=True)
        (ROOT/'source_capture_error.json').write_text(json.dumps({'success':False,'traceback':traceback.format_exc()},indent=2),encoding='utf-8')
        raise
