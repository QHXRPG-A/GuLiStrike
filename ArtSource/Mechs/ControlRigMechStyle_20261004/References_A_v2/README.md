# ControlRig 机甲：四色参考 A-v2

2026-10-04。依据用户附图及原话“改成这种配色”，修订 `/Game/Assets/ControlRig/Characters/Mech` 的参考配色。当前 **A-v2 待审核，B 未开始**；A-v1 保留为历史版本，配色已由本版替代。

## 效果与三视图

四图由同一个真实源网格研究场景原生渲染，均为 **2048×2048**。沿用 A-v1 的四个相机、源参考姿态和正交尺度 **16.14464 m**；正、左、背为正交视图，三分之四也使用正交相机。与原灰模保持同机位、同尺度，可直接对照；没有改姿态、结构或重新生成几何。

| 视角 | 当前 A-v2 | 同机位源灰模 |
|---|---|---|
| 三分之四 | [效果图](ControlRigMech_A_v2_Hero.png) | [灰模](../Baseline/ControlRigMech_Source_Hero_2048.png) |
| 正面 | [正视图](ControlRigMech_A_v2_Front.png) | [灰模](../Baseline/ControlRigMech_Source_Front_2048.png) |
| 左侧 | [左视图](ControlRigMech_A_v2_Left.png) | [灰模](../Baseline/ControlRigMech_Source_Left_2048.png) |
| 背面 | [背视图](ControlRigMech_A_v2_Back.png) | [灰模](../Baseline/ControlRigMech_Source_Back_2048.png) |

![A-v2 三分之四效果](ControlRigMech_A_v2_Hero.png)

## 配色与三档明暗

采用附图标明的四个 sRGB 色值，原件分色边界和主要左右对应关系沿用 A-v1：

| 颜色 | sRGB | 使用位置 |
|---|---|---|
| 深青灰 | `#2C3735` | 机械骨架、软管、底盘下层、孔洞及轴承内芯 |
| 铁锈红 | `#8E3A2A` | 炮口外罩、左右检修/散热外罩、四腿胫部与踝部检修护甲、部分活塞护罩 |
| 灰青 | `#557B78` | 主炮外壳、炮塔主装甲、四腿主要护板、底盘上层 |
| 沙米色 | `#D5C09C` | 圆形关节盖、活塞杆、金属装配件与原有功能镜片 |

原琥珀镜片改用沙米色亮档，不增加第五种装甲色。亮部/中间色/阴影仍使用线性亮度 **1 / 0.74 / 0.42**，固定世界艺术光向和阈值不变。色值表示亮档底色；明暗档在同色相中推导，避免误把阴影当成分色变化。线稿为深青灰派生深色 `#1B2422`，内线 1.4 px、外轮廓 2.25 px；背景保留原中性暖色。

[用户色板原图](Inputs/UserPalette_20261004.jpg)已按原字节复制留档；未修改照片或用照片像素替代其标注色值。[设置](reference_setup.json)记录准确材质、相机、分色与一致性检查。[冻结清单](reference_manifest.json)记录文件哈希、色板来源和 A-v1 的冻结清单引用。

## 活动件与源接口

实际资产为 **四足、单主炮**；保留炮塔回转、主炮俯仰/伸缩、四腿圆关节/活塞/足爪、软管及原单侧天线。部署中的底盘抬升和炮管伸缩保持源基线；参考图为源参考姿态，不是已完成的动作预览。

源基线仍为 **152 骨骼、284,700 三角面、14 材质区段、1 LOD**；Blender 回读参考为 284,660 三角面，差额是先前已审计的 40 个同索引重复面。尺寸为宽 922.64、前后长 1323.33、高 689.26 cm，含上仰炮管。源无物理资产、挂点 0。部署/待机/行走源动画及 CR_Mech 见[源清单](../Source/source_manifest.json)、[Rig/动画基线](../Source/rig_animation_baseline.json)与[实施台账](../../../../Progress/DevelopmentDocumentation/20261004-ControlRig机甲美术统一.md)。

本轮核对网格位置/拓扑/权重、物体变换、骨骼层级/参考姿态和自定义法线未改变。默认 FBX 回读仍将 UE `root` 表示为 Armature 对象，参考场景列 151 根 Blender 骨骼；生产阶段须恢复源 152 骨骼完整契约。当前 `.blend` 仅是参考材质/相机研究，不能当作 B 成品。

## 版本与后续

- [可编辑参考研究场景](ControlRigMech_A_v2_ReferenceStudy.blend)：打开默认显示三分之四相机。
- [当前审核决定](../review_decisions.json)：A-v1 为修改后再审，A-v2 待审核；B 和正式 UE 交付未开始。
- [A-v1 历史参考](../README.md)：冻结文件保持原样，作为修订对照。

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../../../../Progress/RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。

按用户原计划及[制作技能](../../../../.agents/skills/guli-model-production/SKILL.md)、[美术规范 v1.2 §2](../../../../Progress/RequirementDocument/GuLiStrike美术规范.md#2-制作与审核流程)的“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”顺序，先取得具体 **A-v2** 的通过决定。当前配色修改只确认修订方向，不记为 A/B 通过。B 后才导出回读并建立 `/Game/GuLiStrike/Mechs/ControlRigMech` 正式副本；未更改 UE 资源、玩法身份或战斗引用。全局规范仍为 v1.2。

> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../../CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。
