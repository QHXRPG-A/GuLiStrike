# 未通过候选回退与当前观测基线

按用户“找不到原因就回退”的决定，四项候选已实际移除：推进小组索引、StateTree 条件缓存、SceneUI 几何并行、服务器避障候选并行。包含开关关闭时仍执行的索引维护、几何命令准备、快照分批等公共改动，恢复实施前算法。2026-10-11 正式构建通过后，按用户要求清理了临时候选副本 `Before/` 和差异 `candidate-before.patch`；历史性能测量、原始 Trace 和最终报告保留，见 [清理清单](../../SessionCloseout20261011/cleanup-manifest.json)。

保留原网络 HUD、原生每秒网络采样、事件/姿态流计数、被动 CPU 计时，以及此前已采用的枪口/冲击批处理、飞行数据池、LOD、Bounds/屏外处理、避障查询优化/稀疏网格、SceneUI 投影缓存。正式预算、协议和可靠性不变。

## 回退验证

- `static-verification.json`：12 个受影响文件的算法与实施前 Git 版本一致，仅保留被动计时；16 个观测/配置文件哈希未变。条件缓存实现文件已移出 Source，相关调用和 CVar 已清除。
- `Build/verification.json`：沿用本任务明确授权，源码引擎 `D:/UnrealEngine-5.7`，`GuLiStrikeEditor Win64 Development` 构建退出 0，24 actions；引擎、项目、6 个插件共 8 份 BuildId 一致。`build.log` 为完整日志。最终 26 个源文件哈希对应本次构建。
- 原图同进程专服和双客户端，各 1280×720，每连接 250000 B/s；200/600 单位各预热 10 秒、采样 5 秒。采样期运行错误为 0，三 World 原生网络记录有效，移动单位分别为 149/477。200 单位每客户端收到 1764 条射击事件，1754 次真实枪口出生；压力枪口出生为 0，仍仅用于 CPU/网络诊断。
- 初始化阶段 200 单位记录 98 条 StateTree 根选择失败，压力为 50 条同类错误及一次飞行来源尚未就绪。此前 build4 压力基线也记录过 51–149 条初始化错误；不能据此证明本次 200 单位初始化错误无害或归因于回退。原日志完整保留，整体验证为 **partial**，不宣称完整行为通过。
- 两段短采样只确认集成路径与观测可用，不用于证明回退带来性能收益，也不替代正式 P95 对照。详见 `smoke-verification.json` 和 `Paired/*`。
- 原地图 `/Game/Maps/LVL_CommanderMassPrototype` 的 `PerfSnapshotNetwork_Entry` 说明已更新，三个相关对象保存并读回；入口和两个相机仍为 EditorOnly、默认不运行。见 `map-entry-readback.json` 和 [操作说明](../Map/README.md)。
- 临时控制项逐项恢复，正式配置哈希未变，PIE 已停止，本次启动的 Editor PID 50368 已退出。见 `Sessions/`、`editor-exit.json`、`final-checks.json`。

## CPU/GPU 与完整游戏线程账目

最近完整基线为 build4 四候选全关、六个 30 秒 A/A 窗口。按各窗口均值再取中位数：200 单位 GT 28.84 ms/GPU 11.54 ms；600 单位 GT 74.68 ms/GPU 23.57 ms。这是回退前全关基线，包含两臂共有重构，不冒充回退后测量。

回退后 5 秒短检查为 200 单位 GT 27.63 ms/GPU 13.45 ms，压力 GT 70.85 ms/GPU 26.21 ms。两者均显示游戏线程是当前主要限制；不同长度/阶段不可用于计算收益。

完整 GT 分账见 [游戏线程与 GPU 瓶颈](GameThread/README.md)。该账目从完整 GT 事件树计算每个 Scope 扣除直接子 Scope 后的自身耗时，再归并，逐帧核对总和；不能将 Native 子系统均值拼成一个总帧，也不能把任务容器的包含时间全部称为等待。`bottlenecks.json` 保留六轮 GPU 与原生热点数据，`GameThread/*/accounting.json` 保留两次完整 GT 分账。

压力 GPU 已归属的主要阶段是运动矢量约 4.79 ms、BasePass 4.17 ms、Slate UI 2.14 ms、CustomDepth 1.82 ms、ShadowDepths 1.81 ms；另约 2.57 ms 为 CSV Unaccounted。阶段计时不是互斥 GPU 利用率，异步/嵌套项不直接相加；尚未拆到具体网格或材质。压力存在零真实枪口，不能用于证明完整枪口表现成本。

## 使用和后续边界

`Scripts/Performance/run_snapshot_parallel_review.py` 现仅保留 `inspect`、`smoke`、`baseline`，输出转入本目录。旧候选参数被拒绝，避免对已移除的 CVar 做虚假 A/B；旧候选执行记录仅用于历史测量追溯，不直接在当前版本运行。

本轮停止四候选的 P95 因果实验，不宣称修复了全部 Slate/调度波动。网络积压仍是独立问题；协议整改和初始化 StateTree 异常另列后续，当前没有扩大修改范围。
