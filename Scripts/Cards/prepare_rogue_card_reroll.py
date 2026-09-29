"""Import approved card/text data and save the existing F4 entry, without running gameplay."""
import json
import runpy
import sys
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/RogueCards/Reroll'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
NOTE = 'RogueCards_F4_Entry'
REROLL_NOTE = ('重选验证：F4打开后，底部右侧“重选”免费更换当前候选，优先换成未显示的合格牌；'
               '选择前或翻面完成后可用，入场/翻面/提交期间禁用。没有其他合格牌时保留原牌并提示。'
               '重选不会发奖励，单击翻牌后再次确认才结算；Esc关闭再开保持最新候选。'
               '当前5张卡中，初始合格牌只有01.01/02.01/04.01；获得04.01后，合格池扩展为01.01/02.01/03.01/05.01，可观察换牌。'
               '本次仅保存配置和核对实体；重选原生代码需编译加载后由玩家验证。')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    report = {'success': False, 'PIE': 'not_run', 'native_reroll_load': 'pending_build', 'visual': 'not_run'}
    try:
        editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        assert editor.get_game_world() is None, 'Do not edit during gameplay'
        assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map work'
        sys.path.insert(0, str(ROOT / 'Scripts/Cards'))
        entry = runpy.run_path(str(ROOT / 'Scripts/Cards/author_rogue_card_entry.py'))
        report['tables'] = entry['tables']()
        cards = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeRogueCards_Cards')
        card_rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(cards))
        report['card_ids'] = sorted(row['Id'] for row in card_rows)
        assert report['card_ids'] == ['01.01', '02.01', '03.01', '04.01', '05.01']
        texts = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeGameTexts_Texts')
        text_rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(texts))
        expected = json.loads((OUT / 'text-source.json').read_text(encoding='utf-8'))['keys']
        actual = {row['TextId']: row['Content'] for row in text_rows}
        assert all(actual.get(key) == value for key, value in expected.items()), 'Reroll text readback mismatch'
        report['texts'] = {key: actual[key] for key in expected}
        strings = unreal.load_asset('/Game/GuLiStrike/Data/ST_GuLiStrikeGameTexts')
        report['string_table'] = {key: unreal.StringTableLibrary.get_table_entry_source_string(strings.get_path_name(), key)
                                  for key in expected}
        assert report['string_table'] == expected, 'StringTable readback mismatch'
        assert level.load_level(MAP), 'Load the existing commander map'
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        marker = next((actor for actor in actors.get_all_level_actors() if actor.get_actor_label() == NOTE), None)
        assert marker is not None, 'Existing F4 entry marker is missing'
        before = marker.get_editor_property('text')
        base = entry['ENTRY_INSTRUCTIONS']
        marker.set_editor_property('text', base + '\n' + REROLL_NOTE)
        assert level.save_current_level(), 'Save F4 entry'
        assert level.load_level(MAP), 'Reload saved F4 entry'
        marker = next(actor for actor in actors.get_all_level_actors() if actor.get_actor_label() == NOTE)
        assert marker.get_editor_property('text').endswith(REROLL_NOTE), 'Saved marker text mismatch'
        world = editor.get_editor_world()
        game_mode = world.get_world_settings().get_editor_property('default_game_mode')
        game_mode = game_mode or unreal.load_class(None, '/Script/GuLiStrike.GuLiCommanderGameMode')
        controller_class = unreal.get_default_object(game_mode).get_editor_property('player_controller_class')
        presentation = unreal.get_default_object(controller_class).get_component_by_class(unreal.GuLiRogueCardPresentation)
        assert presentation is not None, 'Existing F4 presentation component is missing'
        report['scene'] = {'map': world.get_path_name(), 'saved_and_reloaded': True, 'actor': marker.get_path_name(),
                           'label': NOTE, 'location': str(marker.get_actor_location()),
                           'instructions': marker.get_editor_property('text'), 'previous_instructions': before,
                           'game_mode': game_mode.get_path_name(), 'controller': controller_class.get_path_name(),
                           'presentation_component': presentation.get_class().get_path_name()}
        report['success'] = True
    except Exception:
        report['error'] = traceback.format_exc()
    (OUT / 'scene-readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2, default=str)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
