---
schema: guli-progress/v1
id: DEV-20260909-002
work_id: WORK-20260909-002
kind: development
role: root
title: Ship 僚机客户端 Pawn 与逐架 StateTree 全面重构 — 技术方案
areas:
- wingman
- ship
- combat
- network
- ai
categories:
- gameplay
status: done
verification: partial
created: '2026-09-09'
updated: '2026-09-09'
summary: 在现有 GuLiStrike 模块把僚机客户端 Mass/ISM 实现迁移为 Owner 模拟 Pawn 和 Remote 插值 Pawn；每架运行 UE StateTree 与客户端飞行组件，服务器仅转发姿态并保留战斗权威。
next_action: 后续另立空战射界循环工作项，使持续目标场景的25个成员均完成至少两轮有效开火。
relations:
  requirement: REQ-20260909-002
  predecessor: WORK-20260908-002
status_note: Pawn/Movement/UE StateTree/Actor池、僚机Mass清理及v13合同已落地。正常Candidate取消服务器World、FlightNav、运动包络、配置版本、上传Grant与ACK基线门禁；本地0.5秒脱困且无僚机互撞，服务器重定位RPC仅保留兼容。Wingman
  95/95、200 Actor、300秒持续飞行、Editor/Game构建与BuildId门禁通过；300秒空战每成员两轮开火仅主机17/25、远端7/25，用户于2026-09-09决定暂不扩修，故以done / partial收尾。
---

# Ship 僚机客户端 Pawn 与逐架 StateTree 全面重构 — 技术方案

对应需求：[REQ-20260909-002](../RequirementDocument/20260909-Ship僚机客户端Pawn与逐架StateTree重构.md)。前置实现为 [WORK-20260908-002](20260908-Ship僚机空地统一匈牙利自动选敌.md)。

## 技术选型与权威边界

保持单一 `GuLiStrike` Runtime 模块，不移动服务器战斗合同或 Commander Mass 类型。新增 Pawn、MovementComponent、逐成员行为数据和 Actor 表现池均放在 `Gameplay/Wingman`；v13 线协议继续放在 `Battle/Contracts`，服务器事务和校验继续由 `Battle/Network/Relay`、`Battle/Relay` 和 `Battle/Combat` 持有。

租约拥有客户端运行每成员 UE `UStateTreeComponent`、FlightNav 路径请求、三维避障、攻击路径和 MovementComponent，并持续产生位置、旋转与速度。服务器不运行这些 UObject/Actor 模拟，也不验证速度包络、轨迹、FlightNav、世界碰撞或母舰距离；正常姿态只经过协议、当前拥有者/租约、连接代次、递增帧号、结构和存活完整身份门，随后原样发布 Accepted Pose。远端客户端仅插值该 Accepted Pose。目标分配、FireIntent、伤害、Roster 和租约仍由服务器裁决。

## 数据流

1. Relay 收到 Bootstrap 或租约状态后，把可靠 Roster、编队、能力、武器和攻击状态同步给 `UGuLiWingmanSimulationSubsystem`。
2. Subsystem 按完整成员身份维护 Owner Pawn 注册表；每帧按稳定顺序累积固定步，并为每 Flight 共享 FlightNav 结果。僚机之间不生成邻机快照、不做分离或碰撞。
3. 每个 Pawn 的 StateTree 选择当前行为，攻击模拟或编队逻辑生成移动请求；MovementComponent 完成扫掠、恢复、速度积分和唯一位姿写入。
4. Subsystem 从 Pawn 运行时状态构建 Candidate、FireIntent 和 QA 诊断，Relay 按现有批次事务提交服务器。
5. 服务器按当前租约与完整存活身份接纳新 Flight 帧并发布 Accepted Pose。Roster、能力、编队、导航、上传 Grant 与上一 ACK 的暂时错位只作为诊断元数据，不阻塞正常姿态。Presentation 对 Owner 直接引用模拟 Pawn，对 Remote 从快照池租用同类 Pawn 并插值。
6. Owner Pawn 连续0.5秒无进展时本地选择安全恢复点并立即继续 `Rejoin`。v13紧急重定位请求/响应与 `RebasedMemberMask` 保留线协议兼容，生产 Tick 不自动发送该 RPC。

