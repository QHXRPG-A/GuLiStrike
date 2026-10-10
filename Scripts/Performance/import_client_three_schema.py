"""Import only the new optional Effects fields, keeping every production resource/scale unchanged."""
import contextlib,io,json
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()); OUT=ROOT/'outputs/performance/20261009-client-three-optimizations'
assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects')
before=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
scope={'__name__':'__main__','GULI_TABLE_FILTER':{'DT_GuLiStrikeVfx_Effects'},
    'GULI_IMPORT_REPORT':str(OUT/'schema-import-report.json'),'GULI_IMPORT_PROGRESS':str(OUT/'schema-import-progress.log')}
with contextlib.redirect_stdout(io.StringIO()):
    exec(compile((ROOT/'Scripts/import_data_to_engine.py').read_text(encoding='utf-8'),'schema_import','exec'),scope)
report=json.loads((OUT/'schema-import-report.json').read_text(encoding='utf-8'))
assert not report['errors'],report
after=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
old={r['Id']:r for r in before}
assert len(after)==len(before)==52
for row in after:
    assert row['ResourcePath']==old[row['Id']]['ResourcePath'],row
    assert row['Scale']==old[row['Id']]['Scale'],row
    for key in ['ReducedResourcePath','MinimalResourcePath','BatchResourcePath','ReducedBatchResourcePath','MinimalBatchResourcePath']:
        assert key in row,row
assert unreal.EditorAssetLibrary.save_loaded_asset(table,False)
receipt={'success':True,'rows':len(after),'production_resources_and_scale_preserved':True,'new_optional_fields':5}
(OUT/'schema-import-readback.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(receipt))
