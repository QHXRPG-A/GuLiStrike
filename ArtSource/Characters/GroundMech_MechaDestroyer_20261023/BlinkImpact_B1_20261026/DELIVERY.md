> 2026-10-26 本会话已按用户指令完成归档。以下保留当时版本及审核事实，最终方向/时长与清理说明见[完成归档](../../../../Progress/Archive/20261026-地面机甲镜头方向闪现与会话完成归档.md)；不补记未执行的视觉验收。

# 地面机甲：Shift 闪现与原命中特效调整

2026-10-26。代码、表、保存资产及地图实体检查已完成；当前编辑器为源码 UE5.7 新构建。PIE、联机运行与视觉效果待用户验收，助手未启动 PIE、截图或目检。

同日追加保存问题修复：地图内审核样机的预览动画每帧触发dirty，导致保存成功后又提示未保存。已隔离自动预览修改，保留真实编辑的保存标记；新构建进程19988中预览继续运行、地图保持clean。[原因与复验](MapSave_20261026/REPORT.md)。下文24520为此前命中特效交付时的进程记录。

## 命中特效范围纠正

用户要求的是在原命中特效上调整尺寸、颜色和淡出。此前将它改成新的星形爆点属于误解，已撤下正式引用。原 `NS_MachineGunImpact_AllOptimizations` 保持，Ground 专用调整副本为：

`/Game/GuLiStrike/FX/GroundMech/BlinkImpact_B1/NS_GroundMech_Impact_Adjusted`

- 保留 Glow、Flash、RibbonCore、RIbbonTrailFollower、Sparks、Debris 共 6 个发射器；模块顺序、运动、生成及尺寸曲线保持。
- 5 个渲染材质复制原实例及参数、贴图，派生母材质只在原输出后接橙黄色变换和透明度乘法；不重新设计粒子轮廓。
- GuLiStrikeVfx.xlsx 的 GroundMechImpact（ID 54）Scale 从原效果的 2 调为 4，即线性尺寸 ×2；原共用 ID 5 不变。
- 使用子弹暖橙色 `EE9D58`，从生成起 0.20 秒整体淡出并回收。原粒子运动保留，终止时不会追加第二套共用命中。
- 误制的 `NS_GroundMech_Impact_B1` 无资源引用，只作为未采用历史候选保留；不能当作已批准造型。

证据：[当前资产读回](final_asset_readback.json)、[原形态派生结果](preserved_impact_authoring.json)。读回对比原/派生系统 6 组模块、5 组材质标量/向量/贴图覆盖，并确认正式 DataTable 引用与 5 个颜色/淡出绑定。

## Shift 闪现候选

物理键盘 Shift 按下触发一次闪现，取消原键盘疾跑用途。按机体局部 WASD 选择方向，组合方向归一化；无方向输入时向前。沿真实胶囊扫掠限制落点，不穿越阻挡；保留既有普通与 Mass 碰撞入口。没有新增无敌、伤害或燃料消耗。

| Mobility 表参数 | 首版值 |
|---|---:|
| BlinkDistance | 1200 cm |
| BlinkCooldownSeconds | 2 s |
| BlinkWindupSeconds / BlinkRecoverySeconds | 0.12 / 0.20 s |
| BlinkArmBackDegrees / BlinkBodyLeanDegrees | 120° / 18° |
| BlinkAfterimageCount | 3 |
| BlinkAfterimageSeconds / BlinkAfterimageOpacity | 0.38 s / 0.55 |

以上是可调首版实现值，不是用户对技能数值的最终确认。唯一数值入口为 `Data/Excel/GuLiStrikeMech.xlsx` 的 Mobility/Default，继续走标准导出/导入。

双臂后展与上身前倾在现有动画图末端做姿势叠加，没有替换原骨架或动画片段；恢复时混回当前姿态。闪现中暂停转身与开火并退出地面 IK，重新定位后清除旧落脚点，再平滑恢复。空中可用，既有垂直速度及喷气规则保留。三道残影复制一次姿势，固定在路径世界位置，按阵营色淡出。

