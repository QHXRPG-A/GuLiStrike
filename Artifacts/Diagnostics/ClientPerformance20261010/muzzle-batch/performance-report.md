# 枪口批量化运行与截帧结果

同一源码编辑器进程包含一专服和双客户端。整帧/FPS 是该进程的共同帧率，不等于独立 Client1 成本。

两种场景各三轮模式0/1/2，热身10秒、记录30秒；移动、避障及双方正常开火保留。测量窗口内没有截图、对象盘点或资源写入。

| 场景 | 模式 | 整帧平均 ms | 三轮P95均值 ms | FPS | GT均值 ms | GPU均值 ms |
|---|---|---:|---:|---:|---:|---:|
| dense200 | 0 | 37.89 | 62.70 | 26.39 | 37.87 | 12.77 |
| dense200 | 1 | 38.87 | 63.37 | 25.73 | 38.83 | 12.79 |
| dense200 | 2 | 29.43 | 35.21 | 33.98 | 29.42 | 12.26 |
| stress | 0 | 78.63 | 100.29 | 12.72 | 78.62 | 26.70 |
| stress | 1 | 82.48 | 111.61 | 12.12 | 82.47 | 26.76 |
| stress | 2 | 77.44 | 94.32 | 12.91 | 77.43 | 28.08 |

## dense200

- mode0_to_2: 帧耗时减少 22.3%，FPS增加 28.7%；枪口收益已证实。
- mode1_to_2: 帧耗时减少 24.3%，FPS增加 32.1%；枪口收益已证实。

## stress

- mode0_to_2: 帧耗时减少 1.5%，FPS增加 1.5%；枪口收益未证实。
- mode1_to_2: 帧耗时减少 6.1%，FPS增加 6.5%；枪口收益未证实。

压力场景真实枪口接纳/出生为零：可靠弹丸事件排队导致射击到达时过期。250000 B/s 是每客户端配置预算，500000 B/s 是总预算，不是任务管理器实测；提高临时预算到1 MB/s仍未解除积压。发送端每次最多8包，单包仍最多16条/1000字节。本轮保留正式网络配置；预览枪口独立资源单列，不能算作真实反馈收益。

## 逐份负载与枪口证据

| 样本 | 前/后移动状态 | >10cm净位移单位 | 双客户端实际出生 | 末端枪口组件 | GT枪口资源自身 ms/帧 | 每千次出生 GT资源自身 ms（近似） |
|---|---:|---:|---:|---:|---:|---:|
| dense200-r1-mode0 | 200/187 | 64 | 16320 | 247/255 | 3.082 | 153.53 |
| dense200-r1-mode1 | 200/183 | 68 | 15840 | 232/238 | 3.063 | 157.04 |
| dense200-r1-mode2 | 200/183 | 68 | 16320 | 1/1 | 0.132 | 8.24 |
| dense200-r2-mode0 | 200/184 | 69 | 16200 | 232/229 | 3.259 | 153.71 |
| dense200-r2-mode1 | 200/184 | 72 | 15840 | 206/203 | 3.596 | 161.18 |
| dense200-r2-mode2 | 200/186 | 68 | 15840 | 1/1 | 0.122 | 8.16 |
| dense200-r3-mode0 | 200/185 | 68 | 16104 | 228/219 | 3.068 | 151.65 |
| dense200-r3-mode1 | 200/181 | 68 | 15840 | 237/234 | 3.071 | 154.52 |
| dense200-r3-mode2 | 200/161 | 158 | 18968 | 2/2 | 0.159 | 8.17 |
| stress-r1-mode0 | 596/575 | 477 | 0 | 0/0 | 0.000 | N/A |
| stress-r1-mode1 | 594/576 | 476 | 0 | 0/0 | 0.000 | N/A |
| stress-r1-mode2 | 593/585 | 477 | 0 | 0/0 | 0.000 | N/A |
| stress-r2-mode0 | 590/572 | 477 | 0 | 0/0 | 0.000 | N/A |
| stress-r2-mode1 | 594/574 | 478 | 0 | 0/0 | 0.000 | N/A |
| stress-r2-mode2 | 592/576 | 476 | 0 | 0/0 | 0.000 | N/A |
| stress-r3-mode0 | 592/588 | 476 | 0 | 0/0 | 0.000 | N/A |
| stress-r3-mode1 | 593/574 | 477 | 0 | 0/0 | 0.000 | N/A |
| stress-r3-mode2 | 594/572 | 477 | 0 | 0/0 | 0.000 | N/A |

