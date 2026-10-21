"""Save the opt-in Hybrid CPU review entry in its existing production test map."""
import json
import unreal

assert not unreal.EditorLevelLibrary.get_pie_worlds(True), "Stop PIE before authoring the entry"
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name() == "/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype"
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
label = "PerfHybridCPU_Entry"
tag = unreal.Name("HybridCPUReview20261020")
matches = [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Note)
           if a.get_actor_label() == label]
assert len(matches) <= 1, label
position = unreal.Vector(10139.4320904, -55434.2763966, 1101.7790833)
instructions = """Hybrid交火CPU与指令响应验证入口（默认停用，不自动开始游玩）
地图：/Game/Maps/LVL_CommanderMassPrototype；当前正式配置采用快速小消息压缩。
阶段参考：build-41同构建单轮标准/快速压缩16.2188/16.2310ms，P95 26.3908/26.0135ms；单轮不代表稳定收益。
当前目标：指挥官从点击到绿线提交、全部受令本机Mass单位实际起步及模型提交均严格小于20ms，不增加网络延迟容差。完整目标尚未通过。
在项目根目录使用新输出目录：
python Scripts/Performance/capture_hybrid_cpu.py --output D:/UE5.7/PerformanceCaptures/GuLiStrike/ManualHybridCPUReview --variants standard_compression,candidate --rounds 3 --no-trace --editor-window-state minimized
初始600单位，固定原生交火输入；至少预热10秒，逻辑第3600步开始记录30秒；2359×1748视口、相机目标高度133.4米。
计时前最小化编辑器主窗口、激活游戏预览；逐帧核对两项窗口条件。计时中不切窗口、截图、枚举对象、移动镜头或改变负载。脚本结束后恢复开关并关闭所属会话。
正式快速压缩：gs.Hybrid.FastSmallPayloadCompression默认1；可靠消息正文不超过64KiB时使用无损低压缩级别，大正文保持标准级别；协议、校验、身份和队列字节预算保持。0为独立回退，在新会话生效并传递给逻辑客户端。
正式UI：gs.SceneUI.IndexedGeometry=1，gs.SceneUI.OptimizedPaint=1；分别置0用于回退。Slate.EnableGlobalInvalidation保持0。
四档：LOD0高度35–100米；LOD1为100–300米，仅施加资源LOD1精度上限；LOD2为300–700米综合精简；LOD3为总览。描边仅LOD0。gs.Commander.MeshLOD=0独立回退网格约束。
本机响应：立即意图绿线、纯兵种选区索引、60Hz手动移动与直接本机姿态、移动后同步发布、就绪路线索引及活动端点软避让均保持正式默认；完整成员/身份/权限校验和混合选区回退保留。
结果应用保持常规顺序；弹丸发布保持完整编码；可靠包保持完整排序；VAT保持逐字段提交。未采用的对应实验分支已移除。
CPU回退：gs.Hybrid.FlightServiceFastPath、gs.Hybrid.CacheFlightLifecycleLookup、gs.Hybrid.YieldBetweenCaptureCuts、gs.Hybrid.FastPackCacheKey、gs.Hybrid.NativePeerSend、gs.Hybrid.NativePeerReceive、gs.Hybrid.EntityLayoutCache、gs.Hybrid.ReusePayloadCompressor；正式默认1。锁定项重开会话生效。
开局资源准备：gs.CombatEffects.PreloadImpacts=1、gs.CombatEffects.PrecompileDestruction=1；专用逻辑客户端不创建视觉表现。
引擎Slate补丁：Scripts/Performance/engine-slate-empty-operations.patch，应用于源码UE5.7的LaunchEngineLoop.cpp；Slate.SkipEmptyLocalPlayerOperations默认1，0回退。焦点、鼠标锁定、捕获、拖拽和导航照常处理。
计时脚本临时设t.IdleWhenNotForeground=0、Slate.bAllowThrottling=0；结束恢复，未将编辑器测试窗口条件设为全局生产配置。
响应诊断：python Scripts/Performance/diagnose_commander_response.py --output 新目录 --editor-log 当前编辑器日志 --members 24 --combat
诊断从真实控制器语义入口起算；--trace仅诊断用，默认关闭。它不能替代操作系统鼠标点击至显示器呈现的完整测量。
玩家在本图选择静止编队并右键移动，检查绿线与起步；连续改令、停止、混合选区、受击、死亡、出生、交接和会话退出后反馈须正确更新和清理。
绿线不伪造接受回执；60Hz逻辑推进、伤害、动画、相机、全部单位及弹丸均保留。地面Mass死亡爆炸按用户要求缩至1/3。
用户自行核验UI外观响应、四档模型及部件、描边、特效与移动观感。助手不截图读图或目检；技术与性能数据不能代替视觉验收。
正式配置与阶段证据：Progress/DevelopmentDocumentation/20261021-交火优化正式版本交付.md。
指令阶段诊断：Progress/DevelopmentDocumentation/20261021-本机指令交付与规划调度.md。"""

with unreal.ScopedEditorTransaction("Hybrid CPU review entry"):
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
    note.set_folder_path("Performance/HybridCPU20261020")
    note.set_actor_location(position, False, True)
    note.set_editor_property("text", instructions)

assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level(), "Map save failed"
assert note.get_editor_property("text") == instructions
assert note.actor_has_tag(tag) and note.get_editor_property("is_editor_only_actor")
assert all(abs(a-b) < 0.01 for a, b in zip(note.get_actor_location().to_tuple(), position.to_tuple()))
unreal.MCPythonHelper.submit_result(json.dumps({
    "success": True, "saved": True, "default_inactive": True,
    "world": world.get_path_name(),
    "related_entities": [{
        "label": label, "path": note.get_path_name(), "class": note.get_class().get_name(),
        "position_cm": list(note.get_actor_location().to_tuple()), "editor_only": True,
        "text": note.get_editor_property("text"),
    }],
}, ensure_ascii=False))
