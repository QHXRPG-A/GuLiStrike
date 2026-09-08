---
schema: guli-progress/v1
id: ARC-20260905-001
work_id: ''
kind: archive
role: root
title: 僚机 FlightNav 弧线验证与局部恢复
areas:
- wingman
- commander
- network
- assets
status: recorded
verification: partial
created: '2026-09-05'
updated: '2026-09-05'
summary: 初始版本有两个连续阶段：僚机先成组向母舰内侧掉落，随后某些 Flight 不再移动；Candidate 长时间没有新接纳后，租约进入 Stale/Unavailable，旧表现超时策略又把存活僚机隐藏，所以最终看起来像“全部消失”
next_action: ''
relations:
  work_items:
  - WORK-20260904-002
status_note: ''
---

# 僚机 FlightNav 弧线验证与局部恢复

- 性质：SwarmOrbit 第一套跟随方案的 PIE 缺陷增量归档
- 结论：停飞、整组坠向母舰和断流消失的同一故障链已修复；底层八叉树、Portal、A* 和烘焙资产无需更换
- 完整功能总归档：等待五项新增测试与 v8 GoldenBytes 迁移获授权后再写

## 1. 玩家看到的现象

初始版本有两个连续阶段：僚机先成组向母舰内侧掉落，随后某些 Flight 不再移动；Candidate 长时间没有新接纳后，租约进入 Stale/Unavailable，旧表现超时策略又把存活僚机隐藏，所以最终看起来像“全部消失”。

它不是 Curl Noise 或盘旋半径公式单独造成的，也不是八叉树和 A* 数据整体损坏。真正问题位于固定翼积分、安全策略和网络轨迹验证之间的接口。

## 2. 根因链

1. **最终门制造非法速度跳变**：一步 FlightNav 检查失败时，Integration 把约 `44 m/s` 立即钳成 `30 m/s`。服务器有限加减速门记录到 `SpeedDelta actual=1423.84 cm/s, permitted=170 cm/s` 并拒绝 Candidate。
2. **单机风险被扩大为整组恢复**：BehaviorPolicy 对一个 Flight 的 Emergency 做组级 OR，StateTree 把同组五架全部改成 Recover；视觉上就是一组一起往母舰掉。
3. **只知道通不通，不知道还能飞多远**：旧 heading probe 缺少连续净空估计，起步又直接写巡航速度，固定翼来不及在边界或 Portal 拐角前刹车。
4. **服务器验证了不存在的直线**：客户端 30Hz 走的是合法有限转弯弧线，普通 Candidate 过去仅以 5Hz 发首尾点。服务器把 0.2 秒弧线当直线弦，弦切出安全走廊时得到 `MissingLink`，造成持续假拒绝、accepted sequence 停滞和租约断流。

## 3. 修复内容

- Safety Resolver 分成战略安全方向和下一固定步安全方向，Integration 只采用已经过当前步 FlightNav 检查的方向。
- 对射线离开导航域的位置做 7 次有界二分，得到实际剩余净空；前视距离至少覆盖 `速度² / 2a + 安全余量`，并发布动态速度上限。
- 净空不足时按配置减速度逐步刹车，必要时允许安全降到 0；最终门失败恢复上一合法速度，不再瞬时跳到常态最低速度。
- SwarmOrbit 从静止开始，由唯一 Integration 在有限加速度范围内起飞。
- Emergency 只在具体成员没有安全下一步时成立；只有受影响成员 Recover，健康成员继续按自身状态 Orbit/CatchUp/Recover。
- Relay 复用协议 v8 已有 `TrailSamples`：Active 阶段以 15Hz 留存轨迹，普通 5Hz/授权 10Hz Candidate 带上实际中间点，服务器逐段验证真实弧线。没有新增协议字段和协议版本。
- 新增仅由 `-GuLiWingmanValidationTrace` 开启的 Integration 拒绝诊断，默认无日志噪声。

代码落点：

- `Source/GuLiStrike/Gameplay/Wingman/Mass/GuLiWingmanMassFragments.h`
- `Source/GuLiStrike/Gameplay/Wingman/Mass/GuLiWingmanMassProcessors.h/.cpp`
- `Source/GuLiStrike/Gameplay/Wingman/GuLiWingmanSimulationSubsystem.cpp`
- `Source/GuLiStrike/Battle/Network/Relay/GuLiWingmanRelayComponent.h/.cpp`

