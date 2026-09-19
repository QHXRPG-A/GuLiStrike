"""Import only Soldiers after a normal native rebuild; retain asset and row identities."""
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
out = root / 'TestResults/UnitSpacing'
out.mkdir(parents=True, exist_ok=True)
namespace = {'__name__': 'unit_footprint_import'}
source = (root / 'Scripts/import_data_to_engine.py').read_text(encoding='utf-8')
definitions = source.split('report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}', 1)[0]
exec(compile(definitions, 'import_data_to_engine.py', 'exec'), namespace)
namespace['PROGRESS'] = str(out / 'import-progress.log')
manifest = json.loads((root / 'data/Json/manifest.json').read_text(encoding='utf-8'))
name = 'DT_GuLiStrikeCommander_Soldiers'
report = {'table': namespace['import_table'](name, manifest['tables'][name])}
table = unreal.load_asset('/Game/GuLiStrike/Data/' + name)
report['rows'] = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
report['imported'] = bool(report['table'].get('imported'))
(out / 'soldiers-import.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report, ensure_ascii=False))
if not report['imported']:
    raise RuntimeError('Soldiers import failed; see TestResults/UnitSpacing/soldiers-import.json')