## 改造点与接口

| 变更点 | 路径/类 | 接口与完成判据 |
|---|---|---|
| 僚机 Pawn | `Gameplay/Wingman/GuLiWingmanPawn` | 非复制、不可 Possess、双运行模式；持有完整身份、运行时数据、无同类阻挡碰撞、UE StateTree 和 MovementComponent |
| 三维移动 | `Gameplay/Wingman/Movement/GuLiWingmanFlightMovementComponent` | 30Hz 固定步、唯一位姿写入、最低速度/转弯/倾斜、客户端FlightNav/世界扫掠/母舰软门和0.5秒本地恢复；服务器状态不暂停该组件 |
| 逐成员行为 | `Gameplay/Wingman/Behavior/GuLiWingmanMemberBehavior` 与 `ST_WingmanMemberBehavior` | 每架独立运行UE原生StateTree；六状态固定优先级选择移动请求，不直接写Transform或Velocity，不含等待服务器状态 |
| Owner 模拟注册表 | `UGuLiWingmanSimulationSubsystem` | Mass Entity 数组替换为弱 Pawn 注册表；新增 `AdvanceOwnedGroup`、`FindOwnedPawn`、`GetOwnedPawnCount`，原 Relay-facing API 保持或提供窄兼容层 |
| Actor 表现池 | `AGuLiWingmanPresentationActor` | 移除 Owner/Remote ISM 和 Remote Mass Mirror；保留快照、回看、外推、淡出与 128 组容量，最多激活 200 个 Pawn |
| v13 合同 | `GuLiWingmanProtocolTypes` | 保留重定位原因/请求/响应和 `RebasedMemberMask`；正常姿态采用latest-wins且ACK基线、上传Grant和可靠配置版本不构成接纳门 |
| 服务器 Relay | Relay Server/Component、CombatCoordinator | 正常姿态仅校验传输身份和递增帧后转发；WorldValidator/CarrierResolver调用为零，服务器Wingman Movement写入为零；紧急重定位RPC仅兼容解码 |
| Mass 清理 | `Gameplay/Wingman/Mass` 与组级 Runner | 删除所有僚机 Mass Tag/Fragment/Processor/Archetype/镜像使用和旧组级行为执行；纯数学辅助迁入普通 C++ |

## 移动与恢复实现

- `FGuLiWingmanMovementRequest` 统一表示目标位置、期望速度、行为、冻结攻击轮和是否允许紧急悬停。StateTree/C++ 回退只写该请求。
- Subsystem 每次固定步按完整身份稳定排序后依序推进所有 Pawn。不同僚机不互读碰撞或分离输入；交叉、重叠不会改变彼此航向和速度。
- MovementComponent 从期望方向计算有限转弯和倾斜，保持正常飞行最低速度；将最终位置量化为与 Candidate 一致的整数厘米后，再通过 FlightNav、球体 Sweep 和母舰包络门。
- 主位移失败时依次尝试切向滑移、由成员稳定种子产生的有限三维方向、向上爬升、最近安全点和零平移悬停转向。世界扫掠显式忽略 `AGuLiWingmanPawn`，每个世界障碍备选通过相同客户端最终门。
- 最近安全点只在连续合法移动后更新。0.5 秒无进展进入 `EmergencyAvoid` 并执行单成员本地安全重定位；候选会按固定tick轮换16个角度和5层高度，避免重复传送到同一失败点。恢复后以最低速度 `Rejoin`，不等待服务端响应。
- 即使移动请求意外为零，存活 Pawn 仍累计无进展时间并进入本地恢复，不再从零速度分支提前返回形成永久静止。连续 0.5 秒前进后清空恢复状态。
- 冻结对地轮在进入真实死锁时由客户端取消并清除本地 Checkpoint，恢复后读取当前服务器分配重新开轮；迟到开火仍由服务器按成员、目标、RunId和分配版本拒绝。

