"""Save only the matching map's editor-only review anchors and player instructions."""
import json
import unreal

assert not unreal.EditorLevelLibrary.get_pie_worlds(True), 'Preserve the user play session'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name() == '/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map work'
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
tag = unreal.Name('GroundSceneUIFeedback20261106')
folder = 'QA/UI/GroundFeedback'
existing = {a.get_actor_label(): a for a in actors.get_all_level_actors() if a.actor_has_tag(tag)}
related = []


def place(cls, label, location):
    actor = existing.get(label)
    if actor is None:
        actor = actors.spawn_actor_from_class(cls, location)
        assert actor
        actor.set_actor_label(label)
        actor.set_editor_property('tags', [tag])
    assert isinstance(actor, cls)
    actor.modify()
    actor.set_editor_property('is_editor_only_actor', True)
    actor.set_folder_path(folder)
    actor.set_actor_location(location, False, True)
    related.append(actor)
    return actor


text = '''游骑兵世界UI：玩家验收入口（2026-11-06）
地图 /Game/Maps/LVL_CommanderMassPrototype；正常房间选择 Ground 席位（游骑兵，ModelId=4001）。正式实现自动生效。
观察现有蓝方交战区；本组标记仅存在于编辑器，不改默认起点、战斗单位或游戏数值。

距离边界：≤350米常态完整，425米约一半，≥500米常态隐藏。以相机到目标包围盒的实际距离为准；四个 View 标记是围绕原交战区的名义距离观察点，移动目标和相机偏移会改变实际距离。
可选准备命令（仅玩家主动运行）：
REVIEW_DISTANCE_M=425; exec(open(r'D:/UE5.7/test1/Scripts/SceneUI/prepare_ground_feedback_review.py',encoding='utf-8').read())
可用350、425、500、600；只把本地玩家移到对应观察点并朝向交战区，不修改敌方对象。

敌方变化案例：观看普通交战，敌方掉血或仅护盾减少时，描边、生命条及有容量的护盾条应完整出现；治疗、回满和护盾恢复也应触发。最后一次变化保持3秒，再淡出0.5秒；连续变化重新计时。
500米外只显示正在提示的敌方；同一模型其他实例不应被一起点亮。近处提示结束仍有距离描边，状态条消失；远处均消失。
友方生命/护盾变化不触发这组提示；游骑兵下敌我及选中单位的底圈全部隐藏，据点光环仍按距离显示。
离屏后不显示箭头；回屏只显示仍在3.5秒有效期内的提示，不重播。死亡、变友方、重生和换席位不会继承旧提示。
切换指挥官或巡天检查既有规则；自身固定HUD保持原样。

本轮已做原生编译与静态/资源结构核对；助手未执行新版自动对局、视觉验收或性能复测。用户已接手验收。
记录：Progress/DevelopmentDocumentation/20261106-游骑兵世界UI分档与敌方变化提示.md
'''
with unreal.ScopedEditorTransaction('Save Ground world UI review entry'):
    note = place(unreal.Note, 'GroundSceneUIFeedback_Entry', unreal.Vector(-13500, 53277, 1600))
    note.set_editor_property('text', text)
    place(unreal.TargetPoint, 'GroundUI_CombatAnchor', unreal.Vector(-6000, 53277, 1100))
    for meters in (350, 425, 500, 600):
        place(unreal.TargetPoint, 'GroundUI_View_' + str(meters) + 'm',
              unreal.Vector(-6000-meters*100, 53277, 1600))

assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
assert note.get_editor_property('text') == text
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'saved': True, 'world': world.get_path_name(),
    'prepare_available': hasattr(unreal.GuLiGroundRogueDebugLibrary, 'prepare_shooter'),
    'entities': [{'label': a.get_actor_label(), 'path': a.get_path_name(), 'class': a.get_class().get_name(),
                  'position_cm': a.get_actor_location().to_tuple(), 'editor_only': a.get_editor_property('is_editor_only_actor'),
                  'folder': str(a.get_folder_path())} for a in related]}, ensure_ascii=False))
