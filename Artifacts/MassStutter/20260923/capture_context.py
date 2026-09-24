import json
import os
import time
from pathlib import Path
import unreal

root = Path('D:/UE5.7/test1/Artifacts/MassStutter/20260923')
root.mkdir(parents=True, exist_ok=True)
result = {'pid': os.getpid(), 'captured_at': time.time(), 'worlds': []}
for index in range(3):
    path = '/Game/Maps/UEDPIE_{}_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'.format(index)
    world = unreal.find_object(None, path)
    if not world:
        continue
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    row = {'index': index, 'world': path,
           'world_seconds': unreal.GameplayStatics.get_time_seconds(world),
           'delta_seconds': unreal.GameplayStatics.get_world_delta_seconds(world),
           'paused': unreal.GameplayStatics.is_game_paused(world),
           'controller': controller.get_path_name() if controller else None}
    if index == 0:
        unreal.log('GULI_STUTTER_CONTEXT_BEGIN')
        for command in ['gs.GM.Commander.Nav.Stats', 'gs.GM.Commander.Nav.LastMove'] + [
                'gs.GM.Commander.Nav.Soldier {}'.format(sid) for sid in (1,29,40,170,252,272)]:
            unreal.SystemLibrary.execute_console_command(world, command)
        unreal.log('GULI_STUTTER_CONTEXT_END')
    else:
        output = 'D:/UE5.7/test1/outputs/mass-stutter-20260923/client-{}.json'.format(index)
        unreal.SystemLibrary.execute_console_command(world, 'gs.Commander.QA.InputSnapshot "{}"'.format(output), controller)
        row['snapshot'] = output
        presentation = unreal.GameplayStatics.get_actor_of_class(world, unreal.GuLiCommanderPresentationActor)
        if presentation:
            row['presentation'] = {name: presentation.get_editor_property(name) for name in (
                'interpolation_back_time_seconds', 'maximum_adaptive_interpolation_back_time_seconds',
                'maximum_extrapolation_seconds', 'hard_snap_distance_centimeters')}
    result['worlds'].append(row)
(root / 'context.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result))
