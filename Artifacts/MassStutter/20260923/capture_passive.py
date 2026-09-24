import json
import time
from pathlib import Path
import unreal

_stutter_root = Path('D:/UE5.7/test1/Artifacts/MassStutter/20260923')
_stutter_world = unreal.find_object(None, '/Game/Maps/UEDPIE_0_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype')
assert _stutter_world is not None
_stutter_started = time.perf_counter()
_stutter_frames = []
_stutter_last_time = -1.0
_stutter_meta = {'mode': 'passive_existing_pie', 'duration_seconds': 18, 'soldier_id': 1, 'traces': []}
_stutter_mode = unreal.GameplayStatics.get_game_mode(_stutter_world)
_stutter_rep = _stutter_mode.get_component_by_class(unreal.GuLiCommanderWorldReplicationComponent)
_stutter_meta['replication_settings'] = {name: _stutter_rep.get_editor_property(name) for name in (
    'full_rate_pose_distance_centimeters', 'far_moving_pose_frame_divisor', 'stationary_pose_frame_divisor')}
for index, duration in ((1, 12), (2, 16)):
    world = unreal.find_object(None, '/Game/Maps/UEDPIE_{}_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'.format(index))
    if world:
        controller = unreal.GameplayStatics.get_player_controller(world, 0)
        unreal.SystemLibrary.execute_console_command(world, 'gs.Commander.PredictionTrace.Start baseline 1 {}'.format(duration), controller)
        _stutter_meta['traces'].append({'client': index, 'duration': duration, 'world': world.get_path_name()})

def _stutter_tick(_delta):
    global _stutter_last_time
    try:
        elapsed = time.perf_counter() - _stutter_started
        world_time = unreal.GameplayStatics.get_time_seconds(_stutter_world)
        if world_time != _stutter_last_time:
            _stutter_frames.append({'wall_seconds': elapsed, 'world_seconds': world_time,
                                    'world_delta_seconds': unreal.GameplayStatics.get_world_delta_seconds(_stutter_world)})
            _stutter_last_time = world_time
        if elapsed >= 18.0 or len(_stutter_frames) >= 2000:
            unreal.unregister_slate_post_tick_callback(_stutter_handle)
            (_stutter_root / 'frames.json').write_text(json.dumps({'meta': _stutter_meta, 'frames': _stutter_frames}, indent=2), encoding='utf-8')
            unreal.log('GULI_STUTTER_PASSIVE_DONE samples={}'.format(len(_stutter_frames)))
    except Exception as error:
        unreal.unregister_slate_post_tick_callback(_stutter_handle)
        (_stutter_root / 'frames-error.json').write_text(json.dumps({'error': str(error)}, indent=2), encoding='utf-8')

_stutter_handle = unreal.register_slate_post_tick_callback(_stutter_tick)
(_stutter_root / 'passive-start.json').write_text(json.dumps(_stutter_meta, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(_stutter_meta))
