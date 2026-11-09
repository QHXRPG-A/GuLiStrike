"""Import only this card's data and public text, preserving loaded DataTable identities."""
import json
import math
import re
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/GroundExplosiveEnhancement_20261109'
TABLES = ['DT_GuLiStrikeRogueCards_Cards', 'DT_GuLiStrikeRogueCards_Effects',
          'DT_GuLiStrikeRogueCards_GroundWeaponEffects', 'DT_GuLiStrikeVfx_Effects', 'DT_GuLiStrikeGameTexts_Texts']


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def normalized(value):
    if value == 'None': return ''
    if isinstance(value, str) and "'" in value and value.endswith("'"): return value.split("'", 1)[1][:-1]
    return value


def check(expected, actual, label):
    if isinstance(expected, dict) and isinstance(actual, str) and actual.startswith('('):
        actual = {k: float(v) for k, v in re.findall(r'(\w+)=([^,)]+)', actual)}
    if isinstance(expected, str) and expected.startswith('('):
        expected = {k: float(v) for k, v in re.findall(r'(\w+)=([^,)]+)', expected)}
        actual = {k: float(v) for k, v in re.findall(r'(\w+)=([^,)]+)', str(actual))}
    if isinstance(expected, dict):
        for k, v in expected.items(): check(v, actual.get(k), label + '.' + k)
    elif isinstance(expected, float):
        require(isinstance(actual, (int, float)) and math.isclose(expected, actual, rel_tol=1e-6, abs_tol=1e-5), label)
    else:
        require(normalized(actual) == normalized(expected), label + ': ' + repr(actual))


def main():
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(), 'End PIE before import')
    radius = json.loads((OUT/'radius-calibration.json').read_text(encoding='utf8'))['radius_cm']
    unreal.GuLiStrikeRogueCardsGroundWeaponEffectsRow().get_editor_property('explosive_radius_bonuses')
    unreal.GuLiStrikeVfxEffectsRow().get_editor_property('radius_at_unit_scale_cm')
    paths = ['/Game/GuLiStrike/Data/' + n for n in TABLES] + ['/Game/GuLiStrike/Data/ST_GuLiStrikeGameTexts']
    dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(not dirty.intersection(paths), 'Preserve existing unsaved data edits')
    assets = []
    for name in TABLES:
        table = require(unreal.load_asset('/Game/GuLiStrike/Data/' + name), name)
        source = (ROOT/'Data/Json'/f'{name}.json').read_text(encoding='utf-8-sig')
        require(unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, source), 'Import ' + name)
        require(unreal.EditorAssetLibrary.save_loaded_asset(table, False), 'Save ' + name)
        assets.append(table)
    strings = require(unreal.load_asset(paths[-1]), 'StringTable')
    require(unreal.GuLiRogueCardPresentationLibrary.rebuild_game_text_string_table(strings, assets[-1]), 'Rebuild text')
    require(unreal.EditorAssetLibrary.save_loaded_asset(strings, False), 'Save text')
    ok, error = unreal.EditorLoadingAndSavingUtils.reload_packages(
        [a.get_outer() for a in assets] + [strings.get_outer()], unreal.ReloadPackagesInteractionMode.ASSUME_NEGATIVE)
    require(ok and not str(error), 'Saved data reload: ' + str(error))
    result = {}
    for name in TABLES:
        table = unreal.load_asset('/Game/GuLiStrike/Data/' + name)
        rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
        expected = json.loads((ROOT/'Data/Json'/f'{name}.json').read_text(encoding='utf-8-sig'))
        actual = {r['Name']:r for r in rows}
        require(set(actual) == {r['Name'] for r in expected}, 'Row identities ' + name)
        for r in expected: check(r, actual[r['Name']], name + '.' + r['Name'])
        result[name] = rows
    card = next(r for r in result[TABLES[0]] if r['Id'] == '12.01')
    require(card['Type'] == 3 and card['IconId'] == 0 and card['RequiredCardIds'] == ['11.01'] and card['MaxAcquisitions'] == 3, 'Card routing')
    require(unreal.load_asset(normalized(card['FrontMaterial'])), 'Blank face')
    base = next(r for r in result[TABLES[2]] if r['Id'] == 3)
    impact = next(r for r in result[TABLES[3]] if r['Id'] == 64)
    require(base['ExplosiveRadiusCentimeters'] == impact['RadiusAtUnitScaleCm'] == radius, 'Calibrated base radius')
    require(isinstance(unreal.load_asset(normalized(impact['ResourcePath'])), unreal.NiagaraSystem), 'Owned impact')
    bound = {}
    for suffix in ('Title', 'Description'):
        key = 'Card.GroundMech.ExplosiveEnhancement.' + suffix
        text = unreal.TextLibrary.text_from_string_table(unreal.Name(paths[-1] + '.ST_GuLiStrikeGameTexts'), key)
        _, actual_key = unreal.TextLibrary.string_table_id_and_key_from_text(text)
        require(actual_key == key and unreal.TextLibrary.text_is_from_string_table(text), 'StringTable binding')
        require('\n' not in str(text), 'Single line')
        bound[key] = str(text)
    return {'success': True, 'radius_cm': radius, 'saved_reloaded': paths, 'rows': result, 'text': bound}


try: report = main()
except Exception: report = {'success': False, 'error': traceback.format_exc()}
(OUT/'table_import.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': report['success'], 'error': report.get('error')}, ensure_ascii=False))
