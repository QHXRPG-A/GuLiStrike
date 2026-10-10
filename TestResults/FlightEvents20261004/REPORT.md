# 飞行物启停同步与实际时间预算 — 2026-10-04

代码已实施，源码版 Editor 编译、冷重载和运行测试已执行。连接额度改按实际网络时间恢复；指挥官、Ground、Ship、僚机的飞行物采用统一可靠创建/结束批次，客户端独立 Actor 池表现，服务器检测和结算。战局协议为 24，双方须一起更新。

## 预算实测

[1608 条原生连接样本](budget_native_verification.json)的最大恢复偏差为 **0.002507%**，债务重算不一致为 **0**。控制卡顿后的实际连接间隔为 1.124736 秒，四个连接都只恢复 100ms / 200000 bit；不是按整段卡顿累计信用。

406 单位、两个 Commander 客户端、无开火的预算开关对照各采集约 30 秒。恢复开关开启后，两连接的最老待发姿态年龄 P95 和最大值均为 0ms；关闭时 P95 同样为 0ms，最大值为 113.4ms。[对照证据](budget_ab_pose_age.json)。该场景未饱和，不据此宣称带宽或延迟提升百分比。

原生间隔覆盖实际约 20Hz 与 32–36Hz。设置 60/120 上限后，当前 PIE 实际只到 35.92/36.04Hz，因此没有把它们算作原生 60/120Hz 通过。20/35/60/120Hz 的离线公式验证通过，见 [公式核对](budget_formula_review.json)；两种证据不能混用。

## 四来源飞行物

Dedicated Server + 四个同进程客户端，真实席位为 Commander、Ground、Air、Commander；通过真实 Ship 确认选择安装 Hangar 节点 08。服务器每域补充 125 枚，测试伤害为 0.001，正常武器数值不变。500 是测试目标，没有增加玩法上限。

30.031 秒观测 / 29.954 秒原生网络采集：

| 指标 | 实测 |
|---|---:|
| 活跃飞行物峰值 | 508（包括普通战斗）；fixture 峰值 500 |
| 四域稳定样本 | 125 / 125 / 125 / 125 |
| 飞行启停 RPC 流量 / 客户端 | 19449.72 B/s，含 RPC 头尾 |
| 全部下行 socket 流量 / 客户端 | 43264–45437 B/s |
| 最大飞行批次参数 | 995.5 B；编码器另限制载荷 ≤1000 B、条数 ≤16 |
| 旧逐弹快照 / 纠偏 / 移动复制 | 本次采集均为零 |
| 客户端池容量 / 活跃峰值 | 768 / 508；复用阶段容量保持 768 |
| 专服表现池 | 0 |
| 整个战斗表现子系统更新 P95 / 客户端 | 1.4842 / 1.3334 / 1.2909 / 1.2248 ms |
| 最老待发单位姿态年龄 P95 | 四连接均 0ms；该负载的单位 roster 仅 2 |
| 同进程整体帧率 | 18.21Hz，547 帧；帧间隔 P95 88.62ms |

[观测](mixed_500.json)、[网络原件](mixed_500.nprof)、[分析](mixed_500_summary.json)、[姿态年龄](mixed_500_pose_age.json)。CPU 数值包含其他战斗表现，不能当作单独 Actor、碰撞预测或服务器检测的成本。该 PIE 同时渲染四个客户端及 Editor，不能拿来承诺专服或打包版本帧率。

旧日志中的 `ClientReceivePublicPoseFrame` / `ClientReceiveAttackCandidateResult` 是僚机本体姿态和候选结果，不是弹丸位置同步；这些成本仍存在。流量容器与 RPC 内容不重复相加。

## 生命周期与伤害

- 五号客户端中途加入：服务器一次性补建 500 枚；新客户端接收、四域各 125、客户端没有服务器物理弹丸。[补建](late_join_midflight.json)、[四域回读](latejoin_domains_verified.json)。这是中途加入证据；没有额外模拟丢包下的断线重连。
- 两次负载结束后，原四客户端活跃池归零，容量保留；再次启动峰值 500，没有继续扩容。[复用](pool_reuse_ground_fire.json)、[最终回收](mixed_runtime_complete.json)。
- 真实 Ground 武器从服务器入口开火 59 次，每个客户端对应机甲各播放 59 次表现。[开火](ground_authority_fire_verified.json)。Python 最初直接调用客户端 `SetFireHeld` 没有发出实际请求，未据此认定客户端输入 RPC 通过。
- 来源 Ship 销毁后，已发射的数据飞行物继续存在并正常结束；客户端池在销毁后两秒仍有 377 枚，最后归零。[来源死亡](source_death_result.json)、[最终烟测](final_smoke_complete.json)。报告中的 GUID 交集覆盖全部效果，含非飞行法术场；飞行数量以池计数为准。
- 17 项唯一原生回归最终通过，含截断批次拒绝、条数/字节限制、重复创建、结束墓碑、Epoch 回绕、补建时间推进、池复用、非复制 Actor、伤害去重、目标丢失及导弹 Epoch 隔离。[结果](regression_final_summary.json)。

首次回归有一项旧表断言失败，期待半径 160cm 和 ID 1 扫荡者。回读现表后只更新测试基准为 300cm、扫荡者 ID 5，复测通过；未改游戏数值。旧导弹 RPC 合约测试改查统一可靠批次。最初加单位时还触发过原 Crowd 1024 代理容量断言，该会话不是合格性能样本，也没有扩大其容量。

## 保存场景与复现

`/Game/Maps/LVL_CommanderMassPrototype` 保存了四个 PlayerStart、`FlightEventsQAOrigin` 标记和 `FlightEventsQA_BounceWall`。原先驱号部署保留。新增入口仅用于显式验收，默认休眠，Shipping 禁止启动负载。

1. Editor 控制台运行 `gs.Flights.PIE`，等待四个客户端实际加载。
2. 通过 `Scripts/commander_editor_python.py --file Scripts/FlightEvents/prepare_sources.py` 安装真实机库并开始服务器 500 枚 / 30 秒补充。该脚本先把 Air 移到导航内的安全锚点，避免随机出生点令机库初始导航门失败。
3. 提前停止用服务器 `gs.Flights.Stop`。现存飞行物按正常生命周期结束。采集可加载 `Scripts/FlightEvents/observe_runtime.py`，明确调用 `flight_observe(...)`。

人口隔离可在测试前设置 `guli.stronghold.TeamUnitCap 1`；测试结束恢复 300。交付时 RealTimeBudget=1，全部本轮诊断开关=0，t.MaxFPS=0，PIE 停止。场景最后保存与回读见 [场景回读](final_scene_readback.json)，模块 BuildId/哈希见 [编译清单](build_manifest.json)。

## 验收边界

持续飞行状态流已去除，预算恢复、约 500 枚峰值和上述生命周期技术检查通过。**同负载旧版与新版“飞行总流量下降至少 50%”尚未确认**：历史 [诊断](../NetworkLive20261003/REPORT.md) 人口、开火、镜头及客户端数不同，不能计算合格差值。本机原生 60/120Hz 间隔也尚未达到。独立 Actor/碰撞成本、丢包重连、完整移动目标及双枪散布视觉矩阵仍需专门记录；不把本轮技术检查扩写为这些项目全通过。
