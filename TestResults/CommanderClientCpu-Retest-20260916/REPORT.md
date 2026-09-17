# 客户端 CPU 优化修复后复测（2026-09-16）

## 本次结论

本次独立重新运行既有 1200 单位单客／双客各三轮，共九份客户端采样。

**本轮固定镜头CPU性能验收通过。**

| 验收项 | 结果 |
|---|---|
| 单客／双客各三轮，九份客户端采样完整 | 通过 |
| 存活1200，实际移动人数P05≥1140 | 通过 |
| 单客P95≤16.67ms，双客P95≤33.33ms | 通过 |
| 服务端丢步为0 | 通过 |
| 零移动采样为0 | 通过 |
| 静态名册没有完整实例池校准 | 通过 |
| 空闲血条没有逐兵实例更新 | 通过 |
| Mass位置采样不超过10Hz | 通过 |
| 小地图采样不超过10Hz | 通过 |

## 帧时间与实际移动负载

以下为每组各轮结果的中位数；P95/P99 先在各轮共同 30 秒窗口计算，再取三轮中位数。

| 场景 | 平均 ms | P95 ms | P99 ms | 移动人数 P05 范围 | P95 门槛 ms |
|---|---:|---:|---:|---:|---:|
| 1客 client1 | 9.553 | 10.897 | 11.475 | 1198–1200 | 16.67 |
| 2客 client1 | 16.320 | 18.450 | 19.323 | 1199–1199 | 33.33 |
| 2客 client2 | 16.426 | 18.734 | 19.633 | 1199–1200 | 33.33 |

| 轮次 | 客户端 | 平均 ms | P95 ms | P99 ms | 最大 ms | 移动 P05 | 负载／帧预算 |
|---|---|---:|---:|---:|---:|---:|---|
| retest-1-n1200-c1 | client1 | 9.467 | 10.767 | 11.212 | 12.943 | 1200 | True / True |
| retest-2-n1200-c1 | client1 | 9.558 | 10.936 | 11.741 | 16.433 | 1199 | True / True |
| retest-3-n1200-c1 | client1 | 9.553 | 10.897 | 11.475 | 14.055 | 1198 | True / True |
| retest-1-n1200-c2 | client1 | 16.320 | 18.450 | 19.323 | 20.922 | 1199 | True / True |
| retest-1-n1200-c2 | client2 | 16.616 | 18.945 | 19.906 | 20.945 | 1200 | True / True |
| retest-2-n1200-c2 | client1 | 16.141 | 18.347 | 19.185 | 23.491 | 1199 | True / True |
| retest-2-n1200-c2 | client2 | 16.426 | 18.734 | 19.633 | 21.098 | 1199 | True / True |
| retest-3-n1200-c2 | client1 | 16.510 | 18.750 | 19.666 | 20.671 | 1199 | True / True |
| retest-3-n1200-c2 | client2 | 16.217 | 18.309 | 19.140 | 20.858 | 1200 | True / True |

## 服务端与先前失败原因

先前 `NavigationBuild` 耗时的具体归因由据点／Detour 修复任务定位为 CrowdManager proximity 查询；不能把该顶层计时名称直接解释为 NavMesh 重建。

| 轮次 | 模拟步数（完整50秒） | 丢步 | CrowdManager 平均 ms | P95 ms | 峰值 ms |
|---|---:|---:|---:|---:|---:|
| retest-1-n1200-c1 | 500 | 0 | 1.814 | 2.065 | 2.490 |
| retest-2-n1200-c1 | 500 | 0 | 1.830 | 2.189 | 3.548 |
| retest-3-n1200-c1 | 500 | 0 | 1.818 | 2.121 | 2.611 |
| retest-1-n1200-c2 | 500 | 0 | 1.823 | 2.257 | 3.132 |
| retest-2-n1200-c2 | 500 | 0 | 1.837 | 2.263 | 3.260 |
| retest-3-n1200-c2 | 500 | 0 | 1.864 | 2.249 | 3.268 |

## CPU 路径与调用次数

