# CharacterMovement、飞船及僚机移动开销分析


## 采样范围

复用已经保存的两个 30 秒 CSV／Insights 窗口，未重新启动 PIE、加载压力、改镜头或修改生产代码。当前只读探测已无 PIE World，见 [探测记录](live-movement-inventory.json)。

- 当前实战：CSV 905 帧，Trace 窗内 902 完整引擎帧，GT 平均 33.176 ms。每 World 有 16 辆采矿车、18 辆建造车，无 Ship、Ground 玩家角色及僚机 Pawn。
- 两次场景内容、视口、车辆运动与工厂数量不同，不能据此计算优化收益或断言 CharacterMovement 回退。
- 下表为同一进程内专服和双客户端累计 GT 时间，单位 ms／引擎帧。P95 按每帧完整 Scope 总时长计算，不按单次调用计算，也不把不同项目的 P95 相加。客户端 A／B 是 Trace 分支，不冒认 Client1／Client2。

## 当前车辆 CharacterMovement

| 范围 | 每帧调用 | 平均 ms | P95 ms |
|---|---:|---:|---:|
| 服务器 | 34 | 0.840 | 1.243 |
| 客户端分支 A | 34 | 0.648 | 0.907 |
| 客户端分支 B | 34 | 0.599 | 0.758 |
| 合计 | 102 | 2.088 | 2.663 |

完整窗内合计最大 3.596 ms。代表帧中位帧 1.769 ms、整帧 P95 代表帧 2.080 ms，最慢整帧 3.596 ms；这三个数与 CharacterMovement 自身 P95 的定义不同。

采矿、建造均继承 ACharacter，并使用 `UGuLiExternalCharacterMovementComponent`；该组件复用引擎普通 CharacterMovement Tick，扩展的是移动修订与网络校验。`bAlwaysRelevant` 使这些车辆都在双客户端保有副本。采矿 Pawn 的客户端 Actor Tick 关闭，并未关闭其 CharacterMovement 组件 Tick。600 枚 Mass 移动单位不等于额外 600 次 CharacterMovement。

原始全窗计时的 CharacterMovement 包含子项约 2.091 ms、扣除已记录子项后约 1.516 ms，差额约 0.575 ms。代表帧子树明确出现 `VehiclePresentation → SkeletalMeshComponent0`、采集器、炮塔、挂载和光束组件。因此开销包含运动引起的机械部件级联变换。未标记的内部地面查询／物理求解仍在内部时间中；现有 Trace 不足以分别量化 PhysWalking、FindFloor、Sweep，不能把 1.516 ms 全算成地面查询。原始全窗统计含边界 Scope，与本次只计完整帧的 2.088 ms 略有差异。

两个客户端合计约 1.248 ms，值得优先检查远端车辆的可见性、平滑与多部件变换。服务器权威移动、导航及碰撞应继续推进，客户端方案也必须保留网络样本、死亡、改令、外部位移和必要姿态求值。即使假设全部消除当前 2.088 ms 且其他成本不变，整帧也只会由约 33.21 降至 31.12 ms（约 30.1 → 32.1 FPS）；这不是实测收益或实际可达到的目标。

## 旧飞船场景：分清角色移动和弹丸移动

| 范围 | 平均 ms | P95 ms | 解释 |
|---|---:|---:|---|
| 全部 CharacterMovement Tick | 1.279 | 1.509 | 每 World 34 辆车＋1 Ship＋1 Ground，共 108 次／帧 |
| 服务器 CharacterMovement Tick | 0.451 | 0.615 | 每帧 36 次，仍为车辆与玩家合计 |
| 两个客户端 CharacterMovement Tick | 0.433／0.395 | 0.554／0.481 | 每分支 36 次 |
| CharacterMovementServerMove（CSV） | 0.195 | 0.247 | 网络入口计时；未按 Ship／Ground 单独归因，不能直接加到 Tick 合计 |

`UGuLiShipMovementComponent` 确实继承 CharacterMovement，承担玩家输入、位置预测、RPC、平滑和规范移动历史。旧 CSV 中 `Ticks/GuLiShipMovementComponent=3`、`Ticks/GuLiGroundMechMovementComponent=3`、外部车辆组件 `=102` 是三 World 累计的启用 Tick 数量，**不是耗时**。引擎 `RecordWorldCountsToCSV` 的源码已核对。

现有 Trace 把这三类都记成 `CharMoveComp → UCharacterMovementComponent_TickComponent`，缺少 Owner／组件类身份。只能给出 1.279 ms 合计，不能按 3／108 调用比例伪造飞船单独 ms。复查飞船自身时应增加 Ship Tick、PerformMovement、SimulateMovement、ServerMove 及历史提交的独立 Scope。


## 僚机路径与耗时

