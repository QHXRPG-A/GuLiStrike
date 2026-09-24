# Commander PIE 卡兵现场诊断（2026-09-24）

## 现场与预期

- 地图：`LVL_CommanderMassPrototype`，双客户端 PIE；权威世界为 `UEDPIE_0`，红方本地客户端为 `UEDPIE_1`。
- 最初红方选中 12 名士兵：153、158、169、155、162、167、157、168、175、164、156、166。客户端反馈“已接收 12，拒绝 0”。服务器保留移动订单，但抽样单位的速度为 0、距目标尚有约 1–2 千厘米，无进展计时约 178–181 秒。
- 预期：有移动订单的单位应继续位移；导航失效时应恢复到可走区域，或给出终止失败状态。
- 观察期间该选择集合缩减，原先多数士兵从当前权威名册消失。因此以下导航引用、失败码和投影量由同时在同一 PIE 中卡住的代表单位复核；最初 12 人的每个失败码没有留存。

## 第一处坏状态与证据

1. 接令成功，服务器 `nav_state=Normal`、`moving=1`、`ActiveOrderId != 0`，但位移停止。权威导航统计曾显示 `alive=600`、`active=558`、`blocked=0`、`surface failed=69623`、`personal_paths=0`、`recovery_pending=0`。
2. 士兵 452：坐标约 `(-1523.5, -40727.2)`，`nav_ref_valid=0`，`failure=SurfaceMoveFailed`，连续表面移动失败 109 次，个人路径查询 0 次。随后同一坐标保持不变；当前网格用 10 cm 水平范围投影失败，扩大到 150 cm 时，最近可走点距原点 27.8 cm。
3. 士兵 630：坐标约 `(40397.5, 41393.7)`，同样 `nav_ref_valid=0`、`SurfaceMoveFailed`，10 cm 投影失败，150 cm 投影的最近可走点距原点 110.5 cm。它距离 `GuLiPlacedBuilding_137` 中心约 398 cm，建筑碰撞半尺寸约 345×305 cm，Commander 导航代理半径为 150 cm。
4. 对照士兵 671 正以约 720 cm/s 移动，`nav_ref_valid=1`、表面失败次数 0。说明导航网格和固定步本身仍在运行，故障集中在局部失效引用和网格边缘。
5. 同场景日志持续记录礼物建筑生成及清场，`GuLiPlacedBuilding_137` 在 04:06:40 生成；建筑的 `NavModifierComponent` 使用 `NavArea_Null`，会改变可走面。

## 代码根因

- `FindMoveAlongCurrentNavigationSurface` 遇到失效节点引用时，要求 10×10 cm 范围内重新投影，且新点水平位移不超过 10 cm。上述 452/630 的实际距离为 27.8/110.5 cm，因而反复返回失败（`GuLiBattleAuthoritySubsystem.cpp:1152`）。
- 表面移动失败后的中心线/个人路径恢复都受 `!Soldier.MoveIntent` 条件限制；当前移动订单带 `MoveIntent`，因此仍保持 `Normal` 状态（`GuLiBattleAuthoritySubsystem.cpp:3814`、`:3823`）。
- `TickNavigationRepairs` 与 `CommitReadyNavigationRepairs` 虽已定义，但 `UGuLiBattleAuthoritySubsystem::Tick` 未调用它们；运行时 `recovery_pending=0`、`personal_paths=0` 与此一致（`GuLiBattleAuthoritySubsystem.cpp:1758`、`:3001`、`:3096`）。
- 建筑 `NavArea_Null` 与 150 cm 导航代理半径使局部可走面退缩；建筑清场按物理碰撞足迹处理单位，并不保证所有后续单位位置保持在重建后的导航网格内（`GuLiPlacedBuilding.cpp:54`、`GuLiGiftBuildingClearance.cpp:115`）。这是触发条件的有力证据，当前无法从只读现场确定每个单位离网格的具体时刻。

## 复核与修复验证

- 在本次 PIE 的服务器世界运行 `gs.GM.Commander.Nav.Soldier 452` 或 `630`，再运行 `gs.GM.Commander.Nav.Stats`；确认有活动订单、无效节点引用、持续 `SurfaceMoveFailed` 且恢复查询保持 0。
- 修复后，在建筑生成、NavMesh 重建和移动命令交叠时重复观察：离开可走面的单位应被安全重定位或进入明确的终止失败状态；不能无限保留 `Normal` 订单并累积无进展时间。
- 回归范围：未受建筑影响的移动、正在战斗的自动推进、移动终点和服务器/客户端端点同步。

本次仅诊断，未修改代码或 PIE 状态。
