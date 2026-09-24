import json
import os
from pathlib import Path
import unreal

project = Path('D:/UE5.7/test1')
artifact = project / 'Artifacts/CommanderPerformanceHUD/20260923'
launch = json.loads((artifact / 'editor-launch.json').read_text(encoding='utf-8-sig'))
assert os.getpid() == launch['pid']
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert not level.is_in_play_in_editor()
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name() == '/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'
mode_class = world.get_world_settings().get_editor_property('default_game_mode')
mode = unreal.get_default_object(mode_class)
presentation = unreal.get_default_object(unreal.GuLiCommanderPresentationActor)
table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeGameTexts_Texts')
rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
expected = json.loads((project / 'Data/Json/DT_GuLiStrikeGameTexts_Texts.json').read_text(encoding='utf-8'))
assert {r['Name']: r for r in rows} == {r['Name']: r for r in expected}
result = {'pid': os.getpid(), 'pie': False, 'map': world.get_path_name(),
    'game_mode': mode_class.get_path_name(),
    'hud': mode.get_editor_property('hud_class').get_path_name(),
    'controller': mode.get_editor_property('player_controller_class').get_path_name(),
    'correction_speed_multiplier': presentation.get_editor_property('maximum_correction_speed_multiplier'),
    'interpolation_seconds': presentation.get_editor_property('interpolation_back_time_seconds'),
    'max_adaptive_delay_seconds': presentation.get_editor_property('maximum_adaptive_interpolation_back_time_seconds'),
    'max_extrapolation_seconds': presentation.get_editor_property('maximum_extrapolation_seconds'),
    'text_rows': len(rows), 'text_readback_matches_source': True,
    'performance_texts': {r['Name']: r['Content'] for r in rows if r['Name'].startswith('UI.Performance.')},
    'entry_actors': []}
for name in ('Note_0', 'GuLiMapMarker_27'):
    actor = unreal.find_object(None, world.get_path_name() + ':PersistentLevel.' + name)
    assert actor, name
    loc = actor.get_actor_location()
    entry = {'name': name, 'label': actor.get_actor_label(), 'location': [loc.x, loc.y, loc.z]}
    if name == 'Note_0':
        entry['text'] = str(actor.get_editor_property('text'))
    result['entry_actors'].append(entry)
result['dirty_maps'] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
result['dirty_content'] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
assert result['correction_speed_multiplier'] == 3.0
assert not result['dirty_maps'] and not result['dirty_content']
(artifact / 'loaded-scene.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=False))