## v13 姿态事务与兼容重定位

- 普通 `FGuLiWingmanCandidateBatch` 继续携带完整成员身份、Flight帧号、位置、旋转、速度及诊断版本。Relay验证当前租约、连接代次、递增帧、结构和存活身份后直接构造 Accepted Batch，不调用CarrierResolver或WorldValidator。
- `BaseAcceptedSequence`、`ObservedGrantRevision`、Roster/能力/编队/导航/障碍版本在普通姿态中只供诊断。上一条Accepted响应丢失或可靠配置晚一帧到达，不会阻断后续新Flight帧；原子Bootstrap/Resume/Takeover仍校验冻结的可靠Cut。
- 全局Candidate桶与每Flight上传Grant不再拒绝当前v13普通姿态。客户端仍按5Hz巡航/10Hz战斗节奏发送，以控制带宽；新帧由递增FrameSequence保证latest-wins。
- `FGuLiWingmanEmergencyRebaseRequest/Response` 与 `RebasedMemberMask` 保留NetSerialize、哈希和旧端解码合同，服务器实现仍可处理显式兼容调用。Relay生产Tick不再从Stale或无进展状态自动发起该请求，本机恢复不依赖它。
- 连接活性只取可靠心跳或任一Accepted Pose的最近时间；低速、单Flight年龄、导航失败和配置错位不改变连接生命周期。Socket Logout仍立即进入拥有者丢失流程，心跳看门狗只是无流量兜底。

## 任务清单

- [x] T1：创建需求/开发文档，记录基线、授权测试范围和前置工作项；最终Progress索引检查并入T8。
- [x] T2：实现 `AGuLiWingmanPawn`、运行模式、完整身份和运行时数据；Dedicated Server 不创建，Owner 与 Remote 生命周期可独立验证。
- [x] T3：实现 `UGuLiWingmanFlightMovementComponent`、逐成员UE StateTree、30Hz稳定推进、客户端FlightNav/世界球体扫掠和0.5秒本地恢复；迁移现有编队与攻击运动，并去除同类碰撞。
- [x] T4：把 `UGuLiWingmanSimulationSubsystem` 从 Mass Entity 改为 Pawn 注册表，Candidate、FireIntent、Bootstrap、Roster 和租约流程全部改读 Pawn；战斗授权失效不再停飞。
- [x] T5：把 `AGuLiWingmanPresentationActor` 改为最多 200 个 Pawn 的 Owner/Remote Actor 池，保留快照语义并删除僚机 ISM 与远端 Mass Mirror。
- [x] T6：完成v13合同、序列化、稳定哈希和兼容重定位事务；正常Candidate改为结构/身份校验后直接转发，生产路径停用服务器重定位请求。
- [x] T7：删除僚机 Mass Fragment/Processor/Archetype 与组级 Runner 使用，更新 Build 引用、QA、测试夹具和 UE StateTree 资产命令；Commander Mass 依赖保持。
- [x] T8：执行并记录授权范围内自动化、300秒PIE、200 Actor性能、Editor/Game构建和BuildId门禁；按用户决定把空战持续射界失败保留为已知问题，以部分验证状态更新需求、玩法文档和实施归档。

## 已授权测试范围

