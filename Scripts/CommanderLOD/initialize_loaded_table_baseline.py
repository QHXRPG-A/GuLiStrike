"""Load unchanged source values into the freshly rebuilt native row schema."""
import contextlib
import io
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
ART = ROOT / 'ArtSource/CommanderLOD_20261005'
before = json.loads((ART / 'Reports/source_tables_before_switch.json').read_text(encoding='utf8'))
for name, rows in before.items():
    assert json.loads((ROOT / ('Data/Json/' + name + '.json')).read_text(encoding='utf8')) == rows, name
scope = dict(__name__='commander_lod_baseline_import', GULI_TABLE_FILTER=set(before))
with contextlib.redirect_stdout(io.StringIO()):
    exec(compile((ROOT / 'Scripts/import_data_to_engine.py').read_text(encoding='utf8'), 'import_data_to_engine.py', 'exec'), scope)
pipeline = json.loads((ROOT / 'Data/tmp_import_report.json').read_text(encoding='utf8'))
assert not pipeline['errors'] and all(t['imported'] for t in pipeline['tables']), pipeline
assert len(pipeline['tables']) == len(before)
baseline = {}
for name in before:
    dt = unreal.load_asset('/Game/GuLiStrike/Data/' + name)
    baseline[name] = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt))
(ART / 'Reports/native_tables_full_before_switch.json').write_text(json.dumps(baseline, ensure_ascii=False, indent=2), encoding='utf8')
(ART / 'Reports/native_baseline_import.json').write_text(json.dumps(pipeline, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True, tables=list(before), loaded_new_fields=True, source_values_unchanged=True)))
