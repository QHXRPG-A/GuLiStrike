"""Import WM01 guidance data in an idle editor. Never builds, restarts or starts PIE."""
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/WM01Guidance60'
CONFIG = '/Game/GuLiStrike/Commander/Skills/DA_WM01_MissileQ'
DATA = '/Game/GuLiStrike/Data/'


def main():
    report = {'success': False, 'native_schema_ready': False, 'runtime_validation': 'not_run', 'assets': []}
    try:
        level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        assert not level.is_in_play_in_editor(), 'Preserve the active player session'
        config = unreal.load_asset(CONFIG)
        assert config is not None
        schema_ready = hasattr(config, 'max_projectiles_per_activation') and hasattr(
            unreal.GuLiStrikeSecondaryUnitSkillsSkillsRow(), 'max_projectiles_per_activation')
        report['native_schema_ready'] = schema_ready
        names = ['DT_GuLiStrikeRogueCards_Cards', 'DT_GuLiStrikeGameTexts_Texts']
        if schema_ready:
            names.append('DT_GuLiStrikeSecondaryUnitSkills_Skills')
        owned = {DATA + name for name in names} | {CONFIG, DATA + 'ST_GuLiStrikeGameTexts'}
        dirty = {p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
        assert not owned.intersection(dirty), 'Preserve unsaved changes to target assets: ' + str(owned.intersection(dirty))
        for name in names:
            table = unreal.load_asset(DATA + name)
            assert table is not None, name
            payload = (ROOT / 'data/Json' / (name + '.json')).read_text(encoding='utf-8')
            assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(
                table, payload, table.get_editor_property('row_struct')), name
            assert unreal.EditorAssetLibrary.save_loaded_asset(table, False), name
            actual = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
            expected = json.loads(payload)
            def fields(rows):
                return {r['Name']: {k: r[k] for k in ('MaxAcquisitions', 'BonusCount', 'TextId', 'Content', 'MaxProjectilesPerActivation') if k in r} for r in rows}
            assert fields(actual) == fields(expected), name + ': saved row readback differs'
            report['assets'].append({'path': table.get_path_name(), 'rows': len(actual), 'readback': fields(actual) if 'RogueCards' in name or 'SecondaryUnitSkills' in name else 'matched'})
        texts = unreal.load_asset(DATA + 'DT_GuLiStrikeGameTexts_Texts')
        strings = unreal.load_asset(DATA + 'ST_GuLiStrikeGameTexts')
        assert unreal.GuLiRogueCardPresentationLibrary.rebuild_game_text_string_table(strings, texts)
        assert unreal.EditorAssetLibrary.save_loaded_asset(strings, False)
        text_key = 'UI.CommanderSkills.GuidanceBatch'
        expected_text = next(row['Content'] for row in json.loads((ROOT / 'data/Json/DT_GuLiStrikeGameTexts_Texts.json').read_text(encoding='utf-8')) if row['TextId'] == text_key)
        report['feedback_text'] = unreal.StringTableLibrary.get_table_entry_source_string(strings.get_path_name(), text_key)
        assert report['feedback_text'] == expected_text
        if schema_ready:
            row = next(r for r in json.loads((ROOT / 'data/Json/DT_GuLiStrikeSecondaryUnitSkills_Skills.json').read_text(encoding='utf-8')) if r['Name'] == 'WM01_HomingMissile')
            config.set_editor_property('max_projectiles_per_activation', row['MaxProjectilesPerActivation'])
            assert unreal.EditorAssetLibrary.save_loaded_asset(config, False)
            catalog = unreal.load_asset('/Game/GuLiStrike/Commander/Skills/DA_CommanderSkills_V1')
            report['catalog_issues'] = list(map(str, unreal.GuLiSkillAuthoringLibrary.validate_commander_catalog(catalog)))
            assert not report['catalog_issues']
            report['configuration'] = {'path': config.get_path_name(), 'capacity': config.max_projectiles_per_activation,
                'diameter': config.target_area_diameter_centimeters, 'warning_style': config.ground_warning_style.get_path_name()}
            assert report['configuration']['capacity'] == row['MaxProjectilesPerActivation'] == 60
            assert report['configuration']['diameter'] == row['TargetAreaDiameterCentimeters'] == 1600
            report['success'] = True
        else:
            report['pending'] = 'Native reflection is not loaded. Compile/reopen with permission, then rerun this script to import the skill table and capacity asset.'
    except Exception:
        report['error'] = traceback.format_exc()
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'asset-readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(report, ensure_ascii=False))


main()