每千次反馈使用测量前后实际出生计数差，边界存在少量反馈，属近似归一化。资源自身取完整Trace的GT独占时间；父表现Scope、枪口承载Scope、异步CPU和等待另列JSON，禁止重复相加。

进程内存受跨PIE的缓存、分配器及GC影响，各样本平均/P95保留在JSON，未宣称独立内存收益。

完整数据：[performance-analysis.json](performance-analysis.json)。逐帧事件树在各样本 selected-frames；CPU统计与窗口核对在 insights-windowed。GPU Ribbon结果由测量外的补充ProfileGPU/静态资源读回单独记录。玩家视觉验收待操作。

## 枪口CPU、P95与提交

下表均为两个客户端合计、按真实引擎帧归一化，再取三轮均值。“承载+资源Scope并集”去除嵌套重复，仍只是已归属范围，不能再与资源自身或父Scope相加。

| 模式 | 枪口资源GT自身 ms/帧 | 枪口资源全CPU线程自身 ms/帧 | 承载+资源Scope并集平均/P95 ms | 父表现Scope含子项 ms/帧 |
|---|---:|---:|---:|---:|
| 0 | 3.136 | 5.732 | 5.511/26.866 | 4.606 |
| 1 | 3.243 | 5.922 | 5.705/26.294 | 4.756 |
| 2 | 0.137 | 0.198 | 1.006/1.814 | 2.143 |

| 样本 | 双World接纳/出生 | 生命周期数组上传 | 姿态数组上传 | 姿态查询 |
|---|---:|---:|---:|---:|
| dense200-r1-mode0 | 16320/16320 | 0 | 0 | 484615 |
| dense200-r1-mode1 | 16000/15840 | 0 | 0 | 498919 |
| dense200-r1-mode2 | 16320/16320 | 703 | 2060 | 662410 |
| dense200-r2-mode0 | 16468/16200 | 0 | 0 | 460846 |
| dense200-r2-mode1 | 16216/15840 | 0 | 0 | 445311 |
| dense200-r2-mode2 | 16000/15840 | 649 | 2132 | 665716 |
| dense200-r3-mode0 | 16360/16104 | 0 | 0 | 465107 |
| dense200-r3-mode1 | 16100/15840 | 0 | 0 | 483627 |
| dense200-r3-mode2 | 19384/18968 | 1857 | 2642 | 741436 |
| stress-r1-mode0 | 0/0 | 0 | 0 | 0 |
| stress-r1-mode1 | 0/0 | 0 | 0 | 0 |
| stress-r1-mode2 | 0/0 | 0 | 0 | 0 |
| stress-r2-mode0 | 0/0 | 0 | 0 | 0 |
| stress-r2-mode1 | 0/0 | 0 | 0 | 0 |
| stress-r2-mode2 | 0/0 | 0 | 0 | 0 |
| stress-r3-mode0 | 0/0 | 0 | 0 | 0 |
| stress-r3-mode1 | 0/0 | 0 | 0 | 0 |
| stress-r3-mode2 | 0/0 | 0 | 0 | 0 |

第三轮批量补采中，实际出生量18968高于单次16104，净位移单位158高于68，并使用两个LOD组件；这些差异保留在结果中。整体FPS属于实际固定入口结果，不能宣称运动轨迹和交火时间线完全一致；每千次实际反馈成本用于补充比较。

## 进程内存

