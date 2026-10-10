# 600 移动单位 / 500 弹丸：PIE 截帧分析


## 本次实际捕获

源码 UE5.7 编辑器进程 72432，`GuLiStrikeEditor Win64 Development`，地图 `LVL_CommanderMassPrototype`；同进程一专服、两客户端，各 1280×720，质量3、固定镜头、VSync/帧率上限关闭。i9-14900KF / RTX4090。引擎与项目 BuildId 均为 `dd3ee083-a0fd-45c8-814e-67fe5ef95e31`；本轮未修改运行代码或资源，未重新编译。

600 移动 Mass 单位，四来源各125、目标500枚弹丸；每客户端额外16采矿+16建造持续光束、16枪口+16命中循环预览，与上次整版压力入口一致。接受器运行时长仅由45延至90秒，以便计时结束后仍能截帧；活跃目标、运动/碰撞/网络规则保持。**这是500活弹目标并持续补充的场景，同时存在高频创建/结束事件。**

热身10秒，原生 CSV/CPU/GPU/frame Trace计时30秒；CSV共467帧，QPC窗口内464个完整引擎帧。固定相机、人口600及测量窗口通过检查。实际服务端边界活跃 419→500，客户端数据槽开始324/324、结束475/469；客户端窗口内平均约463/463，Mesh Actor平均约93/93、P95为125。

| 指标 | 平均 | P95 |
|---|---:|---:|
| 整帧 | 64.328 ms / 15.55 FPS | 88.020 ms |
| GameThread | 64.345 ms | 87.676 ms |
| GPU | 16.217 ms | 22.813 ms |
| 进程物理内存 | 6438 MB | 6533 MB |

本次与上次三轮64.41ms/15.53FPS吻合。此前双客户端15–20FPS仅为用户历史观察，没有恢复旧代码，也没有把这个数字当作本次同负载基线。

## 游戏线程热点：整个编辑器进程 / 每引擎帧

以下为 **exclusive独占时间**，已剔除GPU同名计时。服务器、两个客户端和编辑器共享此游戏线程，不能把一项时间再乘2。并行工作线程的CPU量不相加为整帧时间。

| 位置 | 平均独占 ms/帧 | 占整帧 |
|---|---:|---:|
| 命中特效系统 | 8.174 | 12.7% |
| 飞行批次接收/应用 | 2.500 | 3.9% |
| Mass StateTree | 1.993 | 3.1% |
| 双客户端单位插值 | 2.087 | 3.2% |
| Niagara组件通用开销 | 1.729 | 2.7% |
| 本地玩家Slate操作 | 3.327 | 5.2% |
| Slate Prepass | 1.041 | 1.6% |
| Slate慢路径绘制 | 1.090 | 1.7% |
| 避障候选查询 | 1.231 | 1.9% |
| 双客户端飞行预测 | 0.499 | 0.8% |
| 场景UI更新 | 0.394 | 0.6% |
| 场景UI绘制 | 0.364 | 0.6% |

此外，`UWorld_Tick`未细分部分约5.73ms，`ProcessUntilTasksComplete`自身约2.84ms、`WaitForTasks`约1.09ms。前者包含执行嵌套任务，不能将其19.64ms inclusive全部当成空等；等待与算法CPU工作分别保留。

**命中特效不是仅有一段模拟成本。** 63.57ms普通帧内，命中特效8.887ms独占由：网络事件处理期间3.043ms、帧末Niagara渲染数据更新3.910ms、任务执行期间1.928ms等构成。30秒内该系统全CPU线程独占工作量约15.412ms/引擎帧，其中GT8.174ms；两项不能相加。

## 真实普通帧、P95帧和最慢帧

选帧依据为QPC窗口内完整 `FEngineLoop::Tick` 时长。与CSV P95口径分开；每帧GT所有exclusive之和均与该帧时长吻合。导出保留原生时间线顺序与高精度Duration，避免 %.9g Start/End的约10微秒舍入破坏微小Scope归属。

| 选取 | 引擎Tick ms | 命中特效独占 ms | 物理弹丸移动独占 ms |
|---|---:|---:|---:|
| median | 63.574 | 8.887 | 4.028 |
| p95 | 88.021 | 17.389 | 7.255 |
| worst | 101.096 | 6.307 | 11.775 |

**88.021ms的P95帧：** 命中特效17.389ms，其中9.883ms发生在飞行网络事件应用期间、5.303ms在帧末更新；物理弹丸移动7.255ms、StateTree3.905ms、WaitForTasks4.762ms。该帧由集中接收、特效启动/提交和移动开销共同放大。`MulticastFlightBatch`有嵌套同名Scope，其inclusive不能直接当作RPC总耗时累加。


## 特效实例与事件吞吐

两个客户端计时后的同场景盘点：ID5分别 **249 / 209活跃组件**，总组件281/241，差额均32。真实Niagara资产 `MaxPoolSize=32`、`PoolPrimeSize=0`、无固定Tick、无预热、EffectType为空。原生Niagara池回收代码在空闲容量达到MaxPoolSize后执行DestroyComponent，后续不足时NewObject。**32是空闲池上限，绝非32个同时活跃的上限。**

边界快照间服务器创建增加 **25,512**、结束增加 **25,431**，发送7,534批/6,458,210字节（两连接合计），可靠队列1462→1912。约30秒窗口伴随桥调用边界，折算只是每秒800多次创建和结束的近似数量。压力不只来自500条轨迹计算；短寿命/命中与补充同时驱动客户端事件与六层特效实例。

