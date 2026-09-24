import json
import os
import time
from pathlib import Path
import unreal

root = Path('D:/UE5.7/test1/Artifacts/MassStutterAfterFix/20260923')
result = {'pid': os.getpid(), 'wall_time': time.time(), 'worlds': []}
for index in range(3):
    world = unreal.find_object(None, '/Game/Maps/UEDPIE_{}_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'.format(index))
    if not world:
        continue
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    row = {'index': index, 'world': world.get_path_name(), 'world_seconds': unreal.GameplayStatics.get_time_seconds(world),
           'delta_seconds': unreal.GameplayStatics.get_world_delta_seconds(world), 'paused': unreal.GameplayStatics.is_game_paused(world)}
    if index == 0:
        unreal.log('GULI_STUTTER_AFTER_FIX_CONTEXT_BEGIN')
        for command in ('gs.GM.Commander.Nav.Stats', 'gs.GM.Commander.Nav.Soldier 1'):
            unreal.SystemLibrary.execute_console_command(world, command)
        unreal.log('GULI_STUTTER_AFTER_FIX_CONTEXT_END')
        mode = unreal.GameplayStatics.get_game_mode(world)
        rep = mode.get_component_by_class(unreal.GuLiCommanderWorldReplicationComponent)
        row['replication_settings'] = {name: rep.get_editor_property(name) for name in (
            'full_rate_pose_distance_centimeters', 'far_moving_pose_frame_divisor', 'stationary_pose_frame_divisor')}
    else:
        snapshot = root / ('client-{}.json'.format(index))
        unreal.SystemLibrary.execute_console_command(world, 'gs.Commander.QA.InputSnapshot "{}"'.format(snapshot.as_posix()), pc)
        row['snapshot'] = str(snapshot)
        presentation = unreal.GameplayStatics.get_actor_of_class(world, unreal.GuLiCommanderPresentationActor)
        row['presentation'] = {name: presentation.get_editor_property(name) for name in (
            'interpolation_back_time_seconds', 'maximum_adaptive_interpolation_back_time_seconds',
            'maximum_extrapolation_seconds', 'hard_snap_distance_centimeters')}
        camera = unreal.GameplayStatics.get_player_camera_manager(world, 0)
        loc = camera.get_camera_location()
        row['camera_location'] = [loc.x, loc.y, loc.z]
        row['camera_fov'] = camera.get_fov_angle()
    result['worlds'].append(row)
result['cvars'] = {name: unreal.SystemLibrary.get_console_variable_float_value(name) for name in (
    't.MaxFPS', 'r.VSync', 't.IdleWhenNotForeground', 'r.ScreenPercentage', 'r.AntiAliasingMethod',
    'sg.ViewDistanceQuality', 'sg.ShadowQuality', 'sg.GlobalIlluminationQuality', 'sg.ReflectionQuality',
    'r.Lumen.HardwareRayTracing', 'r.RayTracing', 'r.Shadow.Virtual.Enable')}
(root / 'context.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result))