同一共同窗口的 GameThread trace；已剔除导出器重复带入的 GPU 行。以下耗时是 inclusive ms/帧，父子范围不得相加。零次数表示窗口内无该计时事件。

| Scope | 九份窗口次数范围（每30秒） | 单客 | 双客C1 | 双客C2 |
|---|---:|---:|---:|---:|
| `GuLiCommanderRoster_ApplyDelta` | 35–36 | 0.0004 | 0.0008 | 0.0008 |
| `GuLiCommanderPresentation_StateChanges` | 0–0 | 0.0000 | 0.0000 | 0.0000 |
| `GuLiCommanderPresentation_PoolReconcile` | 0–0 | 0.0000 | 0.0000 | 0.0000 |
| `GuLiCommanderPresentation_PoolDelta` | 0–0 | 0.0000 | 0.0000 | 0.0000 |
| `GuLiCommanderPresentationActor` | 1806–3169 | 0.6107 | 0.6611 | 0.6621 |
| `GuLiCommanderPresentation_Interpolation` | 1806–3169 | 0.3596 | 0.3724 | 0.3714 |
| `GuLiCommanderPresentation_InstanceSubmit` | 1806–3169 | 0.2002 | 0.2102 | 0.2097 |
| `GuLiCommanderPresentation_MassPositionSample` | 300–300 | 0.0098 | 0.0177 | 0.0181 |
| `GuLiCommanderPresentation_MassBatchSubmit` | 300–300 | 0.0098 | 0.0176 | 0.0181 |
| `GuLiCommanderPresentation_MassBatchExecute` | 300–300 | 0.0132 | 0.0237 | 0.0242 |
| `MassDeferredCommand_Execute` | 300–300 | 0.0133 | 0.0238 | 0.0243 |
| `GuLiCommanderMiniMap_Snapshot` | 300–300 | 0.0158 | 0.0293 | 0.0296 |
| `GuLiCommanderMiniMap_TerrainPaint` | 0–0 | 0.0000 | 0.0000 | 0.0000 |
| `GuLiCommanderMiniMap_DynamicPaint` | 300–300 | 0.0317 | 0.0592 | 0.0589 |
| `GuLiCommanderHealthBars_UpdateInstances` | 0–0 | 0.0000 | 0.0000 | 0.0000 |
| `GuLiCommanderHealthBars_MaintainActivity` | 300–300 | 0.0007 | 0.0013 | 0.0013 |
| `DeferredRenderUpdates_GameThread` | 1806–3170 | 0.4195 | 0.4390 | 0.4393 |
| `Recreate` | 103–136 | 0.0008 | 0.0014 | 0.0014 |

实例池的 `RebuildLocalInstances` 是现有逐帧表现函数名，不等于完整池校准或渲染代理重建；完整池校准单独看 `PoolReconcile`。全场景 `Recreate` 的残余次数不能归为移动士兵，不能宣称场景更新已全部消失。

## 缓存与进程资源

| 明确计数的容器 | 跨轮高水位 bytes |
|---|---:|
| `GuLiCommanderHealthBars/DataCacheBytes` | 64 |
| `GuLiCommanderMiniMap/DataCacheBytes` | 69680 |
| `GuLiCommanderPresentation/InstanceAndMirrorCacheBytes` | 621468 |
| `GuLiCommanderPresentation/RosterCacheBytes` | 202188 |

各容器跨轮高水位相加 872.46 KiB；这不是同一瞬时峰值，也不是净新增内存。不含全部引擎、Slate、资源及 GPU 缓存。

进程资源取共同窗口约1Hz采样；每格为三轮平均值的中位数／三轮P95的中位数／三轮最大峰值。CPU单位为占用的逻辑核心等效数。

| 场景 | CPU核心（均／P95／峰） | 工作集MiB（均／P95／峰） | PrivateMiB（均／P95／峰） |
|---|---:|---:|---:|
| 1客 client1 | 5.84 / 6.94 / 7.54 | 5925.00 / 5928.49 / 5967.96 | 11573.49 / 11580.30 / 11581.94 |
| 2客 client1 | 3.61 / 4.55 / 6.45 | 5832.56 / 5834.48 / 5935.32 | 11597.84 / 11599.59 / 11634.07 |
| 2客 client2 | 3.62 / 4.62 / 5.44 | 5929.08 / 5932.03 / 5989.38 | 11567.39 / 11574.80 / 11633.60 |

