"""Read saved formal references and native schema; never plays or renders the scene."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'Artifacts/BurningRogue_20261029'
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
report={'PIE':False,'assistant_visual_test':False,'tables':{},'vfx':{},'model_meshes':[]}
for suffix in ['RogueCards_Cards','RogueCards_Effects','Buffs_Buffs','Buffs_Burning','Buffs_BurningBurst','Vfx_Effects']:
    table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrike'+suffix)
    assert table,suffix
    report['tables'][suffix]=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
cards={r['Id']:r for r in report['tables']['RogueCards_Cards'] if r['Type']==3}
assert set(cards)=={'06.01','07.01','08.01'}
assert cards['07.01']['MaxAcquisitions']==cards['08.01']['MaxAcquisitions']==1
assert cards['08.01']['RequiredCardIds']==['07.01']
for name in ['NS_GroundBurningStatus','NS_GroundBurningBullet']:
    path='/Game/GuLiStrike/FX/GroundMech/Burning/'+name
    result=unreal.NiagaraService.compile_with_results(path)
    assert result.success and result.error_count==0,str(result)
    report['vfx'][path]={'compile':str(result),'emitters':[str(e) for e in unreal.NiagaraService.summarize(path).get_editor_property('emitter_names')]}
clazz=unreal.EditorAssetLibrary.load_blueprint_class('/Game/GuLiStrike/GroundMech/BP_GroundMech_Light')
for component in unreal.get_default_object(clazz).get_components_by_class(unreal.SkeletalMeshComponent):
    mesh=component.get_editor_property('skeletal_mesh_asset')
    if mesh:report['model_meshes'].append(mesh.get_path_name())
assert '/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Meshes/SK_GroundMech_A4B2.SK_GroundMech_A4B2' in report['model_meshes']
report['map']=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name()
report['dirty_maps']=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
assert not report['dirty_maps']
report['success']=True
(OUT/'formal_readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'report':str(OUT/'formal_readback.json')}))
