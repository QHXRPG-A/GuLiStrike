# Commander 客户端 GPU 消耗归因（2026-09-16）

## 结论

GPU 压力来自当前场景的完整渲染配置：**TSR、几何基础绘制／深度预处理、虚拟阴影，以及此前统计没有充分体现的异步 Lumen 和对比度自适应着色（软件 VRS）**。

双客每端绘制量几乎没有增加，但 GPUTime 由 5.65ms 增至 14.34～14.46ms，约 2.54～2.56 倍。结合两端选择同一显卡、同时不限帧运行，**共用 GPU 的资源竞争与调度影响是双客耗时放大的首要解释**。现有采样不能进一步区分着色器运算、显存带宽、跨进程调度和 GPU 时钟变化分别贡献多少。

另一个已定位的链路是：**GPU 遮挡查询结果尚未就绪 → 可见性工作线程等待 GPU → 渲染线程等待可见性任务**。原先约 8.5ms 的 Visibility 等待，主要属于这条链路，不是等量的可见性 CPU 运算。

本轮完成归因分析。没有运行优化 A/B，也没有可宣称的优化收益。

## 1. 数据与统计口径

- 基线：[2026-09-16 CPU 复测](../CommanderClientCpu-Retest-20260916/REPORT.md)的九份客户端 `engine.csv`；单客三轮，双客三轮、每轮两份。
- 环境：源码 UE5.7、Windows、Development Editor `-game`，地图 `/Game/Maps/LVL_CommanderMassPrototype`；非 Cook／Shipping。
- 负载：1200 单位持续折返，固定镜头；1920×1080、Epic、屏幕百分比 100%、关闭 VSync／动态分辨率、离屏渲染；客户端不限帧，采集时编辑器关闭。
- 日志选择 NVIDIA GeForce RTX 4090、D3D12 Adapter 0。`LogInit` 中的虚拟显示适配器名称不是实际选用的 D3D12 GPU。
- 下表使用每份完整约 40 秒引擎 CSV 的平均值，再取三轮中位数；与用户提供的 `engine_full_capture` 口径一致。不是共同 30 秒整帧统计，也不是逐帧中位数。
- 额外解析第一轮单客与第一轮双客 client1 的原始 trace，使用原共同 30 秒窗口。分清 `GPU0-Graphics0`、`GPU0-Compute0` 与 CPU 线程。
- 当前编辑器只读场景审计属于辅助证据，不是原采样时逐帧的场景快照。基线清单里的 30 份源码／配置／脚本文件 SHA-256 全部一致。

## 2. GPU 绘制分项

单位：ms。双客两列都是各客户端自身测得的值。

| 指标 | 单客 | 双客 C1 | 双客 C2 |
|---|---:|---:|---:|
| GPUTime | 5.653 | 14.456 | 14.343 |
| TSR | 0.677 | 1.759 | 1.739 |
| ShadowDepths 阴影深度 | 0.680 | 1.722 | 1.714 |
| BasePass 基础绘制 | 0.680 | 1.677 | 1.662 |
| Prepass 深度预处理 | 0.542 | 1.233 | 1.233 |
| LumenReflections 反射 | 0.339 | 0.793 | 0.797 |
| VolumetricFog 体积雾 | 0.249 | 0.713 | 0.710 |
| Postprocessing | 0.255 | 0.653 | 0.657 |
| NaniteVisBuffer | 0.240 | 0.591 | 0.564 |
| ShadowProjection 阴影投射 | 0.209 | 0.508 | 0.494 |
| Unaccounted 未归类 | 0.659 | 1.497 | 1.523 |

**这些计时不可直接相加构造关键路径，也不能作为关闭某功能后的帧时间收益。** CSV 存在未归类时间，异步计算还会与图形队列重叠。

### TSR 为何值得先做对照

原采样日志明确记录 `r.TSR.History.ScreenPercentage=200`、`r.TSR.History.UpdateQuality=3`。在 1080p 输出下，历史缓冲的宽高各为 2 倍，即约 3840×2160、4 倍像素数；不是整个场景都在 4K 绘制，也不是 TSR 总耗时必定增加 4 倍。

当前输入分辨率已经是 1080p、100%，同时仍支付 TSR 的时域抗锯齿与历史更新成本。把历史比例单独改为 100 做 A/B，比直接降低全部画质更容易测出这项配置的收益和画质代价。需要检查远处机械细线、移动闪烁与拖影。[Epic UE5.7 TSR 说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-in-unreal-engine?application_version=5.7)

