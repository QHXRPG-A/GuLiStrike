"""Legacy command compatibility; the current candidate pipeline owns all LOD authoring."""
import json,runpy,sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
SCRIPT=ROOT/'Scripts/CommanderLOD'

def run(entry,mode='info'):
    delivery_file=ROOT/'ArtSource/CommanderLOD_20261005/formal_delivery.json'
    if delivery_file.exists():
        delivery=json.loads(delivery_file.read_text(encoding='utf8'))
        assert delivery['formal_switched']
        result=dict(success=True,state='retired_historical_entry',lod_count=3,approval_B='approved',formal_switched=True,
            current_source='ArtSource/CommanderLOD_20261005',current_scripts='Scripts/CommanderLOD',
            formal_delivery=str(delivery_file),review='http://127.0.0.1:8709/Review/index.html',
            message='This approved source is frozen. Use verify_formal_group.py for saved formal resources. New modeling/baking needs a new review version.')
        if 'unreal' in sys.modules:sys.modules['unreal'].MCPythonHelper.submit_result(json.dumps(result))
        else:print(json.dumps(result,ensure_ascii=False))
        return result
    if mode=='blender':
        assert 'bpy' in sys.modules,'Run this entry inside Blender.'
    if mode=='unreal':
        assert 'unreal' in sys.modules,'Run this entry through CommanderLOD/ue_rpc.py.'
    if mode in ['blender','unreal','python']:
        target=SCRIPT/entry
        assert target.is_file(),target
        return runpy.run_path(str(target),init_globals={'commander_lod_arguments':{}},run_name='commander_lod_compatibility')
    result=dict(success=True,state='retired_historical_entry',lod_count=3,
        current_source='ArtSource/CommanderLOD_20261005',current_scripts='Scripts/CommanderLOD',
        review='http://127.0.0.1:8709/Review/index.html',
        message='Use current candidate authoring. Formal switching requires approval of the actual version.')
    if 'unreal' in sys.modules:sys.modules['unreal'].MCPythonHelper.submit_result(json.dumps(result))
    else:print(json.dumps(result,ensure_ascii=False))
    return result
