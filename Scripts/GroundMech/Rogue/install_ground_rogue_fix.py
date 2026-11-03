"""In-place import after loading Buff schema v2. No PIE, previews or broad saves."""
import json
import math
import re
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/GroundRevision_20261103'
TABLES = ['DT_GuLiStrikeBuffs_Buffs', 'DT_GuLiStrikeBuffs_Burning', 'DT_GuLiStrikeBuffs_BurningBurst',
          'DT_GuLiStrikeVfx_Effects', 'DT_GuLiStrikeGameTexts_Texts',
          'DT_GuLiStrikeRogueCards_GroundRicochet', 'DT_GuLiStrikeRogueCards_Cards']


def equal(actual, expected):
    if isinstance(expected, float):
        return isinstance(actual, (float, int)) and math.isclose(actual, expected, rel_tol=1e-5, abs_tol=1e-6)
    if isinstance(expected, dict):
        if isinstance(actual, str):
            actual = {k: float(v) for k, v in re.findall(r'([A-Za-z]+)=([-+\d.eE]+)', actual)}
        return all(k in actual and equal(actual[k], v) for k, v in expected.items())
    if isinstance(expected, list):
        return len(actual) == len(expected) and all(equal(a, b) for a, b in zip(actual, expected))
    if expected == '' and actual == 'None':
        return True
    return actual == expected


def main():
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(), 'Preserve active PIE'
    unreal.GuLiStrikeBuffsBurningBurstRow().get_editor_property('radius_centimeters')
    assert unreal.load_asset('/Game/GuLiStrike/FX/GroundMech/Burning/NS_GroundBurningBurst'), 'Create burst asset first'
    dirty = {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assert not any('/Game/GuLiStrike/Data/' + n in dirty for n in TABLES + ['ST_GuLiStrikeGameTexts']), 'Preserve unsaved tables'
    result = []
    for name in TABLES:
        path = '/Game/GuLiStrike/Data/' + name
        table = unreal.load_asset(path)
        assert table, path
        raw = (ROOT / 'data/Json' / (name + '.json')).read_text(encoding='utf-8-sig')
        problems = unreal.GuLiDataTableImportLibrary.update_from_json(table, raw)
        assert not problems, list(problems)
        assert unreal.EditorAssetLibrary.save_loaded_asset(table, False), path
        saved = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
        expected = json.loads(raw)
        actual_by_name = {r['Name']: r for r in saved}
        assert len(actual_by_name) == len(expected) and all(equal(actual_by_name[r['Name']], r) for r in expected), name
        result.append({'asset': path, 'rows': len(saved), 'readback_matches': True})
    strings = unreal.load_asset('/Game/GuLiStrike/Data/ST_GuLiStrikeGameTexts')
    texts = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeGameTexts_Texts')
    assert unreal.GuLiRogueCardPresentationLibrary.rebuild_game_text_string_table(strings, texts)
    assert unreal.EditorAssetLibrary.save_loaded_asset(strings, False)
    text = unreal.TextLibrary.text_from_string_table(unreal.Name(strings.get_path_name()), 'Card.GroundMech.BurningBurst.Description')
    assert str(text) == '<Unit>地面机甲</> 灼烧每隔一段时间发生<Gain>爆燃</>', 'Single-line burst wording'
    return {'success': True, 'tables': result, 'description': str(text), 'buff_version': 2, 'radius_cm': 500}


try:
    report = main()
except Exception:
    report = {'success': False, 'error': traceback.format_exc()}
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'table_install.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report, ensure_ascii=False))
