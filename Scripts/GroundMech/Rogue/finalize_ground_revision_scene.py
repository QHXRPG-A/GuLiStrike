"""Save the existing review entry and regenerate both versioned formal manifests."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=Path(globals().get('GULI_PROBE_OUT',ROOT/'Artifacts/GroundRevision_20261103'))
MAP='/Game/Maps/LVL_CommanderMassPrototype'
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert not level.is_in_play_in_editor()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_outermost().get_name()==MAP
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
actors={a.get_actor_label():a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()}
notes={
 'GroundBuff_Instructions':'Red Ground：F4正面单击获取灼烧，再获取爆燃；折射可重复取得。灼烧弹道为火红色，已修复SM6材质。折射以首次命中点为固定50米三维球，各跳就近、无遮挡、不重复；原Enemy_07至09和远靶用于检查不会逐跳扩散出圈。',
 'GroundBuff_FiveMeter_Instructions':'向Host射击：爆燃0.5秒扩张到5米外缘，再0.5秒保持范围淡出；伤害仍在2秒周期立即结算。Inside/BodyIntersection中心6米但碰撞体进入5米球；Outside约13.4米不受伤，Friendly友军不受伤，Shield优先吸收。以真实受击中心核对，基础灼烧独立播放。',
 'GroundHUD_v1_PlayerEntry':'Ground HUD：强化三列图标、右下当前等级，最多五行后滚动；F4候选显示取得后等级，正面单击选牌。八张极简图标由Icons表统一管理，血量和属性合并。M地图、F4选牌，已移除I详情；原包局部刻度弧在入场后循环，模态打开或HUD隐藏时暂停。普通刷新5Hz，事件帧末同步。视觉由玩家验收，助手不截图读图。'}
for name,text in notes.items():
 a=actors[name];a.modify();a.set_editor_property('text',text)
assert unreal.EditorLoadingAndSavingUtils.save_map(world,MAP)
entities=[]
for name,a in actors.items():
 if not name.startswith(('GroundRogue_','GroundBuff_','GroundHUD_')):continue
 row={'label':name,'class':a.get_class().get_name(),'location':list(a.get_actor_location().to_tuple()),'map':a.get_outermost().get_name()}
 if name in notes:assert str(a.get_editor_property('text'))==notes[name]
 if isinstance(a,unreal.GuLiCommanderDeploymentPoint):
  row.update({k:str(a.get_editor_property(k)) for k in ['team','unit_type_id','rows','columns','allow_automatic_fire','start_idle','initial_max_health_override','initial_shield']})
 entities.append(row)
assert unreal.GuLiResourceAuthoringLibrary.validate_current_bake().success
assert unreal.GuLiNavigationBakeLibrary.validate_world_navigation(world).success
manifests=[]
for population in (200,600):
 path=ROOT/f'Config/Hybrid/RuntimeManifest-{population}.json'
 result=json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population,str(path)))
 assert result.get('success'),result
 validation=json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(path)))
 assert validation.get('success'),validation
 manifests.append({'population':population,'export':result,'validation':validation})
report={'success':True,'map':MAP,'entities':entities,'manifests':manifests,'visual_test':False}
(OUT/'scene_final.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'entities':len(entities),'manifests':manifests}))
