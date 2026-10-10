"""Publish only after the explicit CPU input/F+1/identity review has passed."""
import json,contextlib,io
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()));OUT=ROOT/'outputs/performance/20261010-muzzle-batch'
assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
assert json.loads((OUT/'runtime-contract-validation.json').read_text())['success']
table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects')
before=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
rows=[]
for suffix in ['', '_Reduced','_Minimal']:
 path='/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunMuzzle_Batch'+suffix
 assert unreal.NiagaraService.set_parameter(path,'User.GuLiMuzzleInputVersion','1')
 result=unreal.NiagaraService.compile_with_results(path);assert result.success and result.error_count==0
 assert unreal.NiagaraService.save_system(path)
 rows.append({'path':path,'compile_errors':result.error_count,'input_version':float(unreal.NiagaraService.get_parameter(path,'User.GuLiMuzzleInputVersion').current_value),'saved':True})
catalog=unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_CommanderCombatEffects')
old_impact=catalog.get_editor_property('ImpactChannel').get_path_name()
channel=unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/NDC_CommanderMuzzles')
catalog.set_editor_property('MuzzleChannel',channel);assert unreal.EditorAssetLibrary.save_loaded_asset(catalog,False)
scope={'__name__':'__main__','GULI_TABLE_FILTER':{'DT_GuLiStrikeVfx_Effects'},'GULI_IMPORT_REPORT':str(OUT/'muzzle-import-report.json'),'GULI_IMPORT_PROGRESS':str(OUT/'muzzle-import-progress.log')}
with contextlib.redirect_stdout(io.StringIO()):
 exec(compile((ROOT/'Scripts/import_data_to_engine.py').read_text(encoding='utf-8'),'muzzle_effects_import','exec'),scope)
imported=json.loads((OUT/'muzzle-import-report.json').read_text());assert not imported['errors'] and len(imported['tables'])==1 and imported['tables'][0]['imported']
source=json.loads((ROOT/'Data/Json/DT_GuLiStrikeVfx_Effects.json').read_text());assert scope['vfx_table_matches_source'](table,source)
after=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table));old={r['Id']:r for r in before}
for r in after:
 for key,value in r.items():
  if r['Id']==52 and key in ['BatchResourcePath','ReducedBatchResourcePath','MinimalBatchResourcePath']:continue
  assert value==old[r['Id']][key],(r['Id'],key)
assert catalog.get_editor_property('ImpactChannel').get_path_name()==old_impact
(OUT/'muzzle-certification-readback.json').write_text(json.dumps({'success':True,'systems':rows,'muzzle_channel':catalog.get_editor_property('MuzzleChannel').get_path_name(),'impact_channel_unchanged':old_impact,'table_source_matches':True,'scale_and_other_rows_preserved':True,'input_checks':'runtime-contract-validation.json','visual_acceptance':'pending_user_PIE'},ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'certified':3,'id':52,'saved_channel':True,'default_mode':unreal.SystemLibrary.get_console_variable_int_value('gs.Muzzles.BatchMode')}))
