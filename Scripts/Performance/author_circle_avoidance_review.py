"""Save and read back the current player entry without launching gameplay."""
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name() == '/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
tag = unreal.Name('CircleAvoidanceReview20261111')
folder = 'Review/CircleAvoidance20261111'
instructions = r'''圆形避障、原主动绕侧与0.5秒让路（10Hz）
正式玩法使用原Demo主动绕侧：直行受阻时左右各尝试20/37/55/73/91/111/134度，比较转角、可走距离和记忆侧偏好，接触后切线滑动。NavMesh负责全局寻路。
局部空间索引、缓存角度正余弦、复用选中方向的扫圆结果、覆盖格不变时只更新圆位置；一个单位移动后立即发布位置。每步最多16次扫圆，没有换边冷却或增角档位。
地面与重防号低空层互不阻挡；建筑和矿簇阻挡两层；玩家圆形障碍不自动侧绕。
受阻需要侧绕即可请求首个阻挡者向相反横向让路，不要求已经移动成功。同一条命令的成员不互相让路，已落位成员保留命令归属；每跳仅小向大或同尺寸、同移动层生效。
让路者按正常速度横移0.5秒，从下一逻辑步开始；再次被挡时，复用本次碰撞命中把相同方向传给直接阻挡者，下一步执行。每步只通知一人，无整群遍历或额外寻路。结束后继续原任务，停驻者留在让开处。
直接Play选择指挥官，正常框选后右键下令即可测试。
预置场景预览：在项目PowerShell运行 .\Scripts\Performance\Open-CircleAvoidanceReview.ps1 -Scenario crossing
可选 hold / gather / mixed / attack / buildings / ore / work；预览先回放30秒命令，然后保留窗口供玩家继续操作，停止PIE后自动恢复原编辑器设置。
crossing为两队对穿，hold为一队静止，gather为分层集结，mixed为大小单位混合，attack为围攻，buildings/ore为建筑矿簇绕行，work为采矿回厂施工。
源表：data/Excel/GuLiStrikeAvoidance.xlsx。半径、净距、前探、角度与YieldSeconds在Units；模型大小使用独立GridReferenceDiameterMeters。改数值后导出、导入并新开对局。
普通集结进入目标区域、短暂无进展后就地落位；矿车和工程车保留实际工作点。工厂恢复原卸货点，没有新增单车入口预约。
最新性能报告：Artifacts/YieldRelay20261111/性能对比.md；历史采样：Artifacts/CircularAvoidance20261110。报告分别标明版本，不混用历史试版结论。用户接受穿透，手感由玩家直接核验。
实体核对与运行采样不代表玩家手感或视觉验收。'''

def authored(label, cls, location):
    matches = [a for a in actors.get_all_level_actors() if a.get_actor_label() == label]
    assert len(matches) <= 1
    if matches:
        actor = matches[0]
        assert isinstance(actor, cls) and actor.actor_has_tag(tag), 'Unrelated actor owns this name'
    else:
        actor = actors.spawn_actor_from_class(cls, location)
        assert actor
        actor.set_actor_label(label)
        actor.set_editor_property('tags', [tag])
    actor.modify()
    actor.set_editor_property('is_editor_only_actor', True)
    actor.set_folder_path(folder)
    actor.set_actor_location(location, False, True)
    return actor

with unreal.ScopedEditorTransaction('Circle avoidance player review entry'):
    note = authored('CircleAvoidanceReview_Entry', unreal.Note, unreal.Vector(-35000, 62000, 1200))
    note.set_editor_property('text', instructions)
    camera = authored('CircleAvoidanceReview_View', unreal.CameraActor,
                      unreal.Vector(-54485.1674985583, 62000, 14413.904208077727))
    camera.set_actor_rotation(unreal.Rotator(-34.33727264404303, 0, 0), False)
    for old in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Note):
        if old.get_actor_label() == 'PerfMassMovement_Entry' and old.actor_has_tag(unreal.Name('MassMovementReview20261011')):
            old.modify()
            old.set_editor_property('text', '本轮正式避障已替换；当前操作说明见 CircleAvoidanceReview_Entry。历史报告仍保留在 Artifacts/MassAvoidance20261011。')

assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
readback = []
for label in ('CircleAvoidanceReview_Entry', 'CircleAvoidanceReview_View'):
    actor = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == label)
    assert actor.actor_has_tag(tag) and actor.get_editor_property('is_editor_only_actor')
    location = actor.get_actor_location()
    readback.append({'label': label, 'path': actor.get_path_name(), 'class': actor.get_class().get_name(),
                     'location': [location.x, location.y, location.z], 'editor_only': True})
assert note.get_editor_property('text') == instructions
assert camera.get_auto_activate_player_index() == -1
result = {'success': True, 'saved': True, 'map': world.get_path_name(), 'entities': readback,
          'instructions': instructions, 'visual_inspection': 'not performed; reserved for player'}
(root / 'Artifacts/CircularAvoidance20261110/player-entry-readback.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=False))
