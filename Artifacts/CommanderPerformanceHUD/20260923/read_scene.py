import json
import os
from pathlib import Path
import unreal

result = {'pid': os.getpid(), 'new_hud_code_loaded': False, 'worlds': []}
editor_world = unreal.find_object(None, '/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype')
result['editor_map'] = editor_world.get_path_name() if editor_world else None
result['dirty_maps'] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
result['dirty_content'] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
for index in range(3):
    world = unreal.find_object(None, '/Game/Maps/UEDPIE_{}_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'.format(index))
    if not world:
        continue
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    hud = pc.get_hud() if pc else None
    mode = unreal.GameplayStatics.get_game_mode(world)
    result['worlds'].append({'map': world.get_path_name(),
                             'game_mode': mode.get_class().get_path_name() if mode else None,
                             'controller': pc.get_class().get_path_name() if pc else None,
                             'hud': hud.get_class().get_path_name() if hud else None})
result['entry_actors'] = []
if editor_world:
    for name in ('Note_0', 'GuLiMapMarker_27'):
        actor = unreal.find_object(None, editor_world.get_path_name() + ':PersistentLevel.' + name)
        if actor:
            loc = actor.get_actor_location()
            result['entry_actors'].append({'name': name, 'label': actor.get_actor_label(), 'location': [loc.x, loc.y, loc.z]})
Path('D:/UE5.7/test1/Artifacts/CommanderPerformanceHUD/20260923/scene-readback.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result))
