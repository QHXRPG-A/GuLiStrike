"""Bounded passive sampling of the user's running PIE; no gameplay input."""
import json
import time
import os
import ctypes
from pathlib import Path
import unreal

_ms_dir = Path('D:/UE5.7/test1/Artifacts/MassStateBatch/20260923/monitor')
_ms_output = Path('D:/UE5.7/test1/outputs/mass-state-batch-monitor-20260923')
assert not (_ms_dir / 'capture.json').exists(), 'Preserve prior evidence.'
_ms_worlds = unreal.EditorLevelLibrary.get_pie_worlds(True)
assert len(_ms_worlds) == 3
_ms_server = next(w for w in _ms_worlds if unreal.GameplayStatics.get_game_mode(w))
_ms_clients = [w for w in _ms_worlds if w != _ms_server]
_ms_rosters = [unreal.GameplayStatics.get_actor_of_class(w, unreal.GuLiSoldierStateReplicator) for w in _ms_worlds]
assert all(_ms_rosters)
_ms_restore = json.loads((_ms_dir.parent / 'monitor-start.json').read_text(encoding='utf-8-sig'))['result']['diagnostics_before']

def _ms_states():
    result = []
    for world, roster in zip(_ms_worlds, _ms_rosters):
        states = []
        for state in roster.get_all_soldier_states():
            states.append({'id': int(state.get_editor_property('SoldierId').get_editor_property('Value')),
                           'team': str(state.get_editor_property('Team')),
                           'life': str(state.get_editor_property('LifeState')), 'health': float(state.get_editor_property('Health')),
                           'order': int(state.get_editor_property('ActiveOrderId')), 'revision': int(state.get_editor_property('StateRevision'))})
        result.append({'world': world.get_path_name(), 'states': sorted(states, key=lambda s: s['id'])})
    return result

_ms_initial = _ms_states()
_ms_snapshot = json.loads((_ms_output / 'client2-start.json').read_text(encoding='utf-8-sig'))
_ms_screen = {s['id'] for s in _ms_snapshot['soldiers'] if s['on_screen']}
_ms_active = [s['id'] for s in _ms_initial[0]['states'] if s['order'] and s['health'] > 0]
assert _ms_active, 'No active movement orders to sample.'
_ms_sid = next((sid for sid in _ms_active if sid in _ms_screen), _ms_active[0])
_ms_meta = {'wall_start': time.time(), 'perf_start': time.perf_counter(), 'pid': os.getpid(),
            'frame_start': unreal.SystemLibrary.get_frame_count(), 'duration_seconds': 40,
            'trace_soldier': _ms_sid, 'initial_active_orders': len(_ms_active),
            'input_injected': False, 'prior_diagnostics': _ms_restore,
            'cvars': {n: unreal.SystemLibrary.get_console_variable_float_value(n)
                      for n in ('t.MaxFPS', 'r.VSync', 't.IdleWhenNotForeground')}}
_ms_frames = []
_ms_user32 = ctypes.windll.user32
_ms_user32.GetForegroundWindow.restype = ctypes.c_void_p
_ms_user32.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]

unreal.log('GULI_STATE_BATCH_MONITOR_BEGIN frame={} soldier={}'.format(_ms_meta['frame_start'], _ms_sid))
for world, duration in zip(_ms_clients, (32, 35)):
    unreal.SystemLibrary.execute_console_command(world, 'gs.Commander.PredictionTrace.Start baseline {} {}'.format(_ms_sid, duration), unreal.GameplayStatics.get_player_controller(world, 0))

def _ms_finish(error=None):
    unreal.unregister_slate_post_tick_callback(_ms_handle)
    restore_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if restore_world is None:
        restore_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(restore_world, 'guli.Commander.StateStreamDiagnostics {}'.format(_ms_restore))
    _ms_meta.update(wall_end=time.time(), frame_end=unreal.SystemLibrary.get_frame_count(), error=error,
                    restored_diagnostics=unreal.SystemLibrary.get_console_variable_int_value('guli.Commander.StateStreamDiagnostics'))
    final = []
    if error is None:
        final = _ms_states()
        for i, world in enumerate(_ms_clients, 1):
            unreal.SystemLibrary.execute_console_command(world, 'gs.Commander.QA.InputSnapshot "{}"'.format((_ms_output / ('client{}-end.json'.format(i))).as_posix()), unreal.GameplayStatics.get_player_controller(world, 0))
    (_ms_dir / 'capture.json').write_text(json.dumps({'meta': _ms_meta, 'initial': _ms_initial, 'final': final, 'frames': _ms_frames}, indent=2), encoding='utf-8')
    unreal.log('GULI_STATE_BATCH_MONITOR_END frame={} error={}'.format(_ms_meta['frame_end'], error))

def _ms_tick(delta):
    started = time.perf_counter()
    try:
        if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
            _ms_finish('User ended PIE during capture')
            return
        active_pid = ctypes.c_ulong()
        _ms_user32.GetWindowThreadProcessId(_ms_user32.GetForegroundWindow(), ctypes.byref(active_pid))
        _ms_frames.append({'elapsed': started - _ms_meta['perf_start'], 'wall': time.time(),
                           'frame': unreal.SystemLibrary.get_frame_count(),
                           'world_time': unreal.GameplayStatics.get_time_seconds(_ms_server),
                           'delta_seconds': unreal.GameplayStatics.get_world_delta_seconds(_ms_server),
                           'editor_process_foreground': active_pid.value == os.getpid(),
                           'capture_ms': (time.perf_counter() - started) * 1000})
        if started - _ms_meta['perf_start'] >= 40:
            _ms_finish()
    except Exception as exc:
        _ms_finish(str(exc))

_ms_handle = unreal.register_slate_post_tick_callback(_ms_tick)
(_ms_dir / 'capture-start.json').write_text(json.dumps(_ms_meta, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(_ms_meta))
