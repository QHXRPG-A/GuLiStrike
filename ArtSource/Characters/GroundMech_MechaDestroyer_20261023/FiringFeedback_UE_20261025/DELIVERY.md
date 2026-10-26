> 2026-10-26 本会话已按用户指令完成归档。以下保留当时版本及审核事实，最终方向/时长与清理说明见[完成归档](../../../../Progress/Archive/20261026-地面机甲镜头方向闪现与会话完成归档.md)；不补记未执行的视觉验收。

# 地面机甲整臂制退与单曲线弹道交付

2026-10-25。用户已确认发射频率、弹速均翻倍，并将尾迹收敛为“一条曲线”；随后明确“PIE已开，你可以直接用这个效果来做”。本次按[A_v3参考](../FiringFeedback_A_v3_20261025/REVIEW.md)直接实施并接入 Ground。源码与资源已保存、编译及冷加载，UE已重新打开；本轮助手未启动PIE、截图或目检，游戏内效果待用户验收。

## 当前参数

| 项目 | 本次结果 | 维护入口 |
|---|---|---|
| 整臂制退 | 各侧肩关节后摆18°；0.025秒冲击、0.015秒制退停留、0.20秒恢复 | GuLiStrikeMech.xlsx / Gait / Shoot |
| 原枪管后坐 | 保留5cm及原0.15秒曲线，独立于整臂动作 | Gait / Shoot，现有枪管链路 |
| 发射频率 | 2/4/6 → 4/8/12发每秒 | GuLiStrikeMech.xlsx / 升级表 |
| 弹丸飞行速度 | 12000 → 24000cm/s | GuLiStrikeMech.xlsx / 技能表 / BasicMachinegun |
| 枪焰 | 线性尺寸2倍，User.Size=2，组件保持单位缩放 | GuLiStrikeVfx.xlsx / Effects / PlayerMuzzle |
| 弹头可见宽度 | 12 → 24cm | Effects / PlayerBullet / Scale Y、Z |
| 尾迹长度 | 90 → 180cm，单条平滑渐细曲线 | Effects / PlayerBullet / Scale X |
| 伤害、碰撞 | 每发10/15/20，SweepRadius=15cm，寿命5秒保持 | 原机甲数据入口 |

上述数字是配置与几何数据，不是视觉测量。曲线采用浅S形、紫色尾端过渡到橙色/象牙色弹头，一枚弹丸只使用一个粒子、一个渲染器；曲线画布72×180cm中的弹头直径为24cm，画布额外宽度供弯曲使用。弯曲只属于可见尾迹，真实弹丸仍沿最终枪轴直行。材质颜色取统一色库 #522C5C / #EE9D58 / #FEE4D9；模型阵营材质保持。

整臂由实际 MuzzleIndex 分侧触发，重触发从当前制退权重接续，肩部支点与刚性臂长保持。后坐后重新解算枪轴及枪口偏移，保留右键瞄准、松开回正、左右各75°限位与交替开火；动作只写枪臂和枪骨，腿部动画、IK及移动参数未在本轮重制。

## 资源与入口

新资源均位于 `/Game/GuLiStrike/FX/GroundMech/FiringFeedback_A3/`：

- `M_GroundMech_SingleCurve_A3`：程序化单曲线材质，无贴图采样。
- `NS_GroundMech_Bullet_SingleCurve_A3`：沿用项目原子弹系统的独立副本，单粒子，无额外速度积分和随机位置偏移。
- `NS_GroundMech_Muzzle_A3`：原枪焰项目副本，保留原火焰及少量火星，尺寸翻倍。

`DT_GuLiStrikeVfx_Effects` 的 PlayerBullet / PlayerMuzzle（8/9）已指向新资源。原效果资源保留。机甲仍使用 `BP_GroundMech_Light` 和 `ABP_GroundMech_A4B2`，模型、骨架、原包资源保持。

Excel → JSON/CSV → 自动生成行结构 → UE DataTable 已跑通。导入器同步七张表：Upgrades、Skills、Mobility、Gait、GroundIK、Camera、Vfx；以后只调已有数值可重新导出、导入并重新进入游戏。弹头/尾迹尺寸由导入钩子同步Niagara；枪焰倍率由 User.Size 接收，避免叠乘组件缩放。配置指纹加入升级表射速，当前两份网络清单均为 `4d7805a0b88ebfc37d391a66c0b4287e`；输入/状态序列化版本保持。

## 技术核对

- 沿用用户既有保存、编译和重启授权。源码引擎 `D:/UnrealEngine-5.7`，`GuLiStrikeEditor Win64 Development` 最终退出0。首次构建发现编辑器 TObjectPtr 遍历类型问题，修正后重编成功；引擎和项目 BuildId 均为 `d4757e21-eaef-492e-b079-c8c0056c493d`，见[BuildId](build_ids.json)。
- 动画蓝图仅补3条整臂后坐变量输入，69→72节点，原68条连接全部保留；编译0错误0警告，见[补线记录](animation_patch.json)。未全量重建动画图。
- 两套Niagara编译0错误0警告；曲线材质9个表达式、0贴图采样、编译成功，见[资源制作数据](asset_authoring.json)。
- 源表、导出数据及保存后的DataTable核对通过，见[数值核对](source_data_check.json)、[导入结果](table_import.json)。
- 保存并重启后直接读取现存资源，未先重建动画图、求值角色姿势或进入PIE。单粒子、尺寸、材质、枪焰倍率与射速通过[冷加载检查](cold_firing_assets.json)；既有机动、IK、支撑曲线及动画连线通过[完整冷读取](../Production_A4_B_v2/UE_Export/firing_feedback_cold_read_20261025.json)。通用接口未能返回枪焰内部Vector输入文本，不将该空值记录为独立冷读通过；原生写入与Niagara编译已成功。
- 本轮未做性能测量、联机运行或视觉验收。编译与结构检查不代表玩家观感通过。
- Progress索引构建及全量检查完成，0错误、19条既有文档尺寸/任务数提示；当前开发根文档34.8KB，后续宜按模型、机动、武器边界拆分，本轮未扩大范围整理历史文档。

## 玩家验收入口

地图 `/Game/Maps/LVL_CommanderMassPrototype` 已保存，GuLi Local PIE 的 MainRole=Ground，入口为 `BP_GroundMech_Light`。复用既有8个 GroundMobility 实体，平地中心约(-4000,50000,905)，包含18°坡面和四级台阶；`GroundMobility_Entry` 是定位点而非重生点。[地图实体与保存结果](scene_delivery.json)已记录，保存后无未保存资产。

编辑器已重新打开，PIE因编译和冷加载而结束。用户重新Play后：

1. 按住左键，观察左右轮流整臂制退、放大的枪焰、单曲线尾迹及更密集/更快的弹丸。
2. 按右键瞄准并连续开火，核对后坐过程中枪口方向及75°边界；松开右键，核对平滑回正后的继续开火。
3. 移动、转身或喷气时开火，核对腿部动作与武器反馈是否协调。

整臂幅度、恢复节奏、曲线弧度、亮度与遮挡均由用户验收；没有登记为视觉通过。
