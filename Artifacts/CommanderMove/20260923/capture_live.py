"""Read current PIE worlds and emit existing diagnostics; never issue gameplay input."""
import json
import os
from pathlib import Path
import unreal

root = Path('D:/UE5.7/test1/Artifacts/CommanderMove/20260923')
worlds = []
for index in range(3):
    name = 'UEDPIE_{}_LVL_CommanderMassPrototype'.format(index)
    world = unreal.find_object(None, '/Game/Maps/{}.LVL_CommanderMassPrototype'.format(name))
    if world is None:
        worlds.append({'index': index, 'found': False})
        continue
    controller = unreal.GameplayStatics.get_player_controller(world, 0)
    worlds.append({'index': index, 'world': world.get_path_name(),
                   'controller': controller.get_path_name() if controller else None,
                   'game_mode': str(unreal.GameplayStatics.get_game_mode(world))})
    if index == 0:
        unreal.log('GULI_MOVE_CAPTURE_BEGIN PID={}'.format(os.getpid()))
        commands = ['gs.GM.Commander.Nav.Stats', 'gs.GM.Commander.Nav.LastMove']
        commands += ['gs.GM.Commander.Nav.Soldier {}'.format(sid) for sid in
                     (29, 30, 34, 35, 40, 44, 45, 130, 134, 140, 169, 170, 174, 175, 191)]
        for command in commands:
            unreal.SystemLibrary.execute_console_command(world, command)
        unreal.log('GULI_MOVE_CAPTURE_END')
    else:
        command = 'gs.Commander.QA.InputSnapshot "D:/UE5.7/test1/outputs/commander-move-20260923/client-{}.json"'.format(index)
        unreal.SystemLibrary.execute_console_command(world, command, controller)
result = {'pid': os.getpid(), 'worlds': worlds}
(root / 'live-worlds.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
unreal.log('GULI_MOVE_CAPTURE_WORLDS ' + json.dumps(result))
import runpy
runpy.run_path(str(root / 'capture_paths.py'))