- Actor 生命周期：25 个稳定身份、死亡/补充换代、Bootstrap、Resume、Takeover、远端池化和 Dedicated Server 零 Actor。
- Movement/StateTree：环绕、归队、空战、轰炸、三维避障、短暂悬停、动态阻挡、FlightNav 边缘及凹角脱困。
- v13 Relay：错误租约、错误连接、旧帧、伪造成员拒绝；普通姿态容忍ACK基线、Grant和可靠配置元数据错位；兼容重定位NetSerialize、显式调用及旧开火记录拒绝。
- 战斗回归：25 架合法目标全员参战、持续目标每架至少两轮、手动覆盖、冻结俯冲和空地混合。
- 性能与长测：移动/旋转Ship的300秒目标关卡PIE和200 Actor压力门；现有服务器4000 Candidate传输/身份验证压力保留。
- 回归入口：现有匈牙利分配、攻击、Relay、租约、FireIntent、FlightNav 和协议测试。只修订本迁移直接影响的夹具与 GoldenBytes，不扩修无关旧 Registry/严格能力夹具。

## 完成门禁

- 使用 `D:\UnrealEngine-5.7` 构建 `GuLiStrikeEditor Win64 Development` 和 `GuLiStrike Win64 Development`，退出码均为 0。
- 自动化由同一源码版 `UnrealEditor-Cmd.exe` 运行；直接比较引擎、项目和涉及插件 `UnrealEditor.modules` 的 BuildId。
- 目标关卡PIE运行300秒并保存移动/旋转Ship、25架Owner Pawn/StateTree、成员位移、Candidate拒绝、服务器Movement零写入和本地恢复证据。
- 原生代码与测试通过后更新需求验收项、T1–T8、`Progress/Gameplay/飞船.md` 和增量实施归档；最后运行 Progress 索引 `build` 和全量 `check`，要求零错误。

## 风险与备忘

- 从 Mass 到 Actor 改变的是客户端承载方式；若仍复用“最终门失败即减速为零且无出口”的逻辑，卡死会复现。当前零速度分支也累计恢复计时，并由本地安全重定位闭合恢复链，不依赖权威重定位响应。
- 每架 StateTree 和 Actor 组件提高 UObject/Tick 开销。固定步集中在 Subsystem、远端关闭 AI、池容量 200，并以 P95/最大帧耗时门禁约束。
- StateTree `.uasset` 是二进制资产，源码实现不得假定资产一定存在；保留同优先级C++回退。编辑器资产命令已把资产重建为六个UE原生行为状态，审计要求任务和条件各6个且不写Transform/Velocity。
- 当前工作区已有未提交的 WORK-20260908-002 源码和修改后的原型关卡。实施必须增量编辑，不能清理、回滚或重写这些用户改动。

## 实施验证结果

- 全量 `GuLiStrike.Wingman` 自动化：95/95完成，93项成功、2项既有预期警告、0失败。报告位于 `TestResults/WORK-20260909-002/ClientOwnedContinuityFinal/index.json`。
- 200 Actor门禁：25架Owner StateTree/30Hz模拟加175架Remote插值通过；采样P50为0.324ms、P95为0.4248ms、最大0.6368ms，均低于8ms/16ms门槛。
- 300秒双进程PIE：两端各25架Owner Pawn/StateTree持续存在，Relay Active，5秒静止违规0，普通/原子Candidate拒绝0，服务器Wingman Movement写入0；主机/远端Ship最大位移分别21988.607/21987.174cm，最大旋转179.998°/179.965°。
- 同一长测持续保持合法目标并让25架进入攻击行为，但“每成员至少两轮有效开火”未通过：主机17/25、远端7/25。该空战问题按用户决定留给后续工作项，对地攻击与冻结俯冲回归通过。
- 源码版 `GuLiStrikeEditor Win64 Development` 与 `GuLiStrike Win64 Development` 构建退出码均为0；引擎与项目 `UnrealEditor.modules` BuildId均为 `9872a344-2108-49e2-b40c-34c4e2c3ccfe`。

## 结果链接

- [需求](../RequirementDocument/20260909-Ship僚机客户端Pawn与逐架StateTree重构.md)
- [前置开发案](20260908-Ship僚机空地统一匈牙利自动选敌.md)
- [当前飞船玩法](../Gameplay/飞船.md)
- [实施归档](../Archive/20260909-Ship僚机客户端Pawn与逐架StateTree重构.md)
