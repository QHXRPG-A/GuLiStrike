import json
import time
from pathlib import Path
import unreal

_perf_root = Path('D:/UE5.7/test1/Artifacts/MassStutterAfterFix/20260923')
_perf_world = unreal.find_object(None, '/Game/Maps/UEDPIE_0_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype')
assert _perf_world is not None
_perf_trace = _perf_root / 'current-session.utrace'
assert not _perf_trace.exists()
_perf_started = time.perf_counter()
_perf_frames = []
_perf_last_time = -1.0
_perf_meta = {'mode': 'passive_existing_pie', 'duration_seconds': 20, 'qpc_begin': _perf_started,
              'wall_begin': time.time(), 'trace': str(_perf_trace), 'world': _perf_world.get_path_name()}
unreal.SystemLibrary.execute_console_command(_perf_world, 'Trace.File {} cpu,gpu,frame,bookmark'.format(_perf_trace.as_posix()))
unreal.SystemLibrary.execute_console_command(_perf_world, 'Trace.Bookmark MassStutterAfterFixBegin')

def _perf_tick(_delta):
    global _perf_last_time
    try:
        elapsed = time.perf_counter() - _perf_started
        world_time = unreal.GameplayStatics.get_time_seconds(_perf_world)
        if world_time != _perf_last_time:
            _perf_frames.append({'wall_seconds': elapsed, 'world_seconds': world_time,
                                 'world_delta_seconds': unreal.GameplayStatics.get_world_delta_seconds(_perf_world)})
            _perf_last_time = world_time
        if elapsed >= 20.0:
            unreal.unregister_slate_post_tick_callback(_perf_handle)
            _perf_meta['qpc_end'] = time.perf_counter()
            _perf_meta['wall_end'] = time.time()
            unreal.SystemLibrary.execute_console_command(_perf_world, 'Trace.Bookmark MassStutterAfterFixEnd')
            unreal.SystemLibrary.execute_console_command(_perf_world, 'Trace.Stop')
            (_perf_root / 'frames.json').write_text(json.dumps({'meta': _perf_meta, 'frames': _perf_frames}, indent=2), encoding='utf-8')
            unreal.log('GULI_STUTTER_AFTER_FIX_PERF_DONE samples={}'.format(len(_perf_frames)))
    except Exception as error:
        unreal.unregister_slate_post_tick_callback(_perf_handle)
        unreal.SystemLibrary.execute_console_command(_perf_world, 'Trace.Stop')
        (_perf_root / 'frames-error.json').write_text(json.dumps({'error': str(error)}, indent=2), encoding='utf-8')

_perf_handle = unreal.register_slate_post_tick_callback(_perf_tick)
(_perf_root / 'perf-start.json').write_text(json.dumps(_perf_meta, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(_perf_meta))
