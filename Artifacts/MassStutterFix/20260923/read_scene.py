import json
import os
from pathlib import Path
import unreal

result = {'pid': os.getpid(), 'new_cpp_loaded': False, 'asset_changes_required': False}
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_editor_world()
result['editor_world'] = world.get_path_name() if world else None
game = editor.get_game_world()
result['game_world'] = game.get_path_name() if game else None
result['dirty_maps'] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
result['dirty_content'] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
result['entry_actors'] = []
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if actor.get_actor_label() in ('StateTreeReview_Entry', 'Outpost_R2C4') or actor.get_name() in ('Note_0', 'GuLiMapMarker_27'):
        location = actor.get_actor_location()
        result['entry_actors'].append({'name': actor.get_name(), 'label': actor.get_actor_label(),
                                     'class': actor.get_class().get_path_name(),
                                     'location': [location.x, location.y, location.z]})
result['client_game_states'] = []
for index in (1, 2):
    client = unreal.find_object(None, '/Game/Maps/UEDPIE_{}_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'.format(index))
    if client:
        state = unreal.GameplayStatics.get_game_state(client)
        result['client_game_states'].append({'world': client.get_path_name(), 'class': state.get_class().get_path_name(),
                                            'speed_cm_s': state.get_effective_soldier_move_speed_cm_per_second()})
Path('D:/UE5.7/test1/Artifacts/MassStutterFix/20260923/scene-readback.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result))
