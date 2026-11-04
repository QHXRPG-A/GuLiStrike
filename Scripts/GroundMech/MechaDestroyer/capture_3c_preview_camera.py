"""Freeze the already-open Blueprint preview camera before imports or editor restart.

Run in the existing editor only after its new read-only native interface is loaded.
Does not open/focus a viewport, evaluate animation, render, compile or save assets.
"""
import datetime
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
OUT = ROOT / globals().get('GULI_CAMERA_CAPTURE_PATH', f'Artifacts/GroundMobility/camera-{stamp}.json')
BLUEPRINT = '/Game/GuLiStrike/GroundMech/BP_GroundMech_Light'
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
assert not OUT.exists(), 'Preview view is already frozen; preserve this task reference'
helper = unreal.GuLiGroundMechAnimationEditorLibrary
assert hasattr(helper, 'read_blueprint_preview_camera'), 'The new read-only native camera interface is not loaded'
report = json.loads(helper.read_blueprint_preview_camera(unreal.load_asset(BLUEPRINT)))
assert report['success'], report
report['captured_at'] = datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat()
report['scope'] = 'Blueprint preview matrices and actor transform only; no image or gameplay validation'
OUT.parent.mkdir(parents=True, exist_ok=True)
with OUT.open('x', encoding='utf8') as output:
    json.dump(report, output, ensure_ascii=False, indent=2)
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'capture': str(OUT), 'camera': report['spring_arm']}))
