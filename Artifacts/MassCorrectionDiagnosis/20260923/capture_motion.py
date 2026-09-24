import json
import time
from pathlib import Path
import unreal

_diag_root = Path('D:/UE5.7/test1/Artifacts/MassCorrectionDiagnosis/20260923')
_diag_worlds = [unreal.find_object(None, '/Game/Maps/UEDPIE_{}_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'.format(i)) for i in range(3)]
assert all(_diag_worlds)
_diag_start = time.perf_counter()
_diag_meta = {'wall_start': time.time(), 'mode': 'passive_existing_pie', 'soldier_id': 1, 'duration_seconds': 30, 'worlds': []}
for i, w in enumerate(_diag_worlds):
    pc = unreal.GameplayStatics.get_player_controller(w, 0)
    state = unreal.GameplayStatics.get_game_state(w)
    speed_getters = [name for name in dir(state) if name.startswith('get_effective_soldier_move_speed')]
    _diag_meta['worlds'].append({'index': i, 'world_seconds': unreal.GameplayStatics.get_time_seconds(w),
        'game_state': state.get_class().get_path_name(),
        'standard_move_speed': getattr(state, speed_getters[0])() if speed_getters else None})
    if i:
        unreal.SystemLibrary.execute_console_command(w, 'gs.Commander.PredictionTrace.Start baseline 1 30', pc)
    else:
        unreal.log('GULI_CORRECTION_MOTION_BEGIN')
        for cmd in ('gs.GM.Commander.Nav.Stats', 'gs.GM.Commander.Nav.Soldier 1'):
            unreal.SystemLibrary.execute_console_command(w, cmd)
_diag_frames = []

def _diag_tick(_delta):
    try:
        elapsed = time.perf_counter() - _diag_start
        _diag_frames.append({'wall_seconds': elapsed,
            'world_seconds': unreal.GameplayStatics.get_time_seconds(_diag_worlds[0]),
            'world_delta_seconds': unreal.GameplayStatics.get_world_delta_seconds(_diag_worlds[0])})
        if elapsed >= 31.0:
            unreal.unregister_slate_post_tick_callback(_diag_handle)
            for cmd in ('gs.GM.Commander.Nav.Stats', 'gs.GM.Commander.Nav.Soldier 1'):
                unreal.SystemLibrary.execute_console_command(_diag_worlds[0], cmd)
            unreal.log('GULI_CORRECTION_MOTION_END')
            _diag_meta['wall_end'] = time.time()
            (_diag_root / 'motion-frames.json').write_text(json.dumps({'meta': _diag_meta, 'frames': _diag_frames}, indent=2), encoding='utf-8')
    except Exception as e:
        unreal.unregister_slate_post_tick_callback(_diag_handle)
        (_diag_root / 'motion-capture-error.txt').write_text(str(e), encoding='utf-8')

_diag_handle = unreal.register_slate_post_tick_callback(_diag_tick)
(_diag_root / 'motion-start.json').write_text(json.dumps(_diag_meta, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(_diag_meta))