玩家入口仍为 `BP_GroundMech_Light`，动画实例仍为 `ABP_GroundMech_A4B2`。旧世界方向语义接口保持；SprintSpeed=432 仍作为喷气前向 864 的数值基准，物理 Shift 已不再触发疾跑。

## 编译、网络与 PIE 阻断修复

- 沿用用户同范围保存、编译和重启授权，源码引擎 `D:/UnrealEngine-5.7`，目标 `GuLiStrikeEditor Win64 Development`，退出码 0；[构建证据](build.json)及[日志](preserved_impact_build.log)。项目/引擎 BuildId 均为 `d4757e21-eaef-492e-b079-c8c0056c493d`。
- 新编辑器进程 24520，2026-10-26 09:11:28 启动；该时间点已包含本轮原命中特效纠正。
- Ground 输入、移动快照、共享控制版本 5，Ground 状态版本 9。闪现请求序号、阶段、路径及冷却纳入恢复，过期快照不重播残影。
- 用户反馈“开不了 PIE”的具体原因是新 Ground 合同与未更新的运行清单不兼容。两份 200/600 清单已重新导出并校验；配置指纹 `1c9969c60b50c588894a46386544da24`。[清单结果](final_network_validation.json)。
- 资源烘焙与 Ground/Flight 导航只读前置检查通过；未运行会启动本地会话的 PrepareForPIE。[前置检查](pie_asset_preflight.json)。
- 冷启动直接读取保存动画图 73 节点，先读图后检查，不重建图掩盖丢失；[冷读记录](ground_cold_read.json)。Niagara 与母材质编译无错误。

## 地图与玩家验收

地图 `/Game/Maps/LVL_CommanderMassPrototype` 已保存，GuLi Local PIE 的 MainRole 为 Ground。复用原平地、18°坡面、四级台阶和入口定位点，共 8 个实体：[保存与数据核对](scene.json)。本轮临时命中墙已移除，导航保存数据有效。

验证区中心约 `(-4000,50000,905)`；`GroundMobility_Entry` 位于 `(-2800,50000,1149.04)`，是编辑器定位点，不是重生点。可在现有地图就近使用地面和障碍物验收，无需等待经济或战斗前置。

1. 用户启动 PIE，确认进入 Ground。对原地面/单位射击：命中保留旧轮廓，只变大、变橙黄并在 0.2 秒淡出；固定世界曲线弹道沿用既有版本。
2. W/A/S/D 配合按下 Shift，以及不按方向时单独 Shift：核对方向、双臂后展、落点与残影；按住不连发，冷却后再次按下可触发。
3. 面向现有障碍物闪现，检查停在可达位置；在空中闪现后检查垂直惯性、喷气及落地恢复。
4. 核对闪现前后正常移动、转身、双枪与 UI 输入恢复。距离、冷却、动作及残影观感由用户反馈后调表。

结构检查、构建与场景保存不能替代以上玩家效果和联机验收。

## 可复现源文件

- 表编辑：`Scripts/GroundMech/MechaDestroyer/author_blink_impact_tuning.py`（excelize），随后标准导出/导入。
- 原形态命中派生：`Scripts/GroundMech/MechaDestroyer/author_preserved_impact_assets.py`。
- 闪现模拟：`Source/GuLiStrike/Gameplay/GroundMech/GuLiGroundMechBlink.cpp`；姿态：`AnimNode_GuLiGroundMechAim`；世界残影：`Source/GuLiStrike/AI/BlinkVFX`。
- UE 资产目录 `/Game/GuLiStrike/FX/GroundMech/BlinkImpact_B1/` 包含调整副本及残影材质。源命中仍依赖项目原有 Niagara_Toon_Projectiles_2 材质/纹理；本轮未迁入外部包。
