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
正式玩法使用原Demo主动绕侧：首次受阻时左右各尝试20/37/55/73/91/111/134度，比较转角、可走距离和初始侧偏好；已选侧仍可行就继续，当前侧不可行才换侧，完整前探畅通后清除记忆。接触后切线滑动。NavMesh负责全局寻路。
局部空间索引、缓存角度正余弦、复用选中方向的扫圆结果、覆盖格不变时只更新圆位置；一个单位移动后立即发布位置。每步最多16次扫圆，没有换边冷却或增角档位。
地面与重防号低空层互不阻挡；建筑和矿簇阻挡两层；玩家圆形障碍不自动侧绕。
受阻需要侧绕即可请求首个阻挡者向相反横向让路，不要求已经移动成功。同一条命令的成员不互相让路，已落位成员保留命令归属；同移动层内统一让路，不比较双方体型，大单位也可向小单位让路。
让路者按正常速度横移0.5秒，从下一逻辑步开始；再次被挡时，复用本次碰撞命中把相同方向和最初发起者的命令传给直接阻挡者；该请求未被接纳时，再用已有滑动命中请求滑动阻挡者，下一步执行。同命令成员不主动互让，但能传递外部命令的让路请求。每步最多通知一人，无整群遍历、额外碰撞查询或寻路。结束后继续原任务，停驻者留在让开处。
直接Play选择指挥官，正常框选后右键下令即可测试。
预置场景预览：在项目PowerShell运行 .\Scripts\Performance\Open-CircleAvoidanceReview.ps1 -Scenario crossing
可选 hold / gather / mixed / attack / buildings / ore / work；预览先回放30秒命令，然后保留窗口供玩家继续操作，停止PIE后自动恢复原编辑器设置。
crossing为两队对穿，hold为一队静止，gather为分层集结，mixed为大小单位混合，attack为围攻，buildings/ore为建筑矿簇绕行，work为采矿交矿施工。
源表：data/Excel/GuLiStrikeAvoidance.xlsx。半径、净距、前探、角度与YieldSeconds在Units；模型大小使用独立GridReferenceDiameterMeters。改数值后导出、导入并新开对局。
普通集结进入目标区域、短暂无进展后就地落位；矿车和工程车保留实际工作点。矿车在护盾发生器外围交矿并加工；先驱号兵营阻挡普通移动，自产先驱号从内部出营时忽略自身兵营边界，门、天花板和坡道不提供模型碰撞。
本轮由Mass直接持有权威单位状态，取消外部运行时副本和结果回写；保留项目自有10Hz移动、侧绕、0.5秒让路和按SoldierId逐单位立即提交的顺序，不启用官方避障。
2026-11-11矿簇旁往返修复（v6）：中途路径拐点按单位圆半径经过，不再要求大单位贴近到0.5米；最终落位、采矿和卸货到达精度不变。矿簇39附近验证位置见CircleAvoidancePendulum_Entry。
2026-11-11人墙被困修复（v7）：让路链保留最初行进者的命令，不再把小单位群自己的命令当成请求来源。两台先驱号与85个小单位的现场位置复现见CircleAvoidanceCrowd_Entry；重新准备入口：python -X utf8 Artifacts/CrowdTrap20261111/replay.py open（先停止PIE）。
当前v9：按玩家要求移除全部让路体型判断，所有可自动让路单位走同一逻辑；直行请求未被接纳时使用已有滑动命中，每步最多通知一人。先驱号245现场见CircleAvoidanceSlideYield_Entry；重新准备入口：python -X utf8 Artifacts/CrowdTrapFollowup20261111/replay.py open（先停止PIE）。
最新性能报告：Artifacts/MassAuthority20261111/性能对比.md（500/1000移动单位）；上一轮数据精简：Artifacts/MassMovementData20261111/性能对比.md；让路版本比较：Artifacts/YieldRelay20261111/性能对比.md。报告分别标明版本。用户接受穿透，手感由玩家直接核验。
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
    pendulum = authored('CircleAvoidancePendulum_Entry', unreal.Note, unreal.Vector(-7794, 53387, 1100))
    pendulum.set_editor_property('text', '矿簇39旁往返修复入口。正常Play选择指挥官，选先驱号，从矿簇南侧(-9700,53000)向东北(-7350,58050)移动；观察是否继续绕过矿簇，而非在拐点两侧来回抽动。再测试矿车回厂。路径和让路均走正式入口；手感由玩家核验。')
    crowd = authored('CircleAvoidanceCrowd_Entry', unreal.Note, unreal.Vector(-12160, 70500, 1200))
    crowd.set_editor_property('text', 'v7人墙让路入口。先停止PIE，在项目根目录运行 python -X utf8 Artifacts/CrowdTrap20261111/replay.py open；会在此处摆回两个先驱号和85个扫荡者，并选中两台大单位。向东侧约(-8000,71200)右键下令，观察小单位是否逐层腾空、大单位能否脱困；同一命令内部仍不主动互让。0.5秒让路与原主动侧绕不变。结构化复测已完成，手感由玩家核验。')
    slide_yield = authored('CircleAvoidanceSlideYield_Entry', unreal.Note, unreal.Vector(11100, 45880, 1100))
    slide_yield.set_editor_property('text', '当前v9统一让路入口：不比较体型，0.5秒、同命令不主动互让、下一逻辑步传递保持。先停止PIE，在项目根目录运行 python -X utf8 Artifacts/CrowdTrapFollowup20261111/replay.py open；摆回先驱号245及周围单位并选中245。向西南约(8360,42293)右键下令，观察能否滑动腾出空间并穿过小单位群。也可选小单位经过静止大单位，核验大单位会让路。手感由玩家确认。')
    for old in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Note):
        if old.get_actor_label() == 'PerfMassMovement_Entry' and old.actor_has_tag(unreal.Name('MassMovementReview20261011')):
            old.modify()
            old.set_editor_property('text', '本轮正式避障已替换；当前操作说明见 CircleAvoidanceReview_Entry。历史报告仍保留在 Artifacts/MassAvoidance20261011。')

assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
readback = []
for label in ('CircleAvoidanceReview_Entry', 'CircleAvoidanceReview_View', 'CircleAvoidancePendulum_Entry', 'CircleAvoidanceCrowd_Entry', 'CircleAvoidanceSlideYield_Entry'):
    actor = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == label)
    assert actor.actor_has_tag(tag) and actor.get_editor_property('is_editor_only_actor')
    location = actor.get_actor_location()
    readback.append({'label': label, 'path': actor.get_path_name(), 'class': actor.get_class().get_name(),
                     'location': [location.x, location.y, location.z], 'editor_only': True})
assert note.get_editor_property('text') == instructions
assert camera.get_auto_activate_player_index() == -1
result = {'success': True, 'saved': True, 'map': world.get_path_name(), 'entities': readback,
          'instructions': instructions, 'visual_inspection': 'not performed; reserved for player'}
(root / 'Artifacts/CrowdTrapFollowup20261111/player-entry-readback.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=False))
