"""Save the inactive commander combat/navigation review entry; never starts PIE."""
import json
import unreal

assert not unreal.EditorLevelLibrary.get_pie_worlds(True), "Stop PIE before authoring"
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name() == "/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype"
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
label = "Commander_CombatNav_Review"
tag = unreal.Name("CommanderCombatNavReview20261021")
matches = [actor for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Note)
           if actor.get_actor_label() == label]
assert len(matches) <= 1
position = unreal.Vector(-9000, 38000, 2300)
instructions = """扫荡者攻击与Mass导航恢复验证入口（默认停用）
地图：/Game/Maps/LVL_CommanderMassPrototype。
此Note为编辑器开发说明，不自动游玩、生成部队、重建导航或记录数据。

扫荡者：兵种UnitTypeId=5，由先驱号(UnitTypeId=1)的Q技能召唤。
按当前人口与容量规则准备召唤条件；选择己方先驱号召唤扫荡者，再将扫荡者移动到敌方武器射程内。
预期：敌方正常索敌、开火并结算伤害。先驱号基础射程60m，重防号基础射程120m；实际技能数值以当前已提交配置为准。
界面友方显示蓝色、敌方显示红色；不要以颜色代替逻辑阵营或模拟拥有者。

移动：在本Note附近斜坡选择己方重防号，下达移动、连续改令和停止。
原复现单位：276,277,281,282,278,283,287,286,279。
斜坡准备数据与技术结果：Progress/Evidence/20261021-CommanderRegression。
显式PIE空间准备API：GuLiComponentSkillQALibrary.prepare_soldier_positions(pc, ids, positions)。
它仅准备本图25个以内本机模拟单位的位置，不修改健康、拥有权、射程或伤害，不伪造成功结果。
预期：接令后能移动；导航面重建后恢复有效引用，原地重绑不产生位置跳变。
gs.GM.Commander.Nav.Soldier <ID> 可手动读取导航状态。

gs.LocalPIE.LogicDiagnostics 默认0；显式设1后重开PIE才记录敌方逻辑进程快照，诊断期间不做性能验收。
技术复测：build-57，扫荡者远端40包全部应用，无无效状态；原9单位全部恢复节点并实际移动。
用户自行核验模型、开火、受击和移动观感。助手禁止截图读图或视口目检。
本轮不代表20–30ms响应或15ms整帧目标达标。
开发记录：Progress/DevelopmentDocumentation/20261021-扫荡者远端姿态与导航恢复修复.md。
"""

with unreal.ScopedEditorTransaction("Commander combat and navigation review entry"):
    if matches:
        note = matches[0]
        assert note.actor_has_tag(tag), "Refusing to replace an unrelated Note"
    else:
        note = actors.spawn_actor_from_class(unreal.Note, position)
        assert note
        note.set_actor_label(label)
        note.set_editor_property("tags", [tag])
    note.modify()
    note.set_editor_property("is_editor_only_actor", True)
    note.set_folder_path("Development/CommanderRegression20261021")
    note.set_actor_location(position, False, True)
    note.set_editor_property("text", instructions)

assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
assert note.get_editor_property("text") == instructions
assert note.actor_has_tag(tag) and note.get_editor_property("is_editor_only_actor")
assert all(abs(a - b) < .01 for a, b in zip(note.get_actor_location().to_tuple(), position.to_tuple()))
unreal.MCPythonHelper.submit_result(json.dumps({
    "success": True, "saved": True, "default_inactive": True, "world": world.get_path_name(),
    "related_entities": [{"label": label, "path": note.get_path_name(),
                          "class": note.get_class().get_name(), "position_cm": list(position.to_tuple()),
                          "editor_only": True, "text": note.get_editor_property("text")}],
}, ensure_ascii=False))
