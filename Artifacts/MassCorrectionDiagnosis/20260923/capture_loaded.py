"""Passive current-PIE network attribution. No gameplay input or asset writes."""
import json
import time
from pathlib import Path
import unreal

_nb_root = Path('D:/UE5.7/test1/Artifacts/MassCorrectionDiagnosis/20260923')
assert not (_nb_root / 'loaded-start.json').exists(), 'Do not overwrite evidence'
_nb_world = unreal.find_object(None, '/Game/Maps/UEDPIE_0_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype')
assert _nb_world and unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
_nb_sample_file = Path('D:/UE5.7/test1/TestResults/WingmanAttack/pie-sample.json')
_nb_previous_sample = _nb_sample_file.read_bytes() if _nb_sample_file.exists() else None
if _nb_previous_sample is not None:
    (_nb_root / 'loaded-preexisting-pie-sample.json').write_bytes(_nb_previous_sample)

def _nb_command(command):
    unreal.SystemLibrary.execute_console_command(_nb_world, command)

def _nb_sample(name):
    _nb_command('gs.WingmanAttack.QA.Sample')
    if _nb_sample_file.exists():
        (_nb_root / name).write_bytes(_nb_sample_file.read_bytes())

_nb_sample('loaded-counters-start.json')
_nb_meta = {'start_wall': time.time(), 'start_perf': time.perf_counter(),
    'start_frame': unreal.SystemLibrary.get_frame_count(),
    'world_start': unreal.GameplayStatics.get_time_seconds(_nb_world),
    'input_injected': False, 'duration_seconds': 30, 'prior_log_net': 'Log'}
unreal.log('GULI_NETWORK_LOADED_BEGIN frame={}'.format(_nb_meta['start_frame']))
_nb_command('Log LogNet VeryVerbose')
_nb_command('getall IpConnection CurrentNetSpeed')
_nb_command('netprofile AUTOSTOP TIME=32')
_nb_command('CsvProfile STARTFILE=../../../Artifacts/MassCorrectionDiagnosis/20260923/loaded-engine.csv')
_nb_command('CsvProfile START')
_nb_frames = []
_nb_last_marker = -1

def _nb_finish(error=None):
    unreal.unregister_slate_post_tick_callback(_nb_handle)
    _nb_command('netprofile DISABLE')
    _nb_command('CsvProfile STOP')
    _nb_command('Log LogNet Log')
    _nb_sample('loaded-counters-end.json')
    if _nb_previous_sample is not None:
        _nb_sample_file.write_bytes(_nb_previous_sample)
    _nb_meta.update(end_wall=time.time(), end_frame=unreal.SystemLibrary.get_frame_count(),
        restored_log_net='Log', error=error)
    (_nb_root / 'loaded-capture.json').write_text(json.dumps({'meta': _nb_meta, 'frames': _nb_frames}, indent=2), encoding='utf-8')
    unreal.log('GULI_NETWORK_LOADED_END frame={}'.format(_nb_meta['end_frame']))

def _nb_tick(delta):
    global _nb_last_marker
    try:
        elapsed = time.perf_counter() - _nb_meta['start_perf']
        frame = unreal.SystemLibrary.get_frame_count()
        _nb_frames.append([frame, elapsed, time.time(), unreal.GameplayStatics.get_time_seconds(_nb_world),
            unreal.GameplayStatics.get_world_delta_seconds(_nb_world)])
        marker = int(elapsed / 2)
        if marker != _nb_last_marker:
            _nb_last_marker = marker
            unreal.log('GULI_NETWORK_LOADED_TICK frame={} elapsed={:.6f}'.format(frame, elapsed))
        if elapsed >= 30:
            _nb_finish()
    except Exception as exc:
        _nb_finish(str(exc))

_nb_handle = unreal.register_slate_post_tick_callback(_nb_tick)
(_nb_root / 'loaded-start.json').write_text(json.dumps(_nb_meta, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(_nb_meta))
