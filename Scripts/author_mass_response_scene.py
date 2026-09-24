"""Update only the Mass response review notes; preserve the legal default army and navigation fixtures."""
import json
from pathlib import Path
import unreal

MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'MassNavigationReview20260923'
root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_editor_world()
assert world.get_path_name() == MAP + '.LVL_CommanderMassPrototype'
assert editor.get_game_world() is None
api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = api.get_all_level_actors()
owned = {a.get_actor_label(): a for a in actors if TAG in [str(t) for t in a.tags]}
assert len(owned) == 14
assert not any(a.get_class().get_name() == 'GuLiCommanderDeploymentPoint' for a in actors)
before = {a.get_path_name(): (list(a.get_actor_location().to_tuple()),list(a.get_actor_scale3d().to_tuple())) for a in actors}
messages = {
 'MassNav_Entry': 'Mass首批响应：保留原图红蓝各250名，自动据点推进保持。选25人或当前可选规模，向25米外下令。首批算好复核后立即提交绿线，位移仍10Hz。连续改令、S停止、减选、双指挥官同时下令均需检查。2000/10000单方部署入口仍待办。',
 'MassNav_CapacityObserve': '封闭区中心(16500,67000)：检查路径拆分与部分失败。已经完成的合法子批可先提交；其余成员保留旧指令和预约。失败、停止和旧命令不得覆盖新绿线。',
 'MassNav_OreMechObserve': '矿体和地面机甲：比较远处建造/拆除与沿途导航变化，远处变化不应重置无关规划。原地或移动机甲仍受局部障碍校验。重连后重新选择，检查配对终点及旧线不复活。诊断开关 guli.Commander.MoveLatencyDiagnostics 1；采样后恢复0。',
}
with unreal.ScopedEditorTransaction('Update Mass immediate response review instructions'):
 for label,text in messages.items():
  a=owned[label]; assert isinstance(a,unreal.Note); a.modify(); a.set_editor_property('text',text)
assert before == {a.get_path_name(): (list(a.get_actor_location().to_tuple()),list(a.get_actor_scale3d().to_tuple())) for a in api.get_all_level_actors()}
validation = unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
assert validation.get_editor_property('success'), validation.get_editor_property('message')
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
rows=[]
for label,a in owned.items():
 r={'name':a.get_name(),'label':label,'class':a.get_class().get_name(),'position':list(a.get_actor_location().to_tuple()),'scale':list(a.get_actor_scale3d().to_tuple())}
 if isinstance(a,unreal.Note): r['text']=a.get_editor_property('text')
 if isinstance(a,unreal.StaticMeshActor):
  r['mesh']=a.static_mesh_component.static_mesh.get_path_name(); r['collision']=str(a.static_mesh_component.get_collision_profile_name())
 rows.append(r)
report={'success':True,'saved':True,'map':MAP,'actors':rows,'unrelated_transforms_preserved':True,'population':{'red':250,'blue':250,'overrides':0},'validation':{k:str(validation.get_editor_property(k)) for k in ('success','message','source_hash','initial_soldier_count','validated_initial_soldier_count')},'runtime_verified':False}
(root/'Artifacts/MassResponse20260923/scene-delivery.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print({'success':True,'actors':len(rows),'validation':report['validation']})
