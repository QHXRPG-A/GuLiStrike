"""Replace Icon 11 only; preserve both data tables and the other four textures."""
import hashlib
import importlib
import json
import sys
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT/'Artifacts/GroundRogueCardIcons_20261118/PackageAxisRepair'
BASE = ROOT/'ArtSource/UI/GroundMech/RogueCardIcons_20261118_v1'
TABLES = ['/Game/GuLiStrike/Data/DT_GuLiStrikeIcons_Icons',
          '/Game/GuLiStrike/Data/DT_GuLiStrikeRogueCards_Cards']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rows(path):
    return json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(unreal.load_asset(path)))


def inspect(entry):
    texture = unreal.load_asset(entry['resource_path'])
    assert isinstance(texture, unreal.Texture2D)
    return dict(id=entry['id'], path=texture.get_path_name(),
                filenames=list(texture.get_editor_property('asset_import_data').extract_filenames()),
                sha256=unreal.EditorAssetLibrary.get_metadata_tag(texture, 'GroundIconSourceSHA256'),
                version=unreal.EditorAssetLibrary.get_metadata_tag(texture, 'GroundIconVersion'),
                size=[texture.blueprint_get_size_x(), texture.blueprint_get_size_y()],
                compression=str(texture.get_editor_property('compression_settings')),
                lod_group=str(texture.get_editor_property('lod_group')),
                alpha_preserved=not texture.get_editor_property('compression_no_alpha'))


def main():
    assert Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve() == ROOT
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    manifest = json.loads((BASE/'manifest.json').read_text(encoding='utf-8'))
    baseline = json.loads((OUT/'source-before.json').read_text(encoding='utf-8'))
    tables_before = {path: rows(path) for path in TABLES}
    textures_before = {entry['id']: inspect(entry) for entry in manifest['icons']}
    for key, value in textures_before.items():
        assert value['sha256'] == baseline['icon_hashes'][str(key)], key
    (OUT/'ue-before.json').write_text(json.dumps(dict(tables=tables_before, textures=textures_before),
        ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    sys.path.insert(0, str(ROOT/'Scripts/GroundMech'))
    import icon_assets
    importlib.reload(icon_assets)
    imported = icon_assets.import_catalog(ROOT, [11])
    textures_after = {entry['id']: inspect(entry) for entry in manifest['icons']}
    tables_after = {path: rows(path) for path in TABLES}
    assert tables_before == tables_after
    for entry in manifest['icons']:
        texture = textures_after[entry['id']]
        assert texture['sha256'] == entry['sha256']
        assert len(texture['filenames']) == 1
        source = Path(texture['filenames'][0]).resolve()
        assert source == (ROOT/entry['source_png']).resolve()
        assert sha(source) == entry['sha256']
        assert texture['size'] == [256, 256] and texture['alpha_preserved']
        if entry['id'] != 11:
            assert texture == textures_before[entry['id']]
    for relative, digest in baseline['files'].items():
        if relative != 'ArtSource/UI/GroundMech/Icons/catalog.json':
            assert sha(ROOT/relative) == digest, relative
    dirty_content = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    target = next(entry for entry in manifest['icons'] if entry['id'] == 11)
    assert target['resource_path'].split('.')[0] not in dirty_content
    manifest['ue_import']['status'] = 'revision_imported_saved'
    (BASE/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    return dict(success=True, imported=imported, textures=textures_after,
                icon_table_unchanged=True, cards_table_unchanged=True,
                ground_card_icons={r['Id']: r['IconId'] for r in tables_after[TABLES[1]] if r['Type'] == 3},
                other_four_textures_unchanged=True, saved_readback=True,
                dirty_content=dirty_content,
                dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
                assistant_visual_test=False, visual_acceptance='pending_user', pie_started=False)


try:
    result = main()
except Exception:
    result = dict(success=False, error=traceback.format_exc())
report_path = OUT/'install-report.json'
report_path.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=result['success'], error=result.get('error'), report=str(report_path))))