## 3. 原热点列表遗漏的异步计算

以下是第一轮各 30 秒 trace 的 **Compute 队列事件平均跨度**，不是上表的三轮中位数，也不是着色器独占执行时间。

| GPU0-Compute0 事件 | 单客 ms／次 | 双客 C1 ms／次 |
|---|---:|---:|
| LumenScreenProbeGather | 1.513 | 4.027 |
| ContrastAdaptiveShading | 1.019 | 2.693 |
| TemporalSuperResolution | 0.428 | 1.070 |

### Lumen

CSV 中 `GPU/LumenScreenProbeGather` 只有约 0.035／0.101ms，**这个计数器不足以代表其异步队列工作**。原始 trace 确认 Compute 队列每帧有明显的 Screen Probe Gather 事件，即屏幕探针的间接光照收集。

这些跨度受到其他并行工作、依赖和竞争影响，不能说“关掉 Lumen 一定省 4ms”。Epic 也说明异步运行会干扰单项计时；需要临时关闭 Lumen 异步计算做归因测量，之后恢复原配置，再判断实际优化收益。[Epic UE5.7 Lumen 性能指南](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine?application_version=5.7)

### 软件 VRS／对比度自适应着色

这项功能根据上一帧亮度，生成可变着色率图，目的是减少后续着色工作。原采样日志记录：

```ini
r.VRS.EnableSoftware=1
r.VRS.ContrastAdaptiveShading=1
```

来源是本机源码引擎 `Engine/Config/Windows/BaseWindowsEngine.ini:27–28`。Compute trace 中存在 `PrepareImageBasedVRS → ContrastAdaptiveShading`，因此这是实际进入渲染链路的工作。

它有自身开销，也可能节省 Nanite 着色成本。**只能把它列为需要验证净收益的候选，不能把 2.69ms 直接认定为浪费。** 建议临时 `r.VRS.ContrastAdaptiveShading 0`，比较总 GPUTime、Nanite 着色和整帧，而不是只观察该事件消失。

## 4. Visibility 等待已经定位

原始 trace 给出如下嵌套调用：

```text
工作线程：OcclusionCullPipe
  → GPUBound_WaitingForGPUForOcclusionQueries_SeeGPUTrack
    → SyncPoint_Wait
      → FTaskBase::WaitImpl_StateChangeEvent_WaitFor

渲染线程：WaitForVisibilityTasks
  → 等待上述可见性任务流水完成
```

| 首轮共同 30 秒窗口 | 单客 | 双客 C1 |
|---|---:|---:|
| 渲染线程 Visibility 等待，平均 ms／次 | 1.846 | 8.310 |
| 工作线程 GPU 遮挡查询等待，平均 ms／次 | 2.069 | 8.528 |
| Visibility 等待时间与 GPU 查询等待的时间重叠率 | 90.75% | 97.58% |

重叠率是时间相关证据；源码补足了依赖关系：

- `SceneVisibility.cpp:3262–3272`：`FGPUOcclusion::WaitForLastOcclusionQuery()` 以 `bWait=true` 调用 `RHIGetRenderQueryResult`。
- `SceneVisibility.cpp:3359`：遮挡工作流水调用该等待。
- `SceneVisibility.cpp:4838–4842`：渲染线程在 `WaitForVisibilityTasks` 等待 DynamicMeshElementsPipe，并设置 CSV 的 `Visibility` 等待标签。

因此，优化 GPU 可能同时缩短这部分渲染线程等待。仍不能保证帧率按 GPU 节省量等比例提升：单客渲染线程还有其他工作和等待；这两条线程上的等待也不能重复相加。

## 5. 场景与绘制负载核查

### 双客没有让每端多画一倍东西

| CSV 计数器（三轮均值中位数） | 单客 | 双客 C1 | 双客 C2 |
|---|---:|---:|---:|
| RHI DrawCalls／帧 | 611.94 | 611.21 | 611.21 |
| RHI PrimitivesDrawn／帧 | 2,865,432 | 2,865,432 | 2,865,432 |
| GPUSceneInstanceCount | 10,716 | 10,716 | 10,716 |
| LocalUsedMB | 5,731.70 | 5,756.38 | 5,733.55 |
| LocalBudgetMB | 23,370 | 23,370 | 23,370 |

绘制图元数包含多个 pass，并非独立模型三角形数；GPUScene 实例数也包含整个场景与实例池，不等于士兵人数。当前数据不支持“每端渲染对象激增”或“已确认显存超预算”这两种解释。显存计数也不能替代全机驻留／换页诊断。

