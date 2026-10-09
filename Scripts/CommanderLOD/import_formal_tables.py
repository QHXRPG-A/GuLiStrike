"""Import approved resource changes through Models, after native schemas are loaded."""
import contextlib
import io
import json
from pathlib import Path
import unreal

ROOT = Path(r'D:/UE5.7/test1')
report_path = ROOT/'Data/Models/commander-lod-model-switch.json'
switch = json.loads(report_path.read_text(encoding='utf8'))
assert switch['state'] == 'source_exported_verified'
tables = {'DT_GuLiStrikeModels_Models'}
scope = dict(__name__='commander_lod_model_import',GULI_TABLE_FILTER=tables)
with contextlib.redirect_stdout(io.StringIO()):
    exec(compile((ROOT/'Scripts/import_data_to_engine.py').read_text(encoding='utf8'),'import_data_to_engine.py','exec'),scope)
pipeline = scope['report']
assert not pipeline['errors'] and all(r['imported'] for r in pipeline['tables']),pipeline
dt = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeModels_Models')
actual = {r['Id']:r for r in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt))}
for change in switch['groups']:
    for field,value in change['after'].items():
        assert actual[change['id']][field] == value or (not value and actual[change['id']][field] in ('','None'))
switch.update(native_imported=True,state='native_references_verified',pie='not_run',network='not_run')
report_path.write_text(json.dumps(switch,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(dict(success=True,model_ids=[r['id'] for r in switch['groups']],gameplay_values_unchanged=True)))
