---
schema: guli-progress/v1
id: ARC-20260908-002
work_id: ''
kind: archive
role: root
title: 2026-09-08 修复了 Ship 僚机在 FlightNav 边界停住不动
areas:
- wingman
- ship
- navigation
- network
categories:
- gameplay
status: recorded
verification: passed
created: '2026-09-08'
updated: '2026-09-08'
summary: 僚机在最后合法点刹停后可瞬时对准冻结逃逸方向；源码版构建、攻击与Relay专项、主动对空250.63秒和对地85.83秒逐架采样均通过，最长停滞0秒。
next_action: ''
relations:
  work_items:
  - WORK-20260907-001
  - WORK-20260908-001
status_note: passed仅覆盖本次停滞恢复补丁及已授权验证；未迁移的protocol-v7旧测试夹具与既有AuthorityRegistry测试崩溃仍按原记录保留。
---

# 2026-09-08：修复了 Ship 僚机在 FlightNav 边界停住不动

## 变更清单

| 文件 | 变更 |
|---|---|
| `Source/GuLiStrike/Gameplay/Wingman/Mass/GuLiWingmanMassFragments.h` | 保存逐机冻结逃逸方向及有效标记 |
| `Source/GuLiStrike/Gameplay/Wingman/Mass/GuLiWingmanMassProcessors.cpp` | 拒绝航向后冻结反向、刹停时继续消费固定步、停稳后瞬时原地对准、下一位移继续精确校验 |
| `Source/GuLiStrike/Battle/Contracts/GuLiWingmanProtocolTypes.h` | 客户端与服务器共享距Ship运动包络常量 |
| `Source/GuLiStrike/Battle/Relay/GuLiWingmanRelayServer.cpp` | 只接纳严格受限的Recover静止刹停样本，其余运动与世界门保持 |
| `Source/GuLiStrike/Battle/Combat/Tests/GuLiWingmanAttackCombatTests.cpp` | 已授权PIE QA目标入口支持Standalone/Listen权威World，用于当前关卡主动攻击复现 |
| `TestResults/ShipAirCombatLevel/capture_active_*_stoppage.py` | 逐架采集Owner HISM的世界位置、姿态与游戏时间 |

## 决策与实现

停住不动由两个连续问题造成。Integration 的最终世界门拒绝一步越界位移后，旧逻辑把位置留在最后合法点，却继续携带非零速度，并清空该成员固定步余量后退出循环。编队候选使用共享 `ClientSimulationTick`，因此单个成员会相对逻辑钟落后；之后的刹车或追赶会被服务器判成不可能的速度/积分变化，Relay拒绝后表现层长期冻结。

第一层修复让被拒成员沿当前方向按最大减速度刹停，同时消费当帧全部已安排固定步，不再产生逐机时钟漂移。服务器只对 `Recover`、位置变化不超过2cm、速度没有增加超过量化容差的样本跳过恒速位置预测，速度、加速度、转向、FlightNav、物理Sweep和距Ship包络仍逐段验证。

继续实跑发现第二层停滞：恢复锚点可退化为当前边界位置，或者有限转向需要长时间才能形成可执行离开段。现在预测到当前航向不可执行时先冻结其反向；若最终段真的被拒，则用实际被拒航向的反向覆盖。速度降为0后，僚机允许原地瞬时旋转到冻结逃逸方向。该帧没有平移，下一帧只有真实移动段通过全部安全门才提交，因此优先解决玩家可见的“停住不动”，而不放宽穿越导航边界或障碍的条件。

## 验证

- 使用源码引擎 `D:\UnrealEngine-5.7` 构建 `GuLiStrikeEditor Win64 Development` 成功，退出码0。
- [攻击自动化](../../TestResults/ShipAirCombatLevel/AutomationStoppedRecoveryFinal_Attack_20260908/index.json) 7/7；[Relay运动包络](../../TestResults/ShipAirCombatLevel/AutomationStoppedRecoveryFinal_RelayEnvelope_20260908/index.json) 1/1。
- [主动对空分析](../../TestResults/ShipAirCombatLevel/active_dogfight_stoppage_analysis.json)：250.63秒、690个采样点；25架均在689/689个相邻区间移动，最长停滞0秒。末端目标为Air，累计3306发机枪、3420次伤害提交，Relay Active，普通/原子拒绝0，服务器运动写入0。
- [主动对地分析](../../TestResults/ShipAirCombatLevel/active_ground_stoppage_analysis.json)：85.83秒、300个采样点；25架均在299/299个相邻区间移动，最长停滞0秒。末端导弹累计445，Relay Active，普通/原子拒绝0。
- 当前运行日志中 `GULI_WINGMAN_INTEGRATION_REJECT`、`GULI_WINGMAN_SPATIAL_REJECT`、`SpeedDelta`、`CarrierDistance`、非零 `dropped_steps` 和Relay撤销计数均为0。PIE已恢复为空中目标并保留运行。

## 遗留问题

整个 `GuLiStrike.Wingman` 历史集合仍有旧protocol-v7 owner-group夹具无法创建当前v11组，以及既有AuthorityRegistry测试空指针崩溃。本次没有扩改这些未获授权的历史测试；它们不改变本次运行时停滞修复的通过结论。

## 关联

- [三维往返缠斗开发文档](../DevelopmentDocumentation/20260907-Ship僚机三维往返缠斗与随机转向.md)
- [空战原型关卡开发文档](../DevelopmentDocumentation/20260908-Ship空中部队原型关卡与三倍航速.md)
- [Ship玩法记录](../Gameplay/飞船.md)
