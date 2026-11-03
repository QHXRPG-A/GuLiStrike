"""Install the approved toon icons and two changed DataTables in the live editor."""
import json
import importlib
import sys
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/GroundToonIcons_20261103'
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT / 'Scripts/GroundMech'))
import_catalog = importlib.reload(importlib.import_module('icon_assets')).import_catalog

report = {'success': False, 'visual_review': 'not_performed', 'pie_started': False}
try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    changed_tables = {'DT_GuLiStrikeIcons_Icons', 'DT_GuLiStrikeMech_Skills'}
    dirty = {package.get_name() for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assert not dirty.intersection('/Game/GuLiStrike/Data/' + name for name in changed_tables)
    report['textures'] = import_catalog(ROOT)
    # The mobility fingerprint includes the complete skills table, including
    # IconId. Keep its normal post-import synchronization and manifest export.
    # Icon-only changes do not require rebuilding firing VFX.
    scope = {'__name__': 'toon_icon_table_import',
             'GULI_TABLE_FILTER': changed_tables,
             'GULI_SKIP_GROUND_FIRE_POST_IMPORT': True,
             'GULI_IMPORT_REPORT': str(OUT / 'data_import.json'),
             'GULI_IMPORT_PROGRESS': str(OUT / 'data_import.log')}
    script = ROOT / 'Scripts/import_data_to_engine.py'
    exec(compile(script.read_text(encoding='utf-8'), str(script), 'exec'), scope)
    assert not scope['report']['errors'], scope['report']['errors']
    report['tables'] = scope['report']['tables']
    report['ground_mobility'] = scope['report']['ground_mobility']
    assert report['ground_mobility']['success'], report['ground_mobility']
    assert {item['population'] for item in report['ground_mobility']['manifests']} == {200, 600}

    def read_table(name):
        table = unreal.load_asset('/Game/GuLiStrike/Data/' + name)
        return json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))

    icons = {row['Id']: row for row in read_table('DT_GuLiStrikeIcons_Icons')}
    assert 7 not in icons and set(icons) == {1, 2, 3, 4, 5, 6, 8}
    consumers = {}
    for suffix in ('Mech_Skills', 'RogueCards_Cards', 'Buffs_Buffs', 'GroundHUD_Elements'):
        rows = read_table('DT_GuLiStrike' + suffix)
        consumers[suffix] = [{'name': row['Name'], 'icon_id': row['IconId']}
                             for row in rows if row.get('IconId', 0)]
        assert all(row['icon_id'] in icons for row in consumers[suffix]), suffix
    assert next(row['icon_id'] for row in consumers['Mech_Skills'] if row['name'] == 'RocketJump') == 1
    assert next(row['icon_id'] for row in consumers['GroundHUD_Elements'] if row['name'] == 'IconJetCapacity') == 1
    report['consumers'] = consumers
    report['shared_jump_capacity_icon_id'] = 1
    catalog = json.loads((ROOT / 'ArtSource/UI/GroundMech/Icons/catalog.json').read_text(encoding='utf-8'))
    report['retired_assets'] = []
    for path in catalog['retired_assets']:
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            report['retired_assets'].append({'path': path, 'already_absent': True})
            continue
        referencers = list(map(str, unreal.EditorAssetLibrary.find_package_referencers_for_asset(path, True)))
        assert not referencers, {'retired_icon': path, 'referencers': referencers}
        assert path not in dirty, 'Preserve unsaved legacy icon'
        assert unreal.EditorAssetLibrary.delete_asset(path)
        assert not unreal.EditorAssetLibrary.does_asset_exist(path)
        report['retired_assets'].append({'path': path, 'deleted': True, 'referencers': referencers})
    report['success'] = True
except Exception:
    report['error'] = traceback.format_exc()
finally:
    (OUT / 'asset_import.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps({'success': report['success'], 'report': str(OUT / 'asset_import.json'), 'error': report.get('error', '')}))
