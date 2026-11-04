"""Apply the prepared 3C change after native loading and preview-camera capture.

Uses the standard scoped import, gait/contact calibration and manifest export.
No gameplay, animation pose probes or image inspection are executed.
"""
import json
import shutil
import subprocess
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Artifacts/Ground3C_20261103'
CAPTURE = OUT / 'blueprint-preview-camera.json'
MOTION_ONLY = bool(globals().get('GULI_3C_MOTION_ONLY', False))
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
assert MOTION_ONLY or CAPTURE.is_file(), 'Freeze the requested Blueprint preview before changing camera assets'
assert hasattr(unreal.GuLiGroundMechAnimationEditorLibrary, 'read_blueprint_preview_camera'), 'New editor code is not loaded'
row = unreal.GuLiStrikeMechMobilityRow()
assert hasattr(row, 'strafe_speed_multiplier'), 'New runtime Mobility schema is not loaded'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0] == '/Game/Maps/LVL_CommanderMassPrototype'


def run(*arguments):
    result = subprocess.run([shutil.which('python'), *map(str, arguments)], cwd=ROOT,
                            capture_output=True, encoding='utf8', errors='replace',
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    if result.returncode:
        raise RuntimeError(result.stdout + '\n' + result.stderr)


run(ROOT / 'Scripts/GroundMech/MechaDestroyer/author_3c_tuning.py', *(() if MOTION_ONLY else ('--camera-capture', CAPTURE)))
run(ROOT / 'Tools/DataPipeline/export_data_from_excel.py')
table_names = ('Skills', 'Mobility', 'Gait', 'GroundIK') + (() if MOTION_ONLY else ('Camera',))
report_path = OUT / ('motion-asset-import.json' if MOTION_ONLY else 'asset-import.json')
scope = {'__name__': 'ground_3c_import',
         'GULI_TABLE_FILTER': {'DT_GuLiStrikeMech_' + name for name in table_names},
         'GULI_SYNC_MECH_CAMERA_DEFAULTS': not MOTION_ONLY,
         'GULI_IMPORT_REPORT': str(report_path),
         'GULI_IMPORT_PROGRESS': str(OUT / 'asset-import.log')}
source = ROOT / 'Scripts/import_data_to_engine.py'
exec(compile(source.read_text(encoding='utf8'), str(source), 'exec'), scope)
report = json.loads(report_path.read_text(encoding='utf8'))
assert not report['errors'] and len(report['tables']) == len(table_names)
assert all(table['imported'] for table in report['tables'])
assert report['ground_mobility']['success']
tuning_path = OUT / 'source-tuning.json'
tuning = json.loads(tuning_path.read_text(encoding='utf8'))
tuning.update(ue_imported=not MOTION_ONLY, motion_imported=True,
              configuration_fingerprint=report['ground_mobility']['configuration_fingerprint'])
tuning_path.write_text(json.dumps(tuning, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'report': str(report_path), 'camera_pending': MOTION_ONLY,
    'fingerprint': report['ground_mobility']['configuration_fingerprint'], 'pie_started': False}))
