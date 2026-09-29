"""Deploy approved single-line text after the matching native module has been built and loaded."""
import hashlib
import importlib
import json
import sys
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/RogueCards/SingleLineText'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
NOTE = 'RogueCards_F4_Entry'
SCENE_NOTE = ('单行文案验证：F4打开五种卡牌；卡名独立一行，说明仅一行。'
              '单位、增益属性及数值为黄色加粗，“解锁”为暖白；不显示叠加或获取次数说明。'
              '取得导弹仓后可见导弹伤害与雨点攻势；重选后仍保持上述样式。'
              '检查不同窗口比例下说明完整、无换行或卡框溢出。')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    report = {'success': False, 'PIE': 'not_run', 'visual': 'not_run', 'player_acceptance': 'pending'}
    try:
        require_native = json.loads((OUT / 'build-editor-styles-result.json').read_text(encoding='utf-8-sig'))
        source = ROOT / 'Source/GuLiStrike/Gameplay/Cards/GuLiRogueCardPresentation.cpp'
        assert require_native['exit_code'] == 0, 'Build matching native code first'
        assert require_native['source_sha256'] == hashlib.sha256(source.read_bytes()).hexdigest(), 'Native source changed since build'
        style_header = ROOT / 'Source/GuLiStrike/Gameplay/Data/Generated/GuLiStrikeRogueCardUITableRows.h'
        assert require_native['style_header_sha256'] == hashlib.sha256(style_header.read_bytes()).hexdigest(), 'Style schema changed since build'
        assert not unreal.WidgetService.is_pie_running(), 'Do not change assets during gameplay'
        assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map work'
        editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        assert editor.get_editor_world() is not None, 'An idle editor world is required'
        sys.path.insert(0, str(ROOT / 'Scripts/Cards'))
        import card_rich_text
        importlib.reload(card_rich_text)
        owned_packages = {card_rich_text.WIDGET, card_rich_text.STYLE,
                          card_rich_text.SOURCE_STYLE,
                          '/Game/GuLiStrike/Data/DT_GuLiStrikeGameTexts_Texts',
                          '/Game/GuLiStrike/Data/ST_GuLiStrikeGameTexts'}
        dirty = {package.get_path_name() for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
        assert not dirty.intersection(owned_packages), 'Preserve unsaved changes to owned text assets'
        report['style_data_table'] = card_rich_text.import_style_source()
        report['widget'] = card_rich_text.configure_single_line_widget()
        texts = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeGameTexts_Texts')
        assert texts is not None, 'Update the existing public text table in place'
        # AssetTools/CSVImportFactory replaces the UObject and can assert when
        # an ended PIE session still retains a reference. Keep this table object.
        assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(
            texts, (ROOT / 'Data/Json/DT_GuLiStrikeGameTexts_Texts.json').read_text(encoding='utf-8'),
            texts.get_editor_property('row_struct')), 'Fill public text rows in place'
        assert unreal.EditorAssetLibrary.save_loaded_asset(texts, False), 'Save public text rows'
        strings = unreal.load_asset('/Game/GuLiStrike/Data/ST_GuLiStrikeGameTexts')
        assert unreal.GuLiRogueCardPresentationLibrary.rebuild_game_text_string_table(strings, texts)
        assert unreal.EditorAssetLibrary.save_loaded_asset(strings, False)
        # Read the current export, never the original migration snapshot. Later
        # edits to Excel text and style tags must flow through without reseeding.
        exported_texts = {row['TextId']: row['Content'] for row in json.loads(
            (ROOT / 'Data/Json/DT_GuLiStrikeGameTexts_Texts.json').read_text(encoding='utf-8'))}
        exported_cards = json.loads((ROOT / 'Data/Json/DT_GuLiStrikeRogueCards_Cards.json').read_text(encoding='utf-8'))
        expected = {row['TextIds'][1]: exported_texts[row['TextIds'][1]] for row in exported_cards}
        table_values = {row['TextId']: row['Content'] for row in json.loads(
            unreal.DataTableFunctionLibrary.export_data_table_to_json_string(texts))}
        report['text_table'] = {key: table_values[key] for key in expected}
        report['string_table'] = {key: unreal.StringTableLibrary.get_table_entry_source_string(strings.get_path_name(), key)
                                  for key in expected}
        assert report['text_table'] == expected and report['string_table'] == expected, 'Text readback mismatch'
        report['widget_readback'] = card_rich_text.readback()
        level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        world = editor.get_editor_world()
        if not world.get_path_name().startswith(MAP + '.'):
            assert level.load_level(MAP), 'Load existing F4 map'
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        marker = next((actor for actor in actors.get_all_level_actors() if actor.get_actor_label() == NOTE), None)
        assert marker is not None, 'Existing F4 entry marker is required'
        before = marker.get_editor_property('text')
        if SCENE_NOTE not in before:
            marker.set_editor_property('text', before + '\n' + SCENE_NOTE)
        assert level.save_current_level(), 'Save actual F4 entry'
        assert level.load_level(MAP), 'Reload saved F4 map'
        marker = next(actor for actor in actors.get_all_level_actors() if actor.get_actor_label() == NOTE)
        assert SCENE_NOTE in marker.get_editor_property('text')
        world = editor.get_editor_world()
        mode = world.get_world_settings().get_editor_property('default_game_mode')
        mode = mode or unreal.load_class(None, '/Script/GuLiStrike.GuLiCommanderGameMode')
        controller = unreal.get_default_object(mode).get_editor_property('player_controller_class')
        presentation = unreal.get_default_object(controller).get_component_by_class(unreal.GuLiRogueCardPresentation)
        assert presentation is not None
        report['scene'] = {'map': world.get_path_name(), 'actor': marker.get_path_name(), 'label': NOTE,
                           'location': str(marker.get_actor_location()), 'instructions': marker.get_editor_property('text'),
                           'saved_and_reloaded': True, 'controller': controller.get_path_name(),
                           'presentation_component': presentation.get_class().get_path_name()}
        report['success'] = True
    except Exception:
        report['error'] = traceback.format_exc()
    (OUT / 'ue-readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2, default=str)+'\n', encoding='utf-8')
    print(json.dumps({'success': report['success'], 'error': report.get('error')}, ensure_ascii=False))


if __name__ == '__main__':
    main()
