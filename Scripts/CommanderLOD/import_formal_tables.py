"""Import only approved table reference changes through the existing CSV pipeline."""
import contextlib
import io
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
ART = ROOT / 'ArtSource/CommanderLOD_20261005'
switch = json.loads((ART / 'Reports/source_reference_switch.json').read_text(encoding='utf8'))
assert switch['state'] == 'source_exported_verified'
before = json.loads((ART / 'Reports/native_tables_full_before_switch.json').read_text(encoding='utf8'))
tables = {'DT_GuLiStrikeCommander_Soldiers'}
if any(g['name'] == 'BiZhiMao' for g in switch['groups']):
    tables.add('DT_GuLiStrikeBuildings_Buildings')
scope = dict(__name__='commander_lod_import', GULI_TABLE_FILTER=tables)
with contextlib.redirect_stdout(io.StringIO()):
    exec(compile((ROOT / 'Scripts/import_data_to_engine.py').read_text(encoding='utf8'), 'import_data_to_engine.py', 'exec'), scope)
pipeline = json.loads((ROOT / 'Data/tmp_import_report.json').read_text(encoding='utf8'))
assert not pipeline['errors'], pipeline
assert len(pipeline['tables']) == len(tables) and all(t.get('imported') for t in pipeline['tables']), pipeline
changed = {g['name']: g for g in switch['groups']}
dt = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers')
actual = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt))
old = {r['Name']: r for r in before['DT_GuLiStrikeCommander_Soldiers']}
fields = ('ModelAsset', 'PresentationClass', 'VATDefinition')
for row in actual:
    original = old[row['Name']]
    assert {k: v for k, v in row.items() if k not in fields} == {k: v for k, v in original.items() if k not in fields}, row['Name']
    if row['Name'] in changed:
        for field in fields:
            expected = changed[row['Name']]['after'][field]
            assert row[field] == expected or (not expected and row[field] in ('None', '')), (row['Name'], field, row[field], expected)
    else:
        assert all(row[f] == original[f] for f in fields), row['Name']
building_rows = None
if 'DT_GuLiStrikeBuildings_Buildings' in tables:
    building_dt = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeBuildings_Buildings')
    building_rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(building_dt))
    old_buildings = {r['Name']:r for r in before['DT_GuLiStrikeBuildings_Buildings']}
    for row in building_rows:
        original = old_buildings[row['Name']]
        if row['Id'] == 8:
            assert row['Mesh'] == switch['construction']['after']
            assert {k:v for k,v in row.items() if k != 'Mesh'} == {k:v for k,v in original.items() if k != 'Mesh'}
        else:
            assert row == original
switch.update(native_imported=True, state='native_references_verified', native_rows=actual,
    native_building_rows=building_rows, approval_B='approved', original_groups_retained=True, pie='not_run', network='not_run', fps='not_run')
(ART / 'Reports/source_reference_switch.json').write_text(json.dumps(switch, ensure_ascii=False, indent=2), encoding='utf8')
(ART / 'Reports/formal_table_import.json').write_text(json.dumps(dict(success=True, pipeline=pipeline, rows=actual, groups=list(changed)), ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True, groups=list(changed), native_table=dt.get_path_name(), gameplay_values_unchanged=True, pending_groups=switch['pending_groups'])))
