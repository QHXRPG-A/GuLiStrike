"""Save the Mass movement review Note last, after runtime validation is finished."""
import json
from pathlib import Path
import unreal

assert not unreal.EditorLevelLibrary.get_pie_worlds(True), 'Stop PIE before saving the review entry'
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name()=='/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'
instructions=f'''Mass 避障简化与转向验证入口（不自动启动PIE）
优化已固定应用：0.5秒预测，最多六候选含最多两个环境优先位，共享索引，最多0.3秒安全零结果缓存，全部Mass车身/炮塔/炮管三倍转向；无本轮运行时开关。
同进程专服＋双客户端，200单位或600单位＋现有三来源500附加飞行物（167/167/166）；每连接250000 B/s，1280×720。原计划四来源之一已退役，本轮实际使用上述三来源。
项目终端：python Scripts/Performance/run_snapshot_parallel_review.py --phase inspect --scenes dense200
压力场景替换为 stress。front/back/split/mixed 切视角；stop/forward/reverse/north/south 改令；空行回车结束并恢复。
观察迎面交错、同向队列、拥堵、窄路与墙边、停止和90°/180°转向。预测最多六候选含最多两个环境优先；软避障仍保留全部接触对与三轮修正。
左上角查看吞吐、连接预算和飞行队列；UE连接与应用载荷、同一连接两端分别报告。
采用依据：此前完整组合在两种负载各三组配对通过CPU与P95门槛；此次移除回退开关，固定该组合。旧40窗口属于此前可切换构建，不把旧报告当作此次新构建重采样。
配对原始数据：Artifacts/MassAvoidance20261011/PairedReport.md；技术记录：Progress/DevelopmentDocumentation/20261011-Mass避障简化与转向提速.md。
场景相机沿用 PerfSnapshotNetwork_Dense200_View 与 PerfSnapshotNetwork_Stress_View。
本Note仅作编辑器入口；自动化与采样结论不代表玩家已验收。'''
api=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
label='PerfMassMovement_Entry'
tag=unreal.Name('MassMovementReview20261011')
with unreal.ScopedEditorTransaction('Mass movement performance review entry'):
    matches=[a for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Note) if a.get_actor_label()==label]
    assert len(matches)<=1
    if matches:
        actor=matches[0]
        assert actor.actor_has_tag(tag), 'Existing unrelated Note'
    else:
        actor=api.spawn_actor_from_class(unreal.Note,unreal.Vector(-10600,65000,1300))
        assert actor
        actor.set_actor_label(label)
        actor.set_editor_property('tags',[tag])
    actor.modify()
    actor.set_editor_property('is_editor_only_actor',True)
    actor.set_folder_path('Performance/MassMovement20261011')
    actor.set_actor_location(unreal.Vector(-10600,65000,1300),False,True)
    actor.set_editor_property('text',instructions)
saved=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
assert saved
actor=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Note) if a.get_actor_label()==label)
assert actor.actor_has_tag(tag) and actor.get_editor_property('is_editor_only_actor')
assert actor.get_editor_property('text')==instructions
cameras=[]
for name in ['PerfSnapshotNetwork_Dense200_View','PerfSnapshotNetwork_Stress_View']:
    camera=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.CameraActor) if a.get_actor_label()==name)
    assert camera.get_editor_property('is_editor_only_actor') and camera.get_auto_activate_player_index()==-1
    cameras.append({'label':name,'path':camera.get_path_name(),'position_cm':list(camera.get_actor_location().to_tuple()),'auto_activate_player_index':-1,'editor_only':True})
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'saved':True,'world':world.get_path_name(),
    'note':{'label':label,'path':actor.get_path_name(),'position_cm':list(actor.get_actor_location().to_tuple()),
            'editor_only':True,'text':instructions},'cameras':cameras,'default_inactive':True},ensure_ascii=False))