僚机是 `AGuLiWingmanPawn : APawn`，运动为 `UGuLiWingmanFlightMovementComponent : UPawnMovementComponent`。Pawn Actor Tick 和运动组件 Tick 均关闭，Owner 的 Relay 调用 World 子系统，在 30 Hz 固定步长推进。低 FPS 时每渲染帧会补多个固定步，最多 4 步，所以逐帧运动调用会增加，模拟频率没有变成逐渲染帧一次。

| 旧场景相关 GT Scope | 平均 ms | P95 ms | 边界 |
|---|---:|---:|---|
| WingmanRelay | 0.810 | 1.075 | 包含飞行、导航、攻击、姿态捕获、同步及服务器 Relay 维护 |
| GuLiWingmanPresentationActor | 0.462 | 0.591 | 表现轨迹求值、角色管理及远端模型更新 |
| NS_WingmanFlightTrail | 0.381 | 0.450 | 尾迹 Niagara 的 GT 部分，未包含全部异步／GPU 成本 |


Owner 正常固定步通过 `SafeMoveUpdatedComponent` 做 QueryOnly 球体移动，命中才继续 `SlideAlongSurface`；远端僚机关闭碰撞，只求值已接收的姿态。当前源码正常固定步不调用多方向 `ProbeHeading`，其调用只在恢复辅助函数中，不能把它描述成每个正常固定步都执行的扫描。

还有明确的变换工作可评估：Owner 固定步移动父组件后，再更新 PresentationRoot 插值；Remote `ApplyRemotePresentation` 先 `SetActorTransform`，再写 PresentationRoot 的平滑世界姿态。父子变换均会触及模型和尾迹挂载。旧中位帧在 Relay 子树看到 77 次 WingmanMesh 更新，在 PresentationActor 子树看到 50 次，说明应检查同帧级联更新和重复呈现提交；这不是独立僚机数量，也不证明所有更新都可删除。可以评估延迟子组件传播和屏外呈现降频，保持 Owner 30 Hz、服务器合同及屏外逻辑。

当前无僚机场景仅有空 Relay 平均 0.0173 ms，没有该尾迹系统和 PresentationActor 的活跃成本。不能用这个零负载结果验收旧场景僚机优化。

## 后续优先级与验证边界

1. 当前场景优先细分客户端车辆运动／平滑与多部件变换；CharacterMovement 是次级成本，服务器整体 Tick 和 Slate 仍需分别处理。
2. 飞船压力场景优先拆分服务器弹丸物理移动、候选快照及碰撞；Ship 角色移动保留预测和网络合同，先补独立计时再选择改法。
3. 僚机优先量化固定步、导航、远端姿态、尾迹参数与网络捕获，然后评估同帧变换合并和屏外呈现。当前没有证据表明其 CharacterMovement 构成瓶颈，因为该组件不存在。

本次仅新增离线分析脚本、导出和报告，未编译、改逻辑、资源或地图，也未恢复整版 FPS 对照。具体优化收益仍需要同场景配对采样。

离线脚本语法、按 World 调用次数与耗时守恒、CSV 页尾过滤和报告链接检查通过。Progress 索引构建成功，文档检查退出码 0、结构错误 0、维护提示 17；未据这些静态检查宣称运行优化通过。

## 证据

- [汇总与每帧分布](movement-analysis.json)、[代表帧调用树](representative-movement-trees.json)、[可复现离线脚本](analyze_movement.py)。
- [静态检查](static-check.json)、[Progress 检查](progress-check.json)。
- [当前完整帧数据](current_no_ship/metrics.json)、[旧场景完整帧数据](previous_ship_stress/metrics.json)。
- [当前场景存档](../20261010-client1-live-frame-analysis/context-before.json)、[旧场景存档](../20261009-stress-frame-analysis/paired/runtime-p1-frame-diagnosis/context-before.json)。
- [采矿车辆](../../../Source/GuLiStrike/Gameplay/Resources/GuLiMiningVehiclePawn.cpp)、[建造车辆](../../../Source/GuLiStrike/Gameplay/Building/GuLiConstructionVehiclePawn.cpp)、[外部车辆移动](../../../Source/GuLiStrike/Gameplay/Units/GuLiExternalCharacterMovementComponent.cpp)。
- [Ship 移动合同](../../../Source/GuLiStrike/Gameplay/Ship/GuLiShipMovementComponent.h)、[僚机固定步](../../../Source/GuLiStrike/Gameplay/Wingman/Movement/GuLiWingmanFlightMovementComponent.cpp)、[Owner 调度](../../../Source/GuLiStrike/Gameplay/Wingman/GuLiWingmanSimulationSubsystem.cpp)、[远端呈现](../../../Source/GuLiStrike/Gameplay/Wingman/GuLiWingmanPawn.cpp)。
