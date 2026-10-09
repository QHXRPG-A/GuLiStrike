"""Import the three changed catalogue tables through the standard importer."""
import json
import math
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'ArtSource/SweeperTeamColor_v1_20261008'
scope = {'__name__': 'sweeper_catalog_import',
         'GULI_TABLE_FILTER': ['DT_GuLiStrikeModels_' + s for s in ['Models', 'MaterialParameters', 'ColorRegions']],
         'GULI_IMPORT_REPORT': str(OUT / 'datatable-import.json'),
         'GULI_IMPORT_PROGRESS': str(OUT / 'datatable-import-progress.log')}
exec(compile((ROOT / 'Scripts/import_data_to_engine.py').read_text(encoding='utf8'), 'scoped_catalog_import', 'exec'), scope)
report = json.loads((OUT / 'datatable-import.json').read_text(encoding='utf8'))
if report['errors']:
    raise RuntimeError('Scoped DataTable import failed: ' + str(report['errors']))
readback, errors = {}, []
for name in scope['GULI_TABLE_FILTER']:
    dt = unreal.load_asset('/Game/GuLiStrike/Data/' + name)
    actual = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt))
    expected = json.loads((ROOT / 'Data/Json' / (name + '.json')).read_text(encoding='utf8'))
    # UE exports empty soft references as None and floats with six significant
    # digits. Normalize those serialization forms, not the stored parameters.
    for r in actual:
        for field in ['VATDefinition', 'CandidateResourcePath', 'CandidateVATDefinition']:
            if r.get(field)=='None':
                r[field]=''
    def index(rows):
        return {int(r['Id']): {k: v for k, v in r.items() if k != 'Name'} for r in rows}
    a, b = index(actual), index(expected)
    def equal(x, y):
        if isinstance(x, (float, int)) and isinstance(y, (float, int)):
            return math.isclose(x,y,rel_tol=5e-6,abs_tol=1e-5)
        if isinstance(x, dict) and isinstance(y, dict):
            return set(x)==set(y) and all(equal(x[k], y[k]) for k in x)
        return x==y
    if not equal(a,b):
        errors.append(name + ' saved DataTable differs from source')
    readback[name] = {'asset': dt.get_path_name(), 'rows': len(actual), 'source_parity': equal(a,b),
                      'comparison': 'empty soft references normalized; six-significant-digit UE JSON float export tolerance'}
reloads = 0
for sub in unreal.ObjectIterator(unreal.GuLiModelRegistrySubsystem):
    sub.reload_catalog()
    reloads += 1
result = {'success': not errors, 'tables': readback, 'errors': errors,
          'registry_reloads': reloads, 'native_schema_changed': False, 'native_compile_required': False}
(OUT / 'datatable-saved-readback.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(result))
