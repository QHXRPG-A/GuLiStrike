---
schema: guli-progress/v1
id: REQ-20260909-002
work_id: WORK-20260909-002
kind: requirement
role: root
title: Ship 僚机客户端 Pawn 与逐架 StateTree 全面重构
areas:
- wingman
- ship
- combat
- network
- ai
categories:
- gameplay
status: approved
verification: not_applicable
created: '2026-09-09'
updated: '2026-09-09'
summary: 将僚机客户端模拟从 Mass 迁移为独立 Pawn、逐架 UE StateTree 与专用三维飞行 MovementComponent；拥有客户端持续飞行并向服务器上报姿态，服务器转发姿态且只保留战斗与身份权威。
next_action: 后续另立工作项修复空战成员长期无法形成第二轮有效射界；本工作项按部分验证结果收尾。
relations:
  development: DEV-20260909-002
  predecessor: WORK-20260908-002
status_note: 用户于2026-09-09批准完整实施方案，并进一步确认拥有客户端负责逐架寻路、避障、攻击运动和姿态；服务器不得以导航、轨迹、编队版本或能力版本暂停本机僚机，僚机之间不做碰撞与分离。现有WORK-20260908-002统一匈牙利分配和攻击语义作为兼容基线。2026-09-09收尾时，用户决定暂不扩修空战持续射界问题；其余Actor迁移、持续飞行、对地攻击、Relay、性能和构建门禁按实测结果验收。
---

# Ship 僚机客户端 Pawn 与逐架 StateTree 全面重构

## 背景与目标

现有僚机由客户端 Mass Entity、Fragment、Processor 和 ISM 驱动。长时间 PIE 中，部分成员在 FlightNav、物理障碍或母舰距离最终门失败后降速至零，并可能持续停留在恢复状态；组内多个成员聚集在相同边界时，既有 Mass 运动恢复链缺少稳定脱困出口。

本需求将每架客户端僚机改为独立 `AGuLiWingmanPawn`，使用专用三维飞行 MovementComponent 作为唯一位姿写者，并以每架各自运行的 UE StateTree 选择护航、避障和攻击行为。迁移同时重做阻挡恢复链，避免把既有停止逻辑复制到 Actor。拥有客户端持续产生位置、旋转和速度并上报；服务器不复演、不修正、不以 World、FlightNav、轨迹或配置版本差异否决正常姿态，只转发结构合法且属于当前租约存活身份的最新姿态。

本需求承接 [WORK-20260908-002](../DevelopmentDocumentation/20260908-Ship僚机空地统一匈牙利自动选敌.md)。统一空地匈牙利分配、手动目标优先、对空与对地攻击、冻结俯冲轮、Legacy 武器和 Ship 索敌范围显示均保持原有玩法语义。

## 已确认规则

### R1：客户端 Pawn 生命周期

- 本机租约拥有者为一艘 Ship 的 25 个存活编制成员各创建一架非复制 `AGuLiWingmanPawn`；Dedicated Server 不创建僚机 Pawn。
- Pawn 不接受玩家或 `AIController` Possess，以球形碰撞为根组件，使用现有僚机网格和专用 `UGuLiWingmanFlightMovementComponent`。
- 成员以完整组、槽位和 `EntityGeneration` 身份绑定。Bootstrap、Roster 补充、死亡、Resume、Takeover 和租约交接必须按完整身份创建、重置或销毁 Pawn，旧代成员不能复用新代授权；能力/战斗配置失效只清除攻击授权，不销毁或停用飞行 Pawn。
- 远端客户端使用同一 Pawn 的 `RemotePresentation` 模式，仅插值服务器 Accepted Pose，不运行 StateTree、攻击、路径或本地候选模拟。
- 服务器战斗配置、载具源或可靠 ACK 暂时缺失时，本机 Owner Pawn 保留最后可用配置并继续本地飞行；战斗授权可以暂停，Movement 和 StateTree 不能因此暂停。真正断线后，在本地 World 被销毁前，本机 Pawn 仍继续模拟；其他客户端只因收不到新的 Accepted Pose 而冻结并淡出。

### R2：逐架行为和三维移动

