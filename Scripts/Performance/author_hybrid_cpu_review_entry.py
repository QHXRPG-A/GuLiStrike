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
当前目标：指挥官从点击到绿线提交、全部受令本机Mass单位实际起步及模型提交以30ms为上限、向20ms优化，不增加网络延迟容差，且性能不退化。完整目标尚未通过。
迭代阶段只做必要响应分段测量；响应稳定达标后再执行下方完整配对性能复核。build-48单次指令24成员总耗时68.2ms；build-49合批接纳候选无Trace单轮总耗时48.681ms，仍未达标。候选不作为正式默认。
在项目根目录使用新输出目录：
python Scripts/Performance/capture_hybrid_cpu.py --output D:/UE5.7/PerformanceCaptures/GuLiStrike/ManualHybridCPUReview --variants standard_compression,candidate --rounds 3 --no-trace --editor-window-state minimized
初始600单位，固定原生交火输入；至少预热10秒，逻辑第3600步开始记录30秒；2359×1748视口、相机目标高度133.4米。
计时前最小化编辑器主窗口、激活游戏预览；逐帧核对两项窗口条件。计时中不切窗口、截图、枚举对象、移动镜头或改变负载。脚本结束后恢复开关并关闭所属会话。
正式快速压缩：gs.Hybrid.FastSmallPayloadCompression默认1；可靠消息正文不超过64KiB时使用无损低压缩级别，大正文保持标准级别；协议、校验、身份和队列字节预算保持。0为独立回退，在新会话生效并传递给逻辑客户端。
正式UI：gs.SceneUI.IndexedGeometry=1，gs.SceneUI.OptimizedPaint=1；分别置0用于回退。Slate.EnableGlobalInvalidation保持0。
四档：LOD0高度35–100米；LOD1为100–300米，仅施加资源LOD1精度上限；LOD2为300–700米综合精简；LOD3为总览。描边仅LOD0。gs.Commander.MeshLOD=0独立回退网格约束。
本机响应：立即意图绿线、纯兵种选区索引、60Hz手动移动与直接本机姿态、移动后同步发布、就绪路线索引及活动端点软避让均保持正式默认；完整成员/身份/权限校验和混合选区回退保留。
正式默认的结果应用保持常规顺序；弹丸发布保持完整编码；可靠包保持完整排序；VAT保持逐字段提交。
当前响应候选均默认0：gs.Hybrid.CommandApplicationWake按指令及其后续完整逻辑步就绪通知GT，gs.Commander.PresentationAfterCommands将原有单次模型更新放到PostUpdateWork之后。二者独立回退且须在新会话前设置；不加逻辑步、不重做整帧模型更新。
事务调度候选默认0：gs.Hybrid.NetworkRetryBatchLimit每阶段限制尝试数量，候选2；gs.Hybrid.DeferPeerTransactions将接收的事务计算放入独立的就绪队列，候选1。均在新会话前设置；新事实不等待失败重试时限，旧重试继续轮转，身份/权威/最终数值校验保持。
接纳候选gs.Commander.BatchMoveAdmissions默认0；候选1沿用每批最多25成员的移动组，一次接纳最多25人，手动类别单片最多0.5ms。世界每帧2ms、类别份额、查询数量及每帧128人接纳上限保持；0回退8人/0.2ms单片。单次StateTree处理仍可能超出时间片，性能收益待验证。
资源验证候选gs.Hybrid.ReuseResourceValidationSnapshot默认0；候选1在同一事务的验证副本内复用一次资源账本复制，仍逐段校验并保持原顺序。仅多段资源变更进入新路径；新会话生效，0独立回退。没有实施指令插队，仍待用户判断。
归属查询候选gs.Hybrid.DirectCaptureMembership默认0；1按完整对象身份查询飞行/特效快照归属，2同时执行两种路径、比较结果并保留原结果。未注册记录跟随来源的条件保持；不改变快照顺序。
build-54修复重复名单复制：单成员状态、交接及路由分组直接构造所需成员；UE逐对象执行只转换当前成员，协议层仍保留完整选区及原槽位。指令顺序、事件、权限代次、坐标和参数保持。既有协议验收与Editor编译通过，实际响应收益未重新测量。
CPU回退：gs.Hybrid.FlightServiceFastPath、gs.Hybrid.CacheFlightLifecycleLookup、gs.Hybrid.YieldBetweenCaptureCuts、gs.Hybrid.FastPackCacheKey、gs.Hybrid.NativePeerSend、gs.Hybrid.NativePeerReceive、gs.Hybrid.EntityLayoutCache、gs.Hybrid.ReusePayloadCompressor；正式默认1。锁定项重开会话生效。
开局资源准备：gs.CombatEffects.PreloadImpacts=1、gs.CombatEffects.PrecompileDestruction=1；专用逻辑客户端不创建视觉表现。
引擎Slate补丁：Scripts/Performance/engine-slate-empty-operations.patch，应用于源码UE5.7的LaunchEngineLoop.cpp；Slate.SkipEmptyLocalPlayerOperations默认1，0回退。焦点、鼠标锁定、捕获、拖拽和导航照常处理。
计时脚本临时设t.IdleWhenNotForeground=0、Slate.bAllowThrottling=0；结束恢复，未将编辑器测试窗口条件设为全局生产配置。
响应诊断：python Scripts/Performance/diagnose_commander_response.py --output 新目录 --editor-log 当前编辑器日志 --members 24 --combat
验证当前调序候选时追加：--command-wake 1 --late-presentation 1。基线为两项0。均保留1cm和2cm真实位移观察值，不能只选最快成员或改阈值宣布达标。
验证build-48事务调度候选再追加：--network-retry-batch 2 --defer-peer-transactions 1。汇报先给总耗时：从指令入口到全部成员模型提交的最晚时间；绿线、起步与模型都是累计值，不相加。保留1cm和2cm两种总耗时。
验证build-49合批接纳再追加：--batch-move-admissions 1。无Trace单轮24人全部20.087ms接纳，1cm/2cm总响应48.678/48.681ms。另轮带Trace及单兵日志为39.199/73.366ms，仅定位排队和提交时机，不做性能收益比较。
验证build-51副本复用再追加：--reuse-resource-validation-snapshot 1。带Trace单轮1cm/2cm总响应52.735/52.737ms，绿线21.309ms，全部24人首动37.701ms，仍未达30ms。主线程前置状态快照采集12.748ms；记录只定位后续优化方向，不证明性能收益。
验证build-53归属查询候选再追加：--direct-capture-membership 1。带Trace单轮24人1cm/2cm总响应47.758/47.761ms；短诊断内归属查询均值6.485→0.750ms，快照均值7.426→1.808ms，采集次数不同，不代表整帧配对收益。比较模式累计57118次结果相同，未覆盖所有生命周期边界。
诊断从真实控制器语义入口起算；--trace仅诊断用，默认关闭。它不能替代操作系统鼠标点击至显示器呈现的完整测量。
玩家在本图选择静止编队并右键移动，检查绿线与起步；连续改令、停止、混合选区、受击、死亡、出生、交接和会话退出后反馈须正确更新和清理。
名单修复手动复核：分别选择24人和更大编队下达移动、改令、停止，核验每个成员收到对应目标与槽位；编队完整性和逐成员回执保持。此次没有运行不同人数的计时曲线，也没有实施最高优先级插队。
绿线不伪造接受回执；60Hz逻辑推进、伤害、动画、相机、全部单位及弹丸均保留。地面Mass死亡爆炸按用户要求缩至1/3。
用户自行核验UI外观响应、四档模型及部件、描边、特效与移动观感。助手不截图读图或目检；技术与性能数据不能代替视觉验收。
正式配置与阶段证据：Progress/DevelopmentDocumentation/20261021-交火优化正式版本交付.md。
指令阶段诊断：Progress/DevelopmentDocumentation/20261021-本机指令交付与规划调度.md。
事务调度与最新总耗时：Progress/DevelopmentDocumentation/20261021-事务分批与本机响应.md。"""

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