## 4. 为什么不换八叉树 + A*

90 秒定向运行中，服务器对携带真实弧线后的 FlightNav 世界门达到 `2244/2244` 接纳，Navigation/Static/Dynamic 拒绝均为 0，说明导航数据能正确表达这些安全路径。之前的 `MissingLink` 来自把弧线错误简化为直线弦，而不是 Portal 连通性本身失效。

因此维持既定架构：常态 Orbit/Follow 使用局部 FlightNav 探针，不跑 A*；只有 CatchUp/Recover 才按 Flight 异步寻路到稳定恢复锚点。A* 只提供走廊，不替代逐架固定翼积分和最终安全门。

## 5. 验证证据

### 5.1 运行时复现

- 35 秒：5 个 Flight accepted sequence `[174,174,174,174,173]`；Candidate `864/864`、世界门 `869/869` 接纳；`spatial_rejects=0`、`integration_rejects=0`；生命周期 Active。
- 90 秒：25 个 Mass entity 保持存在，最终 mode mask 仅 Orbit；5 个 Flight accepted sequence `[449,449,449,449,448]`；Candidate `2239/2239`、世界门 `2244/2244` 接纳；Navigation/Static/Dynamic/Integration 拒绝均为 0；生命周期 Active、没有 Revoked。

对应日志：

- `TestResults/SwarmOrbit/CurrentPieBugNormalTrail35s.log`
- `TestResults/SwarmOrbit/CurrentPieBugNormalTrail90s.log`

### 5.2 既有自动化与性能

- Avoidance：6/6
- StateTree：3/3
- Mass：5/5
- Relay：21/21
- Performance：5/5
- H4000 OwnerSimulation P95：`15.1883 ms`，预算 `33.333 ms`
- H4000 ServerValidator P95：`1.3659 ms`，预算 `2.5 ms`
- Network 当前功能相关 10 项通过；旧 `GuLiStrike.Wingman.Network.ProtocolV7.GoldenBytes` 仍因 v8 输出首字节 `10` 与旧期望 `0E` 不同而失败

本轮没有新增或修改任何测试文件，也没有擅自迁移 golden bytes。

## 6. 未完成边界

- 第五套专项测试仍需用户授权：确定性、长跑半径带、固定翼可达性、原子 Formation 切换、瞬态租约可见性。
- 仍需交互 PIE 验证移动母舰的连续加速/急停/QE 转向、真实障碍近飞，以及多人远端观察者的视觉一致性。
- 本文是故障修复增量归档，不替代 SwarmOrbit 功能最终总归档。

## 7. 关联文档

- [开发文档](../DevelopmentDocumentation/20260904-僚机无规则护航盘旋技能重构.md)
- [需求文档](../RequirementDocument/20260904-僚机无规则护航盘旋技能重构.md)
- [飞船玩法记录](../Gameplay/飞船.md)

## 8. 僚机视觉机头反向最小修复

### 8.1 首个错误转换

逻辑层的 `FTransform` 已用局部 `+X` 对准速度，Candidate/Accepted Snapshot 也保持该姿态；玩家看到“倒车”发生在最终 HISM 网格提交阶段。当前 `SM_Wingman_Mass` 的可见机头实际朝局部 `-X`，直接使用逻辑旋转后自然会让机尾指向速度方向。

### 8.2 修改边界

仅修改 `Source/GuLiStrike/Gameplay/Wingman/Presentation/GuLiWingmanPresentationActor.cpp`：在 `UpdateInstance` 中把逻辑旋转右乘一个局部 Yaw 180°修正，再提交 Owner/Remote HISM。`Track.PresentedTransform`、Remote Mass镜像、速度、固定翼积分、武器、导航和网络快照均保持原值。

### 8.3 验证

- 源码版引擎：`D:\UnrealEngine-5.7`
- 构建：`GuLiStrikeEditor Win64 Development`，退出码 0
- 既有 `GuLiStrike.Wingman.Presentation`：7/7 通过、0 失败；日志为 `TestResults/SwarmOrbit/WingmanPresentationFacingFixTests.log`
- 引擎、项目和 `GuLiFlightNavigation` 的 `UnrealEditor.modules` BuildId 均为 `9872a344-2108-49e2-b40c-34c4e2c3ccfe`
- 未新增或修改测试源码；最终玩家目视方向仍以重开 PIE 后的当前网格为准
