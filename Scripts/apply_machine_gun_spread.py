"""Import only the commander weapon UnitSkills table after the native spread build."""
import json
from pathlib import Path
import traceback
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'ArtSource/MachineGunSpread_20260930'
TABLE = 'DT_GuLiStrikeCommander_UnitSkills'
ASSET = '/Game/GuLiStrike/Data/' + TABLE
EXPECTED = {'SoldierA_Strafe': 10.0, 'WM01_Strafe': 10.0, 'SoldierA_Test': 0.0,
            'WM01_Test': 0.0, 'WM01_MissileLauncher': 0.0}
result = {'success': False, 'pie_started': False, 'images_read': False,
          'player_acceptance': 'pending', 'table': ASSET}
try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    row = unreal.GuLiStrikeCommanderUnitSkillsRow()
    assert row.get_editor_property('projectile_spread_angle_degrees') == 0.0
    result['new_native_property_loaded'] = True
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    assert ASSET not in [p.get_name() for p in dirty], 'Target table has unsaved work'
    source = json.loads((ROOT / 'Data/Json' / (TABLE + '.json')).read_text(encoding='utf-8'))
    assert {r['Name']: r['ProjectileSpreadAngleDegrees'] for r in source} == EXPECTED
    scope = {'__name__': '__main__', 'GULI_TABLE_FILTER': {TABLE}}
    script = ROOT / 'Scripts/import_data_to_engine.py'
    exec(compile(script.read_text(encoding='utf-8'), str(script), 'exec'), scope)
    imported = scope['report']
    result['import'] = imported
    assert not imported['errors']
    assert len(imported['tables']) == 1 and imported['tables'][0]['asset'] == TABLE
    assert imported['tables'][0]['imported'] and all(imported['tables'][0]['row_checks'].values())
    table = unreal.load_asset(ASSET)
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    assert {r['Name']: r['ProjectileSpreadAngleDegrees'] for r in rows} == EXPECTED
    assert scope['vfx_table_matches_source'](table, source), 'Full skill row readback differs from source'
    result['rows'] = rows
    result['row_struct'] = table.get_editor_property('row_struct').get_path_name()
    assert result['row_struct'] == '/Script/GuLiStrike.GuLiStrikeCommanderUnitSkillsRow'
    assert unreal.EditorAssetLibrary.save_loaded_asset(table, False)
    result['saved'] = True
    result['success'] = True
except Exception:
    result['error'] = traceback.format_exc()
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'asset-application.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({k: result.get(k) for k in ('success', 'new_native_property_loaded', 'saved', 'error')}, ensure_ascii=False))