- 本机 Pawn 各自运行 UE 原生 `UStateTreeComponent` 资产，行为优先级为 `Dead → EmergencyAvoid → GroundAttack → AirAttack → Rejoin → EscortOrbit`；不存在等待服务器或所有者状态导致的停机行为。
- StateTree 只选择行为并输出移动请求。MovementComponent 是 Transform 和 Velocity 的唯一写者；SimulationSubsystem 按完整成员身份稳定排序，以 30Hz 固定步长推进，每帧最多 4 步。
- 正常飞行保持最低速度、有限转弯率和倾斜角。移动使用客户端 FlightNav、WorldStatic/WorldDynamic 球体扫掠和母舰距离软约束。
- 僚机之间不做碰撞、扫掠阻挡、邻机分离或避让；僚机 Pawn 忽略 `ECC_Pawn`，避障探针显式忽略其他 `AGuLiWingmanPawn`。
- 每 Flight 的异步 FlightNav 请求由 SimulationSubsystem 共享协调，不能为 25 架重复提交等价请求。

### R3：阻挡恢复和攻击连续性

- 一次期望位移被阻挡时，按稳定顺序尝试扫掠滑移、有限三维方向采样、紧急爬升、返回最近安全点和短暂悬停转向。
- 连续 0.5 秒无有效进展时由客户端在已验证安全点、编队环候选与稳定方向中执行单成员本地安全重定位，并立即以最低飞行速度进入 `Rejoin`；连续 0.5 秒恢复正常移动后清空恢复状态。
- 冻结轰炸轮遇到真实移动死锁时取消该轮及其 Checkpoint，恢复后根据当前权威分配重新开始，不能在原地永久保留冻结轮。
- 单轮没有可用恢复点时不得进入永久 `Stale` 或等待服务器；下一固定步继续更换稳定角度和高度候选，存活本机 Pawn 始终保持恢复尝试。

### R4：Actor 表现容量

- Owner 直接显示模拟 Pawn。远端 Pawn 使用现有 4 帧缓存、0.1 秒回看、0.5 秒外推和 0.15 秒淡出语义。
- 表现层保留最多 128 组轻量快照；同时激活最多 200 架 Actor，其中 25 架为本机 AI、最多 175 架为远端插值 Actor。
- 远端 Actor 按观察距离和稳定成员身份选择；超过池容量的远端成员只保留快照数据。
- 僚机不再使用 Owner/Remote ISM、远端 Mass Mirror 或僚机 Mass Archetype。Commander 继续使用 Mass，项目模块仍保留其 Mass 依赖。

### R5：v13 姿态转发与兼容合同

- 协议保持 v13；已实现的紧急重定位原因、请求、响应和 `RebasedMemberMask` 保留序列化兼容，但正常运行不再自动发起服务器重定位事务。
- 普通 Candidate 仅校验 v13 协议、当前拥有者/租约、连接代次、递增 Flight 帧号、数据结构和完整存活成员身份。Roster/能力/编队/FlightNav/障碍版本、上传 Grant、上一 AcceptedSequence、服务器 World Sweep、运动包络和 Carrier 历史均不是正常姿态的接纳门。
- 同一 Flight 的新帧采用 latest-wins。丢包、乱序或迟到 ACK 只能丢弃旧帧，不能使后续新帧持续被旧基线卡住。
- 服务器 Accepted Pose 原样携带客户端的位置、旋转、速度与诊断元数据，供其他客户端插值；服务器僚机 Transform 写入计数必须始终为零。
- 服务器仍按连接心跳或任一 Accepted Pose 判断连接活性，并在实际 Socket Logout 时立即处理失联；不得从低速、导航失败或单 Flight 姿态年龄推断本机应停止飞行。

### R6：战斗与联网兼容

- 服务器继续独立为每艘 Ship 运行统一空地匈牙利分配，校验 Candidate 的传输身份以及 FireIntent 和实际伤害；客户端承担运动和攻击状态机模拟。
- 有效手动目标继续覆盖自动分配。自动分到空中目标时执行现有对空攻击，分到地面目标时执行现有俯冲轰炸。
- Legacy 自动武器、逐成员目标版本、冻结攻击轮、开火授权历史和 Ship 索敌范围显示保持原语义。可靠战斗状态未确认时客户端可以继续飞行，但不得绕过服务器开火授权。

