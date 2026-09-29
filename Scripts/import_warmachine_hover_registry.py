"""Feature-scoped invocation of the supported Excel -> JSON -> CSVImportFactory pipeline."""
import json,traceback,unreal
from pathlib import Path
ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/WarMachineHover_20260929'
r={}
try:
    source=(ROOT/'Scripts/import_data_to_engine.py').read_text(encoding='utf-8')
    ns={};exec(source.split('report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}')[0],ns)
    ns['PROGRESS']=str(OUT/'registry-import.log')
    manifest=json.loads((ROOT/'data/Json/manifest.json').read_text(encoding='utf-8'))
    name='DT_GuLiStrikeVfx_Effects';r['import']=ns['import_table'](name,manifest['tables'][name])
    table=unreal.load_asset('/Game/GuLiStrike/Data/'+name)
    r['row_count']=unreal.DataTableFunctionLibrary.get_data_table_row_names(table).__len__()
    r['registered']='WarMachineHover' in [str(x) for x in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)]
except:r['error']=traceback.format_exc()
(OUT/'registry-import.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(r))
