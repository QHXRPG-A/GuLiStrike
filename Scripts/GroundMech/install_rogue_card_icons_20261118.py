"""Import five new textures and merge only the requested catalog/card fields."""
import copy
import hashlib
import importlib
import json
import sys
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
OUT = ROOT / 'Artifacts/GroundRogueCardIcons_20261118'
IDS = {9, 10, 11, 12, 13}
FIELDS = {'09.01': ['IconId'], '10.01': ['IconId'], '11.01': ['IconId'],
          '12.01': ['IconId', 'FrontMaterial']}
ICON_TABLE = '/Game/GuLiStrike/Data/DT_GuLiStrikeIcons_Icons'
CARD_TABLE = '/Game/GuLiStrike/Data/DT_GuLiStrikeRogueCards_Cards'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rows(path):
    asset = unreal.load_asset(path)
    assert asset, path
    return json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(asset))


def save_rows(path, expected):
    asset = unreal.load_asset(path)
    assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(
        asset, json.dumps(expected, ensure_ascii=False), asset.get_editor_property('row_struct'))
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
    actual = rows(path)
    assert {r['Id']: r for r in actual} == {r['Id']: r for r in expected}, path
    return actual


def main():
    assert ROOT == Path('D:/UE5.7/test1'), ROOT
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assert not dirty.intersection({ICON_TABLE, CARD_TABLE}), 'Preserve unsaved data table edits'
    registration = json.loads((OUT/'registration.json').read_text(encoding='utf-8'))
    for relative, digest in registration['source_files_after'].items():
        assert sha(ROOT/relative) == digest, 'Source changed after registration: ' + relative
    exported_icons = json.loads((ROOT/'data/Json/DT_GuLiStrikeIcons_Icons.json').read_text(encoding='utf-8'))
    source_icons = {r['Id']: r for r in exported_icons}
    source_cards = {r['Id']: r for r in json.loads((ROOT/'data/Json/DT_GuLiStrikeRogueCards_Cards.json').read_text(encoding='utf-8'))}
    assert IDS <= set(source_icons)
    assert source_cards['12.01']['IconId'] == 13
    assert source_cards['12.01']['FrontMaterial'] == '/Game/GuLiStrike/Cards/GroundMech/ExplosiveEnhancement/Materials/MI_GroundMech_ExplosiveEnhancement_Artwork.MI_GroundMech_ExplosiveEnhancement_Artwork'
    assert isinstance(unreal.load_asset(source_cards['12.01']['FrontMaterial']), unreal.MaterialInstanceConstant)
    before_icons, before_cards = rows(ICON_TABLE), rows(CARD_TABLE)
    expected_icons = copy.deepcopy(before_icons)
    old_icon_ids = {row['Id'] for row in expected_icons}
    for ident in sorted(IDS):
        if ident in old_icon_ids:
            assert next(r for r in expected_icons if r['Id'] == ident) == source_icons[ident]
        else:
            expected_icons.append(source_icons[ident])
    expected_cards = copy.deepcopy(before_cards)
    changes = []
    for row in expected_cards:
        if row['Id'] in FIELDS:
            wanted = source_cards[row['Id']]
            fields = FIELDS[row['Id']]
            changes.append(dict(card_id=row['Id'], before={k: row[k] for k in fields}, after={k: wanted[k] for k in fields}))
            row.update({k: wanted[k] for k in fields})
    assert len(changes) == 4
    assert not any('NoSurvivors' in row['Name'] for row in expected_cards)
    sys.path.insert(0, str(ROOT/'Scripts/GroundMech'))
    import icon_assets
    importlib.reload(icon_assets)
    textures = icon_assets.import_catalog(ROOT, sorted(IDS))
    assert len(textures) == 5
    actual_icons = save_rows(ICON_TABLE, expected_icons)
    actual_cards = save_rows(CARD_TABLE, expected_cards)
    assert {r['Id']: r for r in actual_icons if r['Id'] not in IDS} == {r['Id']: r for r in before_icons if r['Id'] not in IDS}
    assets = {r['Id']: r for r in actual_icons}
    for card in actual_cards:
        if card['Id'] in FIELDS:
            assert isinstance(unreal.load_asset(assets[card['IconId']]['ResourcePath']), unreal.Texture2D)
    return dict(success=True, textures=textures, changes=changes,
                icons=actual_icons, cards=actual_cards, other_fields_preserved=True,
                saved_readback=True, no_survivors_icon_registered_unbound=True,
                assistant_visual_test=False, pie_started=False,
                dirty_content=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()])


def execute():
    try:
        report = main()
    except Exception:
        report = dict(success=False, error=traceback.format_exc())
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT/'install-report.json'
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(dict(success=report['success'], report=str(path), error=report.get('error'))))
    return report


if __name__ == '__main__':
    execute()