## 边界

- 不在服务器创建或运行僚机 Pawn、StateTree、MovementComponent。
- 不把 Pawn 设置为玩家可操控 Character，不引入 `CharacterMovementComponent`、NavMesh 地面寻路或通用 AIController。
- 不让服务器运行僚机导航、避障、轨迹复演、速度包络或位置纠正；服务器只转发当前租约的结构合法姿态。
- 不做僚机之间的碰撞、分离和避让；世界障碍物避让仍由每架本机 Pawn 客户端完成。
- 不修改 Commander 的 Mass 实现，不移除项目级 Mass 模块依赖。
- 不改变匈牙利代价、0.2 秒扫描、空地目标池、伤害预算或跨 Ship 协调规则。
- 不读取或重写现有二进制关卡内容；本工作项只引用已存在的僚机网格和行为资产路径。

## 验收标准

- [x] 本机租约拥有者稳定创建 25 架身份唯一的 Owner Pawn；死亡、补充换代、Bootstrap、Resume 和 Takeover 后数量与身份正确，Dedicated Server 为零僚机 Pawn。
- [x] 远端表现完全由轻量 Actor 池承担，最多 175 架远端 Actor；僚机源码不再引用 Mass Entity、Fragment、Processor、Archetype 或 ISM。
- [ ] 每架 Owner Pawn 运行自己的 UE 原生 StateTree，30Hz 固定步由 MovementComponent 唯一写入位姿，并正确执行环绕、归队、空战和轰炸；服务端状态、ACK 或配置短暂缺失不得停止本机飞行。
- [x] 球体扫掠、滑移、三维采样、紧急爬升、返回安全点和短暂悬停可处理动态阻挡、FlightNav 边缘及凹角。
- [x] 僚机彼此重叠或交叉时不发生碰撞、分离或停飞；WorldStatic/WorldDynamic 障碍仍触发逐架客户端避让。
- [x] v13 普通姿态拒绝错误租约、错误连接、旧帧和无效成员身份，并容忍 ACK 基线、上传 Grant、Roster/能力/导航元数据暂时错位；服务器 World/Carrier 校验调用为零。
- [ ] 25 架同时拥有合法目标时全部参战；持续目标场景中每架至少完成两轮攻击，手动目标、冻结俯冲和空地混合保持正确。
- [x] 300 秒目标关卡 PIE 中 25 架持续存在且 Relay 保持 Active；Ship 持续改变 position 和 rotation 时，任何存活、可用、可见 Owner Pawn 不得连续 5 秒位移不足 100 厘米，普通与原子 Candidate 拒绝数为零，服务器 Wingman Movement 写入为零。
- [x] 25 架本机 StateTree/30Hz 模拟加 175 架远端插值时固定步不丢失，僚机客户端更新 P95 不超过 8ms、最大值不超过 16ms。
- [x] 直接相关自动化、源码版 `GuLiStrikeEditor` 和 `GuLiStrike` Win64 Development 构建通过，项目与涉及插件的 BuildId 和源码版引擎一致。

## 验收结果（2026-09-09）

已完成 8/10 项。300秒双进程PIE中，两端各25架Owner Pawn与StateTree全程存在，静止违规、普通/原子Candidate拒绝和服务器Wingman位移写入均为0；两艘Ship通过真实MovementComponent输入持续改变位置与旋转。对地、手动目标、冻结俯冲和空地混合由自动化覆盖。

未勾选的两项都受同一空战问题影响：虽然两端25架均获得合法目标并进入攻击行为，300秒内达到至少两轮有效开火的稳定槽位只有主机17个、远端7个。用户决定本工作项暂不处理该空战射界循环，结果以 `partial` 归档。

## 关联

- [技术方案](../DevelopmentDocumentation/20260909-Ship僚机客户端Pawn与逐架StateTree重构.md)
- [前置统一选敌开发案](../DevelopmentDocumentation/20260908-Ship僚机空地统一匈牙利自动选敌.md)
- [当前飞船玩法](../Gameplay/飞船.md)
- [实施归档](../Archive/20260909-Ship僚机客户端Pawn与逐架StateTree重构.md)
