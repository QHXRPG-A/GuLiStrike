---
schema: guli-progress/v1
id: DEV-20260902-001-D02
work_id: WORK-20260902-001
kind: development
role: detail
title: 僚机体系、空中三维导航与客户端校验转发 — 技术方案 · FlightNav 与客户端模拟
areas:
- wingman
- commander
- ship
- network
- combat
status: done
verification: passed
created: '2026-09-02'
updated: '2026-09-07'
summary: 玩法层以FGuLiWingmanPathEnvelope关联MatchEpoch、Group/Generation、Flight、Lease、Roster和RequestSerial。异步结果回到游戏线程后同时核对导航版本和完整Envelope；任一不匹配即丢弃
next_action: ''
relations:
  parent: DEV-20260902-001
status_note: 既定实现已收尾（Reward经济规则待玩法配置；源码Dedicated、完整Server Cook、20个真实Dedicated角色、2个拆分H4000角色及S9双10分钟运行已有通过记录；未追加的性能数值阈值不声明通过）
split_order: 2
split_segment_sha256: 937c6fbed5959198e07df035e7c479e6fb61018b71bb7668142dac21b34b076d
---

# 僚机体系、空中三维导航与客户端校验转发 — 技术方案 · FlightNav 与客户端模拟

<!-- guli-progress:split-content -->
## 3. 三维Flight Navigation

### 3.1 Runtime公共类型

- `AGuLiFlightNavVolume`
  - 轴对齐飞行Bounds、Agent Profile、最小叶尺寸、最大深度、FlightNavigation碰撞通道。
  - 硬引用本地图`UGuLiFlightNavDataAsset`。
  - 首版Actor Rotation非零时Bake/Validate失败。
- `UGuLiFlightNavDataAsset : UPrimaryDataAsset`
  - Header：SchemaVersion、SourceMap、VolumeGuid、Bounds、AgentProfile、SourceSignature、SettingsHash、PayloadChecksum、节点/Portal/连通分量和烘焙统计。
  - Payload：扁平自由叶、Portal、净空、连通分量和紧凑索引；不序列化运行时指针。
- `UGuLiFlightNavWorldSubsystem`
  - Client Owner运行异步A*。
  - Server只加载相同只读资产供Candidate验证，不请求路径。
- `FGuLiFlightPathRequest`
  - Start、Goal、AgentProfileHash、VolumeGuid、NavChecksum、SettingsHash、OverlayRevision、OpaqueRequestToken、MaxVisitedNodes。
- `FGuLiFlightPathResult`
  - Status、OpaqueRequestToken、PortalCorridor、SmoothedPoints、VisitedNodes、ElapsedMicroseconds。

玩法层以`FGuLiWingmanPathEnvelope`关联MatchEpoch、Group/Generation、Flight、Lease、Roster和RequestSerial。异步结果回到游戏线程后同时核对导航版本和完整Envelope；任一不匹配即丢弃。

### 3.2 Bake与签名

Bake仅采集Volume内满足以下条件的组件：

- Static Mobility；
- 非EditorOnly；
- 在FlightNavigation通道Block；
- 具有有效碰撞几何。

签名包含稳定Actor/Component路径、组件GUID、Transform、Bounds、BodySetup/CollisionProfile和源资源标识；Volume、Agent和细分设置进入SettingsHash。输入按稳定键排序，不包含时间戳、地址和容器迭代顺序。

算法：

1. 从Volume根节点递归八分，障碍按Agent半径膨胀。
2. 完全自由保存自由叶，完全占用丢弃，混合节点继续细分。
3. 只为共享有效面的自由叶建立Portal；仅边/角接触不连通。
4. 保存Portal中心、有效矩形、最小净空和连通分量。
5. A*使用欧氏启发和稳定NodeId Tie-break。
6. 成功路径经Portal Corridor和LOS平滑，不能越出净空。

Flight共享Corridor。客户端每帧新请求预算默认4个，个人恢复路径默认2个；工作线程只读不可变Data/Overlay，不访问UObject、World、Physics或NavigationSystem。

### 3.3 Editor流程

- Details提供Bake、Validate、Clear。
- Bake成功后立即回读并校验Header/Payload checksum。
- 修改静态碰撞、Transform、BodySetup、Volume或Agent后Validate显示Stale。
- 打开地图、启动编辑器、PIE、Standalone和正式运行只加载资产，静态扫描/Build次数必须为0。
- Cook对Missing、Stale、SchemaMismatch、ChecksumMismatch直接失败。
- Runtime错误时禁止生成Wingman，不能回退到运行时构建或直线穿墙。
- 分别为`LVL_CommanderMassPrototype`、`LVL_Main`、`LVL_ShipTest`生成独立资产。

### 3.4 客户端路径与服务端验证

- A*、Corridor选择、平滑、局部避障和重新规划只在Lease Owner客户端运行。
- Candidate携带NavSchema/Checksum、CurrentLeaf、CurrentPortal、CorridorRevision和动态ObstacleRevision。
- 服务器不重跑A*，只检查：
  - 版本匹配；
  - 当前Leaf包含成员位置；
  - Portal相邻且净空足够；
  - Trail各段没有跨越不可达区域；
  - 静态/当前动态Sweep不阻挡。
- 动态障碍Overlay由障碍注册、移动超过阈值或拓扑变化时事件驱动更新，最高2Hz；不固定5Hz重建静态树。
- 小物体和Wingman间冲突由客户端本地三维哈希处理。