## 采集条件与验证边界

- 同机源码引擎 `D:/UnrealEngine-5.7`，地图 `/Game/Maps/LVL_CommanderMassPrototype`；Editor运行时 `-game`，非Cook／Shipping。
- 1200单位，join-first分批生成，8秒折返；服务端50秒、客户端40秒，每轮使用各角色同时覆盖的共同QPC 30秒窗口。
- 1920×1080、Epic、屏幕百分比100%、VSync关闭、动态分辨率关闭、原固定镜头；D3D12离屏渲染、客户端不限帧、服务端60FPS上限。
- 延续原正式CPU测试的临时 `MaxAgents=2048` 参数；项目配置与原始压测脚本未改。开启同样的CSV、CPU/GPU trace与网络采集。
- 本次六轮采集期间编辑器关闭，没有并发构建、其他回归或trace导出。旧CPU基线有后台编辑器且实际移动负载不足；本次结果不能用来计算五项CPU改动的独立收益百分比。
- 九份trace在全部采集结束后顺序导出。只读Insights分析器来自Launcher安装；构建、自动化与六轮运行均使用源码引擎。
- 本轮仅复测，未修改生产源码。完整条件见 [conditions.json](conditions.json)，前后源码与模块核对见 [final-validation.json](final-validation.json)。
- 本轮重新运行原有42项自动化；此前鼠标／血条／小地图人工回归证据沿用[原CPU实施报告](../CommanderClientCpu-20260915/REPORT.md)，本轮未重新执行人工交互。旧鼠标脚本仍要求已关闭的预测事件，该判据的适用性限制保留。
- 结论限定为上述固定镜头短窗口场景。双客P95满足33.33ms门槛，但未达到16.67ms；未扩展到全军同屏、全选血条、长时间操作、打包或公网场景。

## 构建与回归

- `GuLiStrikeEditor Win64 Development` 和 `GuLiStrike Win64 Development`：源码Build.bat退出码均为0，本次均为Up to date。
- 引擎、项目和五个涉及插件的七份模块清单BuildId一致：`11b8900e-cd30-41cb-ad6b-96933e5d3584`。
- 原有网络、表现、血条、小地图回归：42成功，0失败，0未运行。
- 30份相关文件的采集前后SHA-256一致；15份运行日志未检出Fatal、Assertion、Ensure或bunch超限标记。

## 收尾

- 编辑器已恢复至原 `/Game/Maps/LVL_CommanderMassPrototype` 地图，没有运行PIE，脏关卡／资产包为空，见 [恢复状态](editor-after.json)。
- CPU开发记录已更新为done/passed；更广的客户端性能需求保留partial。新增复测归档，不覆盖原未通过记录。
- Progress索引构建成功，全量检查0错误、12条既有文档尺寸／任务数提示，见 [构建日志](progress-build.log) 与 [检查日志](progress-check.log)。关联需求已超过20KB，后续可按CPU、同步、显示和资源归因边界拆分，本次未拆分。

## 证据

- [每轮共同窗口负载与服务端结果](movement-results.json)
- [九份客户端CPU、内存与trace统计](comparison.json)
- [汇总与机制门槛](results-summary.json) · [整帧CSV](frames-comparison.csv)
- [Editor构建](editor-build.log) · [Game构建](game-build.log) · [自动化报告](Automation/index.json)
- [模块清单](buildids.json) · [源码清单](source-manifest.json) · [运行日志标记检查](runtime-error-markers.json)
- [既有场景采集脚本副本](run_capture.ps1) · [负载分析](analyze_movement.py) · [CPU分析](analyze_cpu.py) · [汇总脚本](summarize_results.py)
- [Detour与坡道修复记录](../../Progress/Archive/20260916-据点矿厂落点与Detour避让卡顿修复.md)
- [CPU开发记录](../../Progress/DevelopmentDocumentation/20260915-客户端CPU增量维护与10Hz刷新.md)

