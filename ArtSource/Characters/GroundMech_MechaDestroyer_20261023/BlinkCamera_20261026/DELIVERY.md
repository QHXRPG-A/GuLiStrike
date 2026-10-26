> 2026-10-26 本会话已按用户指令完成归档。以下保留当时版本及审核事实，最终方向/时长与清理说明见[完成归档](../../../../Progress/Archive/20261026-地面机甲镜头方向闪现与会话完成归档.md)；不补记未执行的视觉验收。

# 闪现镜头阻尼与三阶段动作

用户要求镜头不要随闪现直接拉走，要看清残影、轨迹及运动过程；随后要求 Begin / Loop / End 三阶段。最新明确改为 **0.1 / 0.8 / 0.2 秒，总时长1.1秒**，覆盖首版1.5秒分配。

| 阶段 | 时长 | 行为 |
|---|---:|---|
| Begin | 0.10 s | 双臂逐渐后展、上身前倾；移动步态逐渐收住，地面脚部继续支撑 |
| Loop | 0.80 s | 保持后展滑行姿态并轻微摆动；沿选定方向用平滑曲线完成位移，每固定步胶囊扫掠 |
| End | 0.20 s | 平滑收势回到基础姿态，地面 IK 恢复；结束后恢复移动/射击/转向 |

这是现有 ABP 的程序姿态动画三阶段，没有伪称新建三个 AnimSequence。Begin/End 使用五次平滑曲线，Loop 周期摆动在两端位置和导数归零。保留骨长和下身骨架；根位移仍由 Ground 模拟唯一执行。受到碰撞可提前止住实际位移，但动作时钟仍完整播放三段，不穿过阻挡物。距离1200cm、冷却2s、原伤害与消耗规则保持。输入格式及共享控制格式仍为5；Ground状态9→10、移动姿态格式5→6，防止旧端将Loop解释为恢复阶段。

镜头对每次实际闪现位移施加世界空间补偿，先停留0.04s，再由临界阻尼追赶；`BlinkCatchupSeconds=0.45` 表示目标停下后、零初速度情况下误差收敛95%的时间，并非0.45s硬切完成。通过SpringArm SocketOffset施加，碰撞检测仍从实际机体根节点出发。普通移动与鼠标转镜没有启用全局相机迟滞；右键构图、缩放及避障保留。解除控制、重生/恢复及其他外部位移清理镜头旧偏移。镜头状态是本地表现，不写入网络姿态。

残影按经过的路程分批生成，冻结在各自世界坐标；遇阻挡不在未到达的路径上补造残影。保留3道、0.38s寿命和阵营配色。未修改命中特效、弹道、材质或机甲几何。

## 唯一数值入口

[GuLiStrikeMech.xlsx](../../../../Data/Excel/GuLiStrikeMech.xlsx)：

- `Mobility.Default`：`BlinkWindupSeconds=.1`、`BlinkLoopSeconds=.8`、`BlinkRecoverySeconds=.2`、`BlinkLoopSwayDegrees=3`。
- `Camera.Free/Aim`：`BlinkHoldSeconds=.04`、`BlinkCatchupSeconds=.45`。

参数均经过现有Excel→JSON/CSV→生成行结构→UE DataTable；导出器和运行配置同时校验三段时间、冷却下限和镜头阻尼范围。

## 已执行的技术核对

- 最新1.1秒修订：只改Mobility三项时长，生成行结构摘要不变，无需编译。结束用户当前PIE后完成单表导入保存，UE读回0.1/0.8/0.2；两份清单通过校验，当前指纹 `f1f4349e74ef3b47355d2f2ac71e0426`。原地图8实体保留并保存，没有重启PIE或烘焙动画。证据：[导出核对](Timing_1p1/export_readback.json)、[编辑器读回](Timing_1p1/editor_readback.json)。以下为首版构建及结构核对记录。
- 源码 `GuLiStrikeEditor Win64 Development` 授权构建成功，首轮48.90s，外部传送中断补充后末轮30.80s，均退出0；引擎 `D:/UnrealEngine-5.7`，项目/引擎BuildId同为 `d4757e21-eaef-492e-b079-c8c0056c493d`。最终编辑器进程32180，重开后直接冷读配置和动画图通过。
- 冷启动先直接读取保存的ABP：73节点、`BlinkPose`接线有效、新阶段属性可读；未重建图。导入仅选Mobility/Camera两表，未重制移动动画或重烘支撑曲线。
- Excel生成值与UE保存的表、运行配置一致；配置指纹 `a4575f32efcd0ecd6b9f1dcccdf42449`，200/600清单均重导出并通过编辑器校验，GroundPlayer状态10。
- 离线公式核对三阶段共1.5s、姿态权重在0.4/0.9s交界连续、无阻挡总行程1200cm；临界阻尼解析积分分为30/60/144段结果一致。它不替代运行时帧率、碰撞或视觉验证。
- 原地图8个GroundMobility实体与Ground玩家入口读回正确并保存，脏地图/内容包均为空。
- **未启动PIE、未运行游戏自动测试、未截图或目检。动作、镜头舒适度和残影可读性待用户验收。**

证据：[首轮构建](build.json)、[最终构建](build_final.json)、[BuildId](build_ids.json)、[冷读动画图](cold_anim_graph.json)、[最终编辑器读回](final_editor_readback.json)、[导入](import.json)、[完整资产读回](asset_readback.json)、[公式计算](analytical_readback.json)、[地图实体与清单](scene_readback.json)。

## 玩家入口

地图 `/Game/Maps/LVL_CommanderMassPrototype`，本地PIE主角色Ground，角色 `BP_GroundMech_Light`、动画 `ABP_GroundMech_A4B2`。复用现有验证区：平地中心约(-4000,50000,905)，`GroundMobility_Entry`为定位点(-2800,50000,1149.04)，不是替换出生点；坡面与20/40/60/80cm台阶保留。

由用户启动PIE：空旷处按Shift观察0.1s蓄势→0.8s位移→0.2s收势；W/A/S/D分别选择方向，长按不连发。行走中触发、右键与普通镜头各触发一次，核对镜头平滑追赶且残影留在路上；靠现有阻挡物触发时核对停止位置；冷却后再次触发及空中触发，核对动作恢复。反馈重点为三段节奏、镜头缓冲强弱及轨迹是否看得清。