### 阴影优先检查地形与环境

- `DefaultGame.ini:114–117` 以及 `GuLiCommanderPresentationActor.cpp:372–375` 明确关闭士兵投影、距离场光照影响、动态间接光照影响及光追可见性。环与血条也关闭投影。
- 当前关卡有一个投影的 Movable DirectionalLight；SkyLight 不投影，实时捕获开启；云阴影关闭。
- 当前 Landscape 有 **1024 个可见、可投影的 LandscapeComponent**，`enable_nanite=false`；XY 范围约 **8.16×8.16km**。另有 3 个投影的圆柱静态网格。运行时建筑／车辆不包含在这个无 PIE 审计中。
- 因此不能把 ShadowDepths 的成本直接归为“1200 士兵各画一次动态阴影”。**地形、环境和运行时建筑是下一步逐对象归因的候选**。组件数量本身不是成本证明，更不能推导每帧全部重画。

### 基础绘制／深度预处理

- 已知基础绘制与深度预处理都有持续成本，但这轮没有逐 draw 的网格／材质归因，无法判定地形、建筑与士兵各占多少。
- 当前默认士兵网格有三个 LOD：**18,999／964／238 三角形，各 1 个 section**；材质为单面 Opaque、Default Lit、3 张纹理，不是全员使用多材质槽的原始复杂模型。
- 还需要实际可见实例数、LOD 分布、屏幕覆盖面积和逐 draw 证据；不能用 `1200 × LOD0` 估算本轮绘制量。
- 当前体积雾距离为 300,000cm；CSV 中体积雾有约 0.25／0.71ms 成本，可作为后续画质预算项。
- 本轮是移动压测，不代表密集战斗、全选血条或大量技能特效；不能据此为这些场景的 GPU 表现下结论。

## 6. 下一步修复顺序与验证

先做每次只改一个配置的临时对照。保持原镜头、人数、分辨率、同步方式和采集条件；恢复对应基线后再切下一项。

| 顺序 | 实验 | 要回答的问题 |
|---|---|---|
| 1 | TSR 历史比例 200 → 100 | 高分辨率历史的画质收益是否值得其计算和带宽开销？ |
| 2 | `r.VRS.ContrastAdaptiveShading 0` | 当前场景软件 VRS 的总收益能否覆盖生成着色率图的开销？ |
| 3 | 临时 `r.Lumen.AsyncCompute 0` 做归因，测后恢复 | 分清 Lumen 自身工作与并行重叠；随后独立比较 Lumen GI／反射预算。关闭异步本身不是优化结论。 |
| 4 | 固定镜头隔离地形、环境／建筑、士兵；查看 ShadowDepths、BasePass、Prepass | 找出真正昂贵的对象和 pass，再决定阴影范围、LOD、材质与几何方案。 |
| 5 | 独立调整体积雾／其他后处理 | 验证在目标俯视距离下的视觉收益与性能代价。 |

每项看 GPUTime、整帧 P95、RenderThreadTime、Visibility 等待，以及对应 pass。单客先辨别本地收益，双客验证竞争下的收益；有候选方案后用原单客／双客各三轮复测。最终保留优化前后同镜头画面对比。

既有预算是单客整帧 P95≤16.67ms、双客≤33.33ms。本轮没有重新约定更严格 GPU 预算；不能把通过旧整帧门槛当成 GPU 已经优化完成。

## 7. 产物与复核

- [九份原始 CSV 的完整分项汇总](gpu-baseline.json) · [分析脚本](analyze_baseline.py)
- [两份 trace 的队列与等待分析](trace-findings.json) · [解析脚本](analyze_traces.py) · [导出脚本](export_waits.py)
- [场景／网格／材质审计](scene-audit.json) · [地形与雾审计](landscape-fog-audit.json) · [当前编辑器 VRS 参数](editor-vrs.json)
- [只读编辑器审计脚本](../../Scripts/diagnosis/inspect_commander_gpu_scene.py)
- [九份日志配置与清单复核](validation.json) · [结束时编辑器状态](editor-after.json)
- 原始导出保留在本目录两个 `retest-*` 子目录，源 trace 和 CPU 复测结果未覆盖。

分析脚本逐份断言 GPUTime、GameThreadTime、RenderThreadTime 与既有 `comparison.json` 一致。30 份清单文件哈希一致，编辑器审计前后没有脏关卡／资产包。本轮新增分析文件，未实施生产代码、渲染配置或资产修改。
