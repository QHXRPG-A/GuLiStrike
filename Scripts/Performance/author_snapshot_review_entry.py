"""Last editor mutation for snapshot/network review; run after runtime validation."""
import json
import unreal

assert not unreal.EditorLevelLibrary.get_pie_worlds(True), 'Stop PIE before saving the review entry'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name() == '/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'
actors_api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
tag = unreal.Name('SnapshotNetworkReview20261010')
specs = [
    ('PerfSnapshotNetwork_Entry', unreal.Note, [-10800, 65000, 1300], None),
    ('PerfSnapshotNetwork_Dense200_View', unreal.CameraActor, [-10800, 61000, 15500], [-10800, 65000, 1000]),
    ('PerfSnapshotNetwork_Stress_View', unreal.CameraActor, [15000, 58000, 16000], [15000, 65000, 5401.779]),
]
instructions = '''网络诊断与回退基线（默认不运行）
同进程专服＋双客户端，200单位或600单位＋三来源500飞行物；250000 B/s，1280×720。
项目终端：python Scripts/Performance/run_snapshot_parallel_review.py --phase inspect --scenes dense200
压力场景替换为 stress。终端 front/back/split/mixed 切视角，stop/reverse/forward 改令，空行回车结束并恢复。
左上角查看 FPS、延迟、收发吞吐、连接预算和飞行队列；不要把同一连接两端相加。
四项未通过的新优化已从代码回退；保留原生网络采集、投影缓存、既有避障查询优化与表现批处理。
回退记录与当前构建：Artifacts/PerformanceOptimization20261010/Rollback/README.md。历史配对报告保留。
截帧已定位车辆模型与Slate波动，但组合P95未获稳定收益，按用户决定结束该轮候选。网络协议和可靠性保持。
本Note与观察相机仅作编辑器入口；保存地图不代表玩家验收。'''

with unreal.ScopedEditorTransaction('Snapshot performance and network review entry'):
    for label, cls, position, target in specs:
        matches = [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, cls) if a.get_actor_label() == label]
        assert len(matches) <= 1, label
        if matches:
            actor = matches[0]
            assert actor.actor_has_tag(tag), 'Existing unrelated actor: ' + label
        else:
            actor = actors_api.spawn_actor_from_class(cls, unreal.Vector(*position))
            assert actor
            actor.set_actor_label(label)
            actor.set_editor_property('tags', [tag])
        actor.modify()
        actor.set_editor_property('is_editor_only_actor', True)
        actor.set_folder_path('Performance/SnapshotNetwork20261010')
        actor.set_actor_location(unreal.Vector(*position), False, True)
        if target:
            actor.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(unreal.Vector(*position), unreal.Vector(*target)), True)
            actor.set_editor_property('auto_activate_for_player', unreal.AutoReceiveInput.DISABLED)
            actor.get_component_by_class(unreal.CameraComponent).set_field_of_view(90.0)
        else:
            actor.set_editor_property('text', instructions)

saved = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
assert saved, 'Map save failed'
rows = []
for label, cls, position, target in specs:
    actor = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, cls) if a.get_actor_label() == label)
    assert actor.actor_has_tag(tag) and actor.get_editor_property('is_editor_only_actor')
    row = {'label': label, 'class': actor.get_class().get_name(), 'path': actor.get_path_name(),
           'position_cm': list(actor.get_actor_location().to_tuple()), 'editor_only': True}
    if target:
        row['auto_activate_player_index'] = actor.get_auto_activate_player_index()
        assert row['auto_activate_player_index'] == -1
    else:
        row['text'] = actor.get_editor_property('text')
        assert row['text'] == instructions
    rows.append(row)
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'saved': saved, 'world': world.get_path_name(),
    'default_inactive': True, 'related_entities': rows}, ensure_ascii=False))
