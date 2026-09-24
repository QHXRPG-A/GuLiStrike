"""Save and read back the existing Mass review fixtures; no population or terrain changes."""
import json
from pathlib import Path
import unreal

MAP='/Game/Maps/LVL_CommanderMassPrototype'
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert editor.get_editor_world().get_path_name()==MAP+'.LVL_CommanderMassPrototype'
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
api=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors=api.get_all_level_actors()
owned={a.get_actor_label():a for a in actors if 'MassNavigationReview20260923' in [str(t) for t in a.tags]}
assert len(owned)==14
before={a.get_path_name():(a.get_actor_location().to_tuple(),a.get_actor_scale3d().to_tuple()) for a in actors}
messages={
 'MassNav_Entry':'Mass即时移动：保留NavMesh与自动推进。命令接纳后立即行进，共享路线随后接管。整条命令内随机分配不同站位；取选中模型缩放后包围盒XY最长边作为间距，圈距等于此宽度，点距不小于此宽度。10Hz位移、客户端插值；绿线仍指向点击点。测试25/100及混合体型选兵，确认全部批次使用同一间距、站位不重复，并记录实际移动响应。',
 'MassNav_CornerObserve':'墙角：点击墙后方，路线尚未完成时保持趋近及表面约束；共享路线准备好后沿走廊绕行。禁止穿墙和到达吸附。远处造楼不应清空无关路线。',
 'MassNav_NarrowObserve':'窄通道：群体共享NavMesh走廊，不执行个人连接校验、槽位匹配或失败拆组。保留局部避障，拥堵时允许重叠和持续等待。',
 'MassNav_CapacityObserve':'封闭区(16500,67000)：允许长期受阻，不因5/30秒超时结束任务。站位仅查NavMesh覆盖，同一命令不重复分配；点数不足的成员保留移动意图，不争夺已分配位置、不新增个人寻路。S停止后旧绿线不得复活。大人口合法部署入口仍沿用原待办。',
 'MassNav_OreMechObserve':'矿体与机甲：保留行进避障、绕行侧滞回、最后合法位置与真实速度回写。分别验证矿体消失、机甲通过、沿途/远处导航变化、连续改令、双指挥官、重同步。诊断开关：guli.Commander.MoveLatencyDiagnostics 1。',
}
with unreal.ScopedEditorTransaction('Mass immediate shared NavMesh instructions'):
 for label,text in messages.items():
  a=owned[label];a.modify();a.set_editor_property('text',text)
assert before=={a.get_path_name():(a.get_actor_location().to_tuple(),a.get_actor_scale3d().to_tuple()) for a in api.get_all_level_actors()}
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
rows=[]
for label,a in owned.items():
 r={'label':label,'class':a.get_class().get_name(),'position':a.get_actor_location().to_tuple(),'scale':a.get_actor_scale3d().to_tuple()}
 if isinstance(a,unreal.Note):r['text']=a.get_editor_property('text')
 if isinstance(a,unreal.StaticMeshActor):r.update(mesh=a.static_mesh_component.static_mesh.get_path_name(),collision=str(a.static_mesh_component.get_collision_profile_name()))
 rows.append(r)
report={'saved':True,'map':MAP,'actor_count':len(actors),'unrelated_transforms_preserved':True,'fixtures':rows,'validation':'Note text only; actor configuration read back. PIE authoring gate is recorded separately in the runtime log.'}
Path('D:/UE5.7/test1/Artifacts/MassImmediate20260924/scene-delivery.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print({'saved':True,'fixtures':len(rows),'validation':report['validation']})
