# GuLi Frame Profiler

UE 5.7 / Win64 编辑器插件，项目启动自动加载，默认快捷键 **Alt+/**。

按一次后采集 **5 秒 CPU Trace**，用原生 TraceServices 在后台生成报告。以 `-AttachRenderDoc` 启动编辑器时，CPU 窗口结束后另行截取当前视口。采样期间可以正常操作；重复按键不会建立第二个任务。截帧停顿不进入 CPU 窗口，但 RenderDoc 常驻钩子的成本仍会进入，所以性能验收使用普通启动。

## 入口与文件

- 编辑器 **Tools → GuLi Frame Profiler → Capture and Analyze**，或 `Alt+/`。普通启动生成完整 CPU 报告；GPU 调试启动同时生成 RenderDoc 截帧。
- `gs.FrameProfiler.CaptureCPU` 使用相同的 5 秒 CPU 分析路径，明确跳过 RenderDoc 截帧，适合反复定位热点。
- **Editor Preferences → Keyboard Shortcuts → GuLi Frame Profiler** 可修改快捷键。文本输入框与模态弹窗不触发快捷键。
- 默认目录：项目父目录下 `PerformanceCaptures/GuLiStrike/<时间戳-编号>/`，当前项目对应 `D:/UE5.7/PerformanceCaptures/GuLiStrike/`。
- 每次保存 `cpu.utrace`、`frame.rdc`（截帧成功时）、`context.json`、`report.json`、`report.md`。Slate 归属直接写入上下文；不生成事件 CSV。
- 完成后自动打开报告面板，按钮可打开目录、RenderDoc、Unreal Insights。
- 当前捕获状态见输出根目录 `latest-status.json`。它是状态指针，不替代每次采集的独立证据。

## 统计含义

- 只采集当前编辑器/渲染客户端进程。独立逻辑客户端、Coordinator 的耗时不合并进来；它们对同机资源的竞争仍会反映在当前进程表现中。
- 引擎帧间隔与完整 `FEngineLoop::Tick` 范围分别统计。平均、P95、峰值包含完整窗口内的零活动帧。
- CPU scope 时间包含显式等待，报告将等待/同步单列；该数据不等同于操作系统 CPU 利用率。
- 自身耗时扣除嵌套子范围；包含耗时用于查看调用树。Hybrid 及各功能范围按祖先区间并集计算，不重复相加。不同功能范围可能互相包含，不能把表中各行再求和。
- 完整 Hybrid 沿用分析口径 v5，纳入预测避障处理器根范围和外移主线程工作。后台线程另报，不冒充主线程成本。
- 分析 v2 / Slate 归属 v4 支持嵌入和独立窗口的 PIE。先缓存游戏视口、游戏 UI 层及编辑器子树身份；每帧沿已缓存的弱父链检查身份、重新挂接和切换控件的活动子项，避免反复进行全窗口布局搜索。只有已确认的编辑器独占区间才扣除，混合祖先、共享及未知组件仍计入客户端上界。生命周期或归属校验失败时不给出达标结论。
- 报告区分“已测量”“无活动”“不适用”“未就绪”和“缺少埋点”。编辑器采样没有运行玩法，Hybrid 等项目显示“不适用”；PIE 仅在原生埋点覆盖和运行就绪均已确认时，才把没有调用记为零。后台寻路及快照工作另报，不能当作主线程成本。
- 上下文记录采样期间的前台/后台帧数。后台限速或绘制暂停可能改变帧时，不能与前台游玩成绩直接比较。
- 5 秒采样适合定位当前热点，不替代严格配对的优化验收。

## 前置与异常

- 项目默认 `renderdoc.AutoAttach=False`，避免 RenderDoc 1.46 的 DLL 加载钩子在 Windows 消息轮询中反复扫描模块。该诊断开销不属于玩法或画质；普通启动保留 CPU 分析。
- GPU 截帧需先保存并关闭当前编辑器，再使用 `D:/UnrealEngine-5.7/Engine/Binaries/Win64/UnrealEditor.exe D:/UE5.7/test1/GuLiStrike.uproject -AttachRenderDoc` 启动。日志应出现 `RenderDoc plugin is ready!`，Alt+/ 随后自动使用 CPU 加 GPU 路径；退出后正常打开项目即可回到性能模式。保留既有安装目录，不卸载 RenderDoc。
- 附加成功后，插件默认关闭常驻的详细绘制诊断标记，保留普通 RDG 配置；仅在 CPU 窗口结束后的截帧阶段临时开启详细材质/通道名称，渲染命令完成后恢复原值与设置优先级。上下文逐项记录五项 CVar 的恢复结果；恢复失败将标记报告不完整。此策略不改变画质。初始化前设置 `gs.FrameProfiler.KeepDetailedGPUEvents=1` 可保留引擎的常驻标记策略。
- 已有其他 Trace、原生性能捕获或 Slate 窗口归属捕获时，本插件拒绝启动，不停止对方任务。
- 普通模式明确记录 `renderdoc_requested=false` 和 `renderdoc_attached_at_start=false`，没有 `.rdc` 属于预期。已请求 GPU 截帧但失败时仍保留 CPU 报告并标记不完整。Windows 开发者模式不影响 CPU Trace 分析，本插件不修改系统设置。
- 提前结束 PIE 或取消采样保留不完整证据，不登记为完整 5 秒采样。
- Trace 写入连接尚在建立时，引擎可能暂时拒绝停止。插件保留本次所有权并重试，停止成功且文件关闭后才进入分析；不会提前返回待机并遗留文件录制。退出插件时也处理仍在建立的自有连接。
- 不修改玩法、画质、模拟归属或网络配置，不自动开始 PIE，不进入 Game/Server/Shipping 目标。

## 开发入口

```text
gs.FrameProfiler.Capture
gs.FrameProfiler.CaptureCPU
gs.FrameProfiler.Status
gs.FrameProfiler.Cancel
gs.FrameProfiler.MeasureIdle 5 label
```

插件只有引擎依赖。项目模块通过 `IGuLiFrameProfilerContext` 注册开始/结束采样适配器，复用原生 Hybrid 负载、队列及 Slate 归属统计；该接口不提供玩法 Blueprint 入口。

`MeasureIdle` 位于项目 Editor 模块，插件卸载后仍可测量。它关闭工具报告面板，使用原生帧间隔和进程 CPU 时间计数，不录制 Trace、不安装 Slate 归属回调、不截帧；结束后一次写入 `idle-overhead/` 下的独立 JSON。用于对照常驻开销，记录模块加载、RenderDoc DLL、内存 Trace 通道、前台帧数、视口和前后实际模拟单位组成。没有文件录制不表示内存 Trace 通道已关闭；常规捕获会恢复触发前的通道状态。

离线分析附带 `game_thread_frame_gaps`，按游戏线程 Frame 内相邻事件边界汇总未细分时间。它包含等待和线程未被调度的时间，须结合源码或调用栈定位，不能直接视为某个函数的 CPU 工作量。

TraceServices 分析会话在主线程创建，以满足 ChaosVD 等编辑器提供器的初始化约束；Trace 解析、统计和报告文件输出继续在后台执行。

2026-10-20 最终代码已通过源码版 UE5.7 Development Editor 编译并重启加载。600 单位、32 建筑、1600×1104、原窗口布局下的两次完整前台 CPU 报告为 13.567ms 和 13.586ms，P95 为 19.320ms / 19.233ms；普通无文件 Trace 窗口 13.076ms。平均 15ms 目标通过，不表示每帧都低于 15ms。最终 GPU 回归已生成可打开的 RDC，五项 CVar 值及优先级全部恢复。实施及玩家复核状态见 [15ms 优化记录](../../Progress/DevelopmentDocumentation/20261020-保持玩法与表现的15ms帧时间优化.md)。
