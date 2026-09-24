import json
import os
from pathlib import Path
import unreal

evidence = Path('D:/UE5.7/test1/Artifacts/MassStutterFix/20260923')
loaded = json.loads((evidence / 'editor-loaded-modules.json').read_text(encoding='utf-8-sig'))
result = {
    'pid': os.getpid(),
    'new_cpp_loaded': loaded['pid'] == os.getpid() and loaded['expected_project_module_loaded'],
    'asset_changes_required': False,
    'assistant_started_pie': False,
}
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_editor_world()
if not world:
    world = unreal.find_object(None, '/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype')
game = editor.get_game_world()
result['editor_world'] = world.get_path_name() if world else None
result['game_world'] = game.get_path_name() if game else None
result['dirty_maps'] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
result['dirty_content'] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
result['entry_actors'] = []
if world:
    for name in ('Note_0', 'GuLiMapMarker_27'):
        actor = unreal.find_object(None, world.get_path_name() + ':PersistentLevel.' + name)
        if not actor:
            continue
        location = actor.get_actor_location()
        result['entry_actors'].append({
            'name': actor.get_name(),
            'label': actor.get_actor_label(),
            'class': actor.get_class().get_path_name(),
            'location': [location.x, location.y, location.z],
        })
(evidence / 'scene-loaded-readback.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result))
