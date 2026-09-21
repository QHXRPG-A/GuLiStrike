"""Repeatable feature-scoped import. Execute with Scripts/ue_exec.py after rebuilding native changes."""
import contextlib
import io
import json
import sys
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/Vfx'))
from configure_registry_bindings import apply

scope={'__name__':'__main__','GULI_TABLE_FILTER':{'DT_GuLiStrikeVfx_Effects','DT_GuLiStrikeMech_Skills','DT_GuLiStrikeSpellFields_Fields'}}
with contextlib.redirect_stdout(io.StringIO()):
    exec(compile((ROOT/'Scripts/import_data_to_engine.py').read_text(encoding='utf-8'),'import_data_to_engine.py','exec'),scope)
result=json.loads((ROOT/'Data/tmp_import_report.json').read_text(encoding='utf-8'))
if not all(t.get('imported') for t in result['tables']):
    raise RuntimeError('VFX table import/readback failed: '+str(result['tables']))
result['bindings']=apply()
output=ROOT/'outputs/vfx-registry-20260921/import-report.json'
output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'tables':[{'name':t['asset'],'unchanged':t.get('unchanged',False)} for t in result['tables']],
                  'saved':result['bindings']['saved'],'blueprints':result['bindings']['blueprints']}))