## 4. Wingman客户端模拟

### 4.1 身份与编制

- 一个Carrier对应一个`SimulationGroupId + GroupGeneration`、25架、5个Flight和一份Lease。
- 成员使用`WingmanId + EntityGeneration`，本局不复用。
- 每个RosterRevision冻结`RequiredActiveFlightMask`和每Flight的`RequiredActiveMemberMask`。
- 死亡先可靠移出Mask；空Flight不提交空包。补充先可靠加入新身份，再进入下一Candidate。
- 正常Active Flight Candidate独立接纳；首次Bootstrap、进入Stale后的Resume和换主后的Takeover必须原子覆盖全部Required Flight。

### 4.2 Mass Fragment与Tag

| Fragment/Tag | 关键字段 | 端别 |
|---|---|---|
| Identity | Group/Generation、Roster、WingmanId、EntityGeneration、Flight/Slot、Team | 服务器创建，客户端镜像 |
| LeaseMirror | AuthorityEpoch、LeaseId、Availability、BaselineRevision | Owner客户端只读 |
| HealthMirror | Health、Dead、LifeRevision | 服务器写，客户端镜像 |
| ClientKinematics | Position、Velocity、Quaternion、Bank、LastSafe | 仅Owner客户端 |
| FormationBehavior | Ring、Phase、FlightMode、Threat、Guidance | 仅Owner客户端 |
| FlightPath | Corridor、Leaf、Portal、Nav/ObstacleRevision | 仅Owner客户端 |
| AcceptedSnapshotMirror | AcceptedSequence、Position/Velocity/Quaternion、AcceptedServerTime | 服务器存储，远端只读；SnapshotAge仅在读取时派生 |
| `FGuLiWingmanClientSimulationTag` | StateTree、Path、Steering、Integration查询 | Owner客户端 |
| `FGuLiWingmanRemotePresentationTag` | 插值、外推、ISM更新 | 非Owner客户端/观战者 |
| `FGuLiWingmanSharedUnavailableTag` | Initializing/Stale/Unavailable时禁止共享交互和远端显示；不单独排除Owner私有初始化/恢复模拟 | 所有客户端按服务器Availability |
| `FGuLiWingmanPrivateBootstrapOrResumeTag` | 从Spawn/冻结Baseline运行私有30Hz，只可生成全组Bootstrap/Resume/Takeover Candidate | 当前Lease Owner |

ClientSimulation/PrivateBootstrapOrResume和RemotePresentation互斥。Initializing/Stale/Unavailable时当前Lease Owner可从已确认Baseline运行私有初始化/恢复模拟，但首个全组Bootstrap/Resume/Takeover接纳前不得Store普通单Flight状态、Relay、显示或参与战斗。Dedicated Server不创建或运行ClientKinematics、StateTree、Path、Steering、FlightIntegration Entity管线。

### 4.3 StateTree

客户端StateTree状态：

- JoiningEscort
- EscortOrbit
- EmergencyAvoid
- OwnerUnavailable
- Dead

信号：

- OwnerReady/OwnerLost
- PathReady/PathFailed
- CollisionThreat/ThreatCleared
- HealthZero
- LeaseGranted/LeaseRevoked
- RebaseRequired

[UE5.7 MassGameplay官方说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-mass-gameplay-in-unreal-engine?application_version=5.7)描述了Signal驱动Mass StateTree的运行方式，适合低频模式选择。项目必须实现带Client ExecutionFlags的专用Mass StateTree Processor，并自动化证明NM_Client Owner执行、Dedicated Server和Remote Presentation不执行。

StateTree Task只修改FlightMode、请求Guidance/Path或发信号；不逐Tick写Transform、不直接扣血。固定翼积分Processor是客户端Transform唯一写者。

### 4.4 客户端30Hz Processor顺序

1. `WingmanLeaseAndRosterConsumeProcessor`
2. `WingmanStateTreeSignalProcessor`
3. `CarrierAnchorProcessor`
4. `FormationAndPathResultProcessor`
5. `WingmanAvoidanceAndSteeringProcessor`
6. `WingmanFlightIntegrationProcessor`
7. `WingmanLocalTargetAcquireProcessor`，默认5Hz错峰
8. `WingmanFireIntentProcessor`
9. `WingmanCandidateCaptureProcessor`

`WingmanCandidateCaptureProcessor`在Cruise每6个ClientSimTick捕获一次；获得服务器短期`HighRate10Hz`授权后，在Combat/EmergencyAvoid/近障碍每3 Tick捕获一次。客户端可以申请高频档，但不能凭自报`SubmissionMode`自行取得服务器10Hz预算。模式切换记录`SUBMISSION_MODE_CHANGED`；不得用渲染帧Delta控制捕获。

### 4.5 固定翼积分

每个30Hz固定步：

1. 计算母舰预测Anchor和Flight环形Guidance。
2. 合成Corridor、槽位、邻居分离、碰撞预测和紧急拉升方向。
3. 将期望速度限制在Min/Cruise/CatchUp。
4. 按Acceleration/Deceleration限制速度变化。
5. 按MaxTurnRate限制速度方向变化。
6. `Position += Velocity * FixedDt`。
7. 前向轴对准Velocity，Pitch/Yaw来自航向，Roll根据横向加速度并限制MaxBank。
8. 更新LastSafe状态和路径摘要。

禁止直接插值Position到槽位、停速等待、逐架Chaos或由Representation反写逻辑Transform。

