"""After compiled actions/VFX installation: import scoped tables and save the normal map.
Never starts PIE and never creates replacement combat/mining gameplay fixtures.
"""
import unreal,json,runpy,math
from pathlib import Path
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'ArtSource/CommanderRecoil_20261114';MAP='/Game/Maps/LVL_CommanderMassPrototype'
AS=unreal.EditorAssetLibrary;ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

def main():
    assert not unreal.EditorLevelLibrary.get_pie_worlds(True),'User must stop PIE before delivery'
    for name in ('ue-actions-readback.json','muzzle-glow-readback.json'):assert json.loads((OUT/name).read_text(encoding='utf8'))['success']
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world and world.get_path_name().split('.')[0]==MAP,'Keep the requested normal gameplay map'
    result=runpy.run_path(str(ROOT/'Scripts/import_data_to_engine.py'),init_globals={
        'GULI_TABLE_FILTER':['DT_GuLiStrikeCommander_UnitSkills','DT_GuLiStrikeVfx_Effects'],
        'GULI_IMPORT_REPORT':str(OUT/'table-import.json'),'GULI_IMPORT_PROGRESS':str(OUT/'table-import.log')})['report']
    assert not result['errors'] and len(result['tables'])==2 and all(x.get('imported') for x in result['tables'])
    tables={}
    for name in ['Commander_UnitSkills','Commander_Soldiers','Models_Models','Vfx_Effects']:
        rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrike'+name)))
        tables[name]=rows
    skills=[x for x in tables['Commander_UnitSkills'] if x['UnitTypeId'] in (1,2) and x['SlotId']=='BasicAttack']
    assert skills
    for r in skills:assert r['BurstRoundCount']==(8 if r['UnitTypeId']==1 else 4) and r['BurstRestSeconds']==2
    miner=next(x for x in tables['Commander_Soldiers'] if x['Id']==3)
    source=next(x for x in json.loads((ROOT/'Data/Json/DT_GuLiStrikeCommander_Soldiers.json').read_text(encoding='utf8')) if x['Id']==3)
    assert math.isclose(miner['PresentationScale'],source['PresentationScale'],rel_tol=1.e-5)
    for field in ('MovementSpeedCmPerSecond','MovementSpeedCentimetersPerSecond'):
        if field in miner:assert miner[field]==900
    effect=next(x for x in tables['Vfx_Effects'] if x['Name']=='CommanderMuzzleGlow')
    for field in ('ResourcePath','ReducedResourcePath','MinimalResourcePath','BatchResourcePath','ReducedBatchResourcePath','MinimalBatchResourcePath'):
        assert unreal.load_asset(effect[field]),field
    economy=unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy')
    layout=unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap')
    assert str(layout.get_editor_property('map_package'))==MAP
    assert layout.calculate_layout_hash()==layout.get_editor_property('layout_hash')
    assert economy.get_editor_property('mining_vehicle_unit_type_id')==3
    anchors=layout.get_editor_property('spawn_anchors');made=[]
    existing=ACTORS.get_all_level_actors()
    for side in ('red','blue'):
        label='CommanderActionReview_'+side
        matches=[a for a in existing if a.get_actor_label()==label];assert len(matches)<=1
        position=anchors.get_editor_property(side+'_assembly')+unreal.Vector(0,0,350)
        note=matches[0] if matches else ACTORS.spawn_actor_from_class(unreal.Note,position)
        note.modify();note.set_actor_label(label);note.set_folder_path('GuLiStrike/Review/CommanderActions_20261114')
        note.set_editor_property('is_editor_only_actor',True)
        note.set_editor_property('text','正常指挥官玩法审核：\n矿车直行/转向按1.17秒循环；停稳0.2秒后混回待机；采矿直接后坐，2.46秒循环。\n重防号左右4发后额外停2秒；先驱者双枪8轮后额外停2秒；用正常攻击指令观察。\n重防号解锁Q导弹后持续后倾，导弹层不回缩枪管；结束平稳回正。\n近远景切换LOD，观察后坐、灯芯、奶白枪口亮芯与暖光晕；停火后完成回弹。\n此标记不改变部队、矿石、移速、伤害或正常生产流程。视觉效果由玩家审核。')
        made.append(note)
    assert LEVELS.save_current_level()
    # Rebuild the local map-bound snapshot after the new Army/Mass schemas and
    # source tables are installed. Binary resources and this snapshot stay local
    # when publishing a source-only delivery.
    manifest=json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(600,str(ROOT/'Config/Hybrid/RuntimeManifest-600.json')))
    assert manifest['success'],manifest
    configs={'map':MAP,'notes':[{'label':a.get_actor_label(),'location':list(a.get_actor_location().to_tuple()),'editor_only':a.get_editor_property('is_editor_only_actor')} for a in made],
        'resource_layout_hash':str(layout.get_editor_property('layout_hash')),'initial_mining_vehicles_per_team':economy.get_editor_property('initial_mining_vehicles_per_team'),
        'mining_vehicle_unit_type_id':economy.get_editor_property('mining_vehicle_unit_type_id'),'miner':miner,'basic_attack_rows':skills,'muzzle_effect':effect,
        'game_mode':str(world.get_world_settings().get_editor_property('default_game_mode')),'runtime_manifest':manifest,'play_started':False,'runtime_visual_acceptance':'pending user','success':True}
    (OUT/'delivery-scene-readback.json').write_text(json.dumps(configs,ensure_ascii=False,indent=2),encoding='utf8')
    unreal.MCPythonHelper.submit_result(json.dumps(configs,ensure_ascii=False))

if __name__=='__main__':main()