ObjectIterator还看到两客户端共30,425个无资产、非活跃Niagara对象，其中包含已销毁待GC/诊断残留，**不能据此宣布内存泄漏或3万个在模拟**。空闲池容量和组件再初始化值得优先对照，但本轮没有改池容量，也没有证明扩大池就能省多少ms。

已核对两个命中入口：生产端普通Linear事件 `bUseCatalogImpact=false` 由ApplyState播放；逻辑Projectile终止 `true` 在ApplyFlightEvent转换为Linear后播放，原State在ApplyState不满足Linear条件。本场景证据不支持“两个入口必然重复播放”，不把它当已确认根因。

## GPU单帧

使用UE5.7原生 **ProfileGPU** 捕获实际队列、Draw/Dispatch和Pass树；没有安装RenderDoc/PIX，因此没有伪称取得.rdc或逐Draw材质回放。CPU/GPU Trace、原生GPU帧树和实际客户端PNG已保存。

这次GPU单帧在计时窗口之后：Graphics忙碌 **21.930ms**（3056 Draw、3768 Dispatch），Compute **2.128ms**（186 Dispatch），Copy无记录工作；队列可并行，不能相加为24.058ms的整帧。

两个视图相关的 `Niagara GPU Ribbons` 分别 **4.752 / 4.501ms**，合计9.253ms，约该帧Graphics的42.2%；共2560 Dispatch。SceneRender两个主要视图分支10.861/9.194ms。Ribbon生成/排序固定调度成本是明确GPU热点；降低节点数或改Opaque并不会消除这些计算Pass。CPU发射器也可能采用GPU Ribbon初始化，当前Pass树没有资产身份，不能把9.253ms全算到采矿/建造或某个命中资产上。

GPU依然低于GT平均64ms。本轮不以牺牲分辨率、关闭光照、缩减人口/活弹或静默丢可靠事件来提高测试FPS。

## 已达预算的部分与当前改进顺序

避障查询原生World聚合 **1.223ms/引擎帧，活动P95 1.926ms**，达到≤1.6/≤3ms预算。双客户端飞行预测GT独占约0.499ms；原生每客户端表现总计1.301/1.346ms。场景UI Update0.239/0.171ms、Paint0.160/0.193ms，来源稳态发现0。采矿+建造系统GT独占合计0.160ms。它们当前只占整帧较小部分。

1. **先处理命中特效实例。** 增加真正启动/新建/池复用/完成计数；用更充足空闲池及预分配做3对容量对照，确认是否减少高峰重建；进一步把同用途命中组织为稳定槽位批次，保留六层轮廓、事件跟随、随机顺序、朝向和期限，兼容模块才采用GPU。
2. **处理Ship集中生命周期。** 保留Actor/ProjectileMovement/NotifyHit权威路径，验证服务器Actor复用和完全状态复位，减少125个一起到期、一起Destroy/Spawn的峰值；同时分析移动/场景Sweep。不得更改寿命、伤害/命中顺序或仅错开压力事件冒充优化。
3. **复用飞行批次解码/应用容量。** 保持可靠创建/结束、16条/1000字节及原事件顺序，减少临时数组、每事件配置解析和组件参数重建。现有8批信用和队列压力分别计量。
4. **减少GPU Ribbon调度数量。** 按资产与用途做隔离帧、尝试跨实例批次/共享生成结果；分别记录CPU和GPU变化。Opaque与8节点的视觉合同保留；本轮不直接切换未经对照的新候选。

这是本轮截帧后的新热点处理顺序，尚未执行这些追加修复或承诺新增FPS收益。30FPS需要将总GT从64.33降至33.33ms量级，仅继续减少约1ms的查询或预测不足以达到该目标。

## 正式应用确认与证据入口

用户最新明确“直接替换成新特效新逻辑”。正式使用授权已记录，原先加宽Opaque、保留火花碰撞的视觉确认与现在组合的逐帧效果反馈仍分开。

- ID4：`/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanLaserPool_SmallBatches`
- ID5：`/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunImpact_AllOptimizations`
- ID36：`/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Optimized_GPU_All`
- ID38：`/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanLaserPool_SmallBatches`
- ID45：`/Game/GuLiStrike/Buildings/Construction/NS_ConstructionLaser_Optimized_GPU_All`
- ID52：`/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunMuzzle_AllOptimizations`


- [交互帧时间线与两客户端画面](index.html)
- [CSV整帧数据](paired/runtime-p1-frame-diagnosis/frames.csv)、[原始Unreal Insights轨迹](paired/runtime-p1-frame-diagnosis/session.utrace)
- [窗口内GT计时与CPU资格](paired/runtime-p1-frame-diagnosis/insights-windowed/summary.json)
- [三个真实帧的分析](frame-analysis.json)、[完整事件导出目录](selected-frames/median-events.csv)
- [GPU单帧原始队列/Pass报告](gpu-profile-frame.log)、[GPU结构化行](gpu-profile-rows.json)
- [客户端1截图](client-1.png)、[客户端2截图](client-2.png)、[截图时间与范围](viewport-frames.json)
- [Niagara对象盘点](post-window-inventory.json)、[池配置只读回读](pool-settings-readback.json)
- [测量场景/人口/有效性](paired/runtime-p1-frame-diagnosis/result.json)、[控制恢复](restored-runtime.json)

截图用ReadPixels、对象盘点和ProfileGPU均在计时窗外；截图HUD显示8FPS受盘点/ReadPixels影响，不能代替窗口15.55FPS，也不与选取的CPU帧宣称同步。结束后停止本轮PIE，恢复CaptureSeconds20、Slate节流1及原生GPU分析选项，新优化仍开。此轮仅新增诊断脚本和报告，生产代码/资产未改、对应Map无需重建或再次保存。
