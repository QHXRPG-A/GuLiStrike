"""Save the Ground combat review marker without changing gameplay spawn or assets."""
import json
import unreal

assert not unreal.EditorLevelLibrary.get_pie_worlds(True), "Stop PIE before authoring the entry"
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name() == "/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype"
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), "Preserve pre-existing unsaved map edits"
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
notes = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Note)
label = "PerfGroundCombatHitches_Entry"
tag = unreal.Name("GroundCombatHitches20261106")
matches = [a for a in notes if a.get_actor_label() == label]
assert len(matches) <= 1, label
position = unreal.Vector(-13500, 53277, 1600)
instructions = """游骑兵交战长帧治理：玩家核验入口（2026-11-06）
地图：/Game/Maps/LVL_CommanderMassPrototype。
Ground席位：游骑兵，ModelId=4001，BP_GroundMech_Light；非WM01重防号。
正式实现自动生效：后台事务各阶段最多4项或约1ms后让出，完整逻辑步和输入/回执FIFO保持；小地图八方向排序复用精确深度键；武器准备时预载自身弹丸和命中特效。无需启用候选开关。
本标记仅在编辑器存在，不修改默认起点、相机、单位配置、弹丸数量或画质，不自动启动游戏。

进入：使用本图已有本地PIE房间入口，选择Ground席位；等游骑兵可操控、HUD出现且房间就绪。
蓝方交战准备入口：现有GuLiGroundRogueDebugLibrary.prepare_shooter，将本人移至本标记位置，不改变交战单位的位置。
编辑器Python命令（只在PIE准备阶段执行）：
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc=unreal.GameplayStatics.get_player_controller(w,0)
unreal.GuLiGroundRogueDebugLibrary.prepare_shooter(pc,unreal.Vector(-13500,53277,1600),unreal.Vector(-6000,53277,1100))
然后回到游戏窗口，用正常输入移动和连续开火；跨越小地图地形锚点并旋转视角，检查地形、轮廓、朝向、标记与准星无变化。
核验双方交火、命中/伤害/结束、单位移动与死亡、资源采集和退出清理；首次开火与首次命中特效也需核验。玩家反馈真实观感，技术数据不代替视觉验收。

本轮可复现采样：初始600单位、本机模拟300；1292×997视口，编辑器主窗口最小化、游戏前台，逻辑265步附近开始原生5秒CaptureCPU。准备和数据读取均在计时窗外。
蓝方位置(-13500,53277,1600)，瞄准(-6000,53277,1100)，前移并持续普通开火，HotspotMask=31、GameplayWorkers=2。
脚本：D:/UE5.7/PerformanceCaptures/GuLiStrike/20261106-ground-combat-hitches/capture_ground.py；命令后附全新标签，正式版本不传历史batch参数。
三轮旧/新版各884完整帧：>25ms为39→9，>30ms为9→0；平均16.926→16.939ms，P99 30.074→25.059ms。后台维护峰值126.875→2.822ms，输入排队均值20.192→1.894ms，>=3批逻辑追赶53→1帧，HUD Tick峰值12.929→5.001ms。
收益是减少长帧，平均帧耗时基本持平；短窗Development测试不代表所有战况或Shipping保证。
渲染诊断：当前较大Ribbon申请来自采矿激光NS_MiningLaser_Optimized_GPU_All_ThreeTier，最大2.717ms；原始24.624ms上传阻塞未复现，不能声称已根除。诊断补丁仅补充资源名称。
技术检查/编译/性能测量已完成；玩家视觉验收待反馈，助手未截图或读图。
记录：Progress/DevelopmentDocumentation/20261106-游骑兵交战长帧治理.md。
证据：D:/UE5.7/PerformanceCaptures/GuLiStrike/20261106-ground-combat-hitches/comparison.json。
"""

replacement = "事务调度现已正式接入：收到的对端事务进入就绪队列，后台维护各阶段最多尝试4项或约1ms；单个事务不截断，剩余工作继续唤醒并轮转。主线程输入/回执FIFO与完整逻辑步骤、身份、权威和最终数值校验保持。旧候选CVar已移除，无需启用命令。游骑兵专项入口见PerfGroundCombatHitches_Entry及20261106-游骑兵交战长帧治理开发文档。"
historical = "build-48事务调度候选已由正式有界维护替换，诊断脚本不再传递旧候选参数。汇报先给总耗时：从指令入口到全部成员模型提交的最晚时间；绿线、起步与模型都是累计值，不相加。保留1cm和2cm两种总耗时。"
related = []
with unreal.ScopedEditorTransaction("Save Ground combat hitch review entry"):
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
    note.set_folder_path("QA/Performance/GroundCombat")
    note.set_actor_location(position, False, True)
    note.set_editor_property("text", instructions)
    related.append(note)
    for old in notes:
        if old.get_actor_label() != "PerfHybridCPU_Entry":
            continue
        text = old.get_editor_property("text")
        lines = text.splitlines(keepends=True)
        updated = "".join((replacement + "\n") if line.startswith("事务调度候选默认0：")
                          else (historical + "\n") if line.startswith("验证build-48事务调度候选再追加：")
                          else line for line in lines)
        if updated != text:
            old.modify()
            old.set_editor_property("text", updated)
        assert "gs.Hybrid.NetworkRetryBatchLimit" not in updated
        assert "gs.Hybrid.DeferPeerTransactions" not in updated
        related.append(old)

assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level(), "Map save failed"
assert note.get_editor_property("text") == instructions
assert note.actor_has_tag(tag) and note.get_editor_property("is_editor_only_actor")
assert all(abs(a-b) < .01 for a,b in zip(note.get_actor_location().to_tuple(), position.to_tuple()))
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
unreal.MCPythonHelper.submit_result(json.dumps({
    "success": True, "saved": True, "world": world.get_path_name(),
    "qa_prepare_available": hasattr(unreal.GuLiGroundRogueDebugLibrary, "prepare_shooter"),
    "related_entities": [{"label": a.get_actor_label(), "path": a.get_path_name(),
                          "class": a.get_class().get_name(), "position_cm": list(a.get_actor_location().to_tuple()),
                          "editor_only": a.get_editor_property("is_editor_only_actor"),
                          "folder": str(a.get_folder_path()), "text": a.get_editor_property("text")}
                         for a in related],
}, ensure_ascii=False))
