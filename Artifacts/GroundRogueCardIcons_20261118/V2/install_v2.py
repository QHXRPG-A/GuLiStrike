"""Update the five icon textures and their version notes, keeping card rows intact."""
import hashlib
import importlib
import json
import sys
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT/'Artifacts/GroundRogueCardIcons_20261118/V2'
IDS = {9, 10, 11, 12, 13}
ICON_TABLE = '/Game/GuLiStrike/Data/DT_GuLiStrikeIcons_Icons'
CARD_TABLE = '/Game/GuLiStrike/Data/DT_GuLiStrikeRogueCards_Cards'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rows(path):
    return json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(unreal.load_asset(path)))


def main():
    assert Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve() == ROOT
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    dirty = {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assert ICON_TABLE not in dirty
    registration = json.loads((OUT/'registration.json').read_text(encoding='utf-8'))
    for relative, digest in registration['files_after'].items():
        assert sha(ROOT/relative) == digest, relative
    export = json.loads((OUT/'export-verification.json').read_text(encoding='utf-8'))
    assert sha(ROOT/'data/Json/DT_GuLiStrikeIcons_Icons.json') == export['icons_json_sha256']
    before_icons, before_cards = rows(ICON_TABLE), rows(CARD_TABLE)
    sources = {r['Id']: r for r in json.loads((ROOT/'data/Json/DT_GuLiStrikeIcons_Icons.json').read_text(encoding='utf-8'))}
    expected = []
    for row in before_icons:
        new = dict(row)
        if row['Id'] in IDS:
            wanted = sources[row['Id']]
            assert all(wanted[k] == row[k] for k in row if k != 'Note')
            new['Note'] = wanted['Note']
        expected.append(new)
    assert {r['Id'] for r in expected} == set(sources)
    sys.path.insert(0, str(ROOT/'Scripts/GroundMech'))
    import icon_assets
    importlib.reload(icon_assets)
    textures = icon_assets.import_catalog(ROOT, sorted(IDS))
    table = unreal.load_asset(ICON_TABLE)
    assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(expected, ensure_ascii=False), table.get_editor_property('row_struct'))
    assert unreal.EditorAssetLibrary.save_loaded_asset(table, False)
    after_icons, after_cards = rows(ICON_TABLE), rows(CARD_TABLE)
    assert {r['Id']: r for r in after_icons} == {r['Id']: r for r in expected}
    assert after_cards == before_cards
    for texture in textures:
        asset = unreal.load_asset(texture['path'])
        filenames = asset.get_editor_property('asset_import_data').extract_filenames()
        assert len(filenames) == 1
        assert Path(filenames[0]).resolve() == (ROOT/texture['source']).resolve()
        assert sha(Path(filenames[0])) == texture['sha256']
    dirty_after = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    dirty_maps = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    assert not dirty_after and not dirty_maps
    return dict(success=True, textures=textures, icons=after_icons, cards_unchanged=True,
                ground_card_icons={r['Id']: r['IconId'] for r in after_cards if r['Type'] == 3},
                saved_readback=True, stable_ids_and_paths=True, dirty_content=dirty_after,
                dirty_maps=dirty_maps, assistant_visual_test=False, user_visual_acceptance='pending',
                pie_started=False)


def execute():
    try:
        result = main()
    except Exception:
        result = dict(success=False, error=traceback.format_exc())
    path = OUT/'install-report.json'
    path.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(dict(success=result['success'], error=result.get('error'), report=str(path))))


if __name__ == '__main__':
    execute()