| 场景 | 模式 | 物理内存三轮均值/P95 MiB | 虚拟使用均值/P95 MiB |
|---|---:|---:|---:|
| dense200 | 0 | 9481/9532 | 17299/17365 |
| dense200 | 1 | 9700/9750 | 17452/17524 |
| dense200 | 2 | 9502/9540 | 17227/17277 |
| stress | 0 | 10478/10757 | 18857/18927 |
| stress | 1 | 11296/11358 | 19569/19624 |
| stress | 2 | 9740/9817 | 18288/18352 |

## 压力场景的实际负载与截帧瓶颈

额外入口保持四来源各125枚；正常交火弹丸也在运行。服务器计数起/止：`active=2611 created=17964 ended=15353 payloadBytes=1715344 batches=1921 bootstrap=0 queued=44947 policy=events_only domains=2201/125/125/160` → `active=2547 created=79033 ended=76486 payloadBytes=6146339 batches=6994 bootstrap=0 queued=233192 policy=events_only domains=2194/101/121/131`。

代表性压力样本GT独占时间（不能与含子项的World/父Scope重复相加）：

| Scope | 平均 ms/引擎帧 | 最大单次 ms |
|---|---:|---:|
| GuLiSceneUI_Paint | 7.931 | 5.417 |
| GuLiCommander_PredictiveAvoidanceCandidateQuery | 5.530 | 8.871 |
| UWorld_Tick | 4.772 | 76.066 |
| Projectile Movement | 4.233 | 2.417 |
| CombatEffectReplication | 3.081 | 28.912 |
| ProcessLocalPlayerSlateOperations | 2.182 | 3.271 |
| BP_CombatAvatarFly01_C | 2.138 | 12.300 |
| GuLiCombatEffects_Presentation | 2.109 | 3.668 |
| Slate::AddCustomVertsElement | 2.029 | 1.928 |
| GuLiCommanderMassStateTreeProcessor_0 | 1.932 | 5.012 |
| GuLiCommander_ManualAvoidanceRefresh | 1.764 | 3.185 |
| ProcessUntilTasksComplete | 1.676 | 25.564 |
| GuLiCommanderPresentation_Interpolation | 1.553 | 1.552 |
| GuLiProjectilePool_NarrowPhase | 1.545 | 0.340 |

该样本中位/P95/最差实际GT引擎帧：73.83 ms / 91.58 ms / 112.12 ms。最差帧的 CombatEffectReplication 单次约28.9ms；SceneUI Paint约7.9ms/帧，避障候选查询约5.5ms/帧，Ship等 Projectile Movement约5.2ms含子项。这些属于本轮截帧发现，未在聚焦ID52的任务中改动其他管线。

## GPU Ribbon

六份旧/批量枪口资源均为CPU模拟，Ribbon Renderer的bUseGPUInit=false；运行Niagara.Ribbon.GpuInitMode=0，没有强制GPU初始化。因此ID52的GPU Ribbon初始化Dispatch为0。其他飞行/光束资源的GPU Ribbon工作由测量外的完整ProfileGPU另列：[资源读回](ribbon-renderer-readback.json)、[补充GPU记录](gpu-supplement/summary.json)。GPU平均/P95使用主采样CSV；补充GPU截帧不替代30秒窗口统计。

## 测量外完整GPU输出的Ribbon统计

前一份输出可能跨入启用详细标记前的帧，列为过渡；以下使用后两份有完整标记的输出。每份含GPU队列输出，不能当作每游戏帧均值，也不能归属单独Client1。它们包含其他飞行/光束资源，ID52 GPU初始化仍为0；主窗口GPU平均/P95以CSV为准。

| 场景 | 模式 | 全队列Ribbon独占Dispatch（两份） | 全队列Ribbon独占GPU ms（两份） |
|---|---:|---:|---:|
| dense200 | 0 | 276 / 536 | 2.290 / 2.434 |
| dense200 | 2 | 444 / 628 | 2.065 / 2.536 |
| stress | 0 | 1700 / 1601 | 6.300 / 5.805 |
| stress | 2 | 1700 / 1724 | 5.804 / 5.886 |
