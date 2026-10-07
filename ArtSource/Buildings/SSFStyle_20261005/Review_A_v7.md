# SSF 建筑参考审核 A v7

**当前版本：SSF_Reference_A_v7。A 待审核，B 未开始。** 本套是基于原模型的参考设计，成品重制、减面、三档 LOD 和正式 UE 导入在相应审核后执行。

![六座建筑总览](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Overview_Buildings.png)

根据用户反馈“线稿和三档明暗好像没加？”，保持A_v6全部配色和原几何，强化参考线稿与三档色阶可见性。A_v6已有连接完整的三档与线稿，但军工厂该机位暗部仅0.70%，总览线条太细。本版调整分区阈值和线条粗细，不改变任何主体、设备、点缀或框架HEX。

下表是沿用A_v6的全部实际HEX，本轮40项配色角色、90个实际材质的基础色与三档RGB保持原值。军工厂和战略中心沿用上一轮提亮后的紫色，其他建筑与配套配色不再改动。

| 建筑 | 色系 | 主体 | 设备 | 点缀 | 框架 |
|---|---|---|---|---|---|
| 空军基地 | 暖橙航空 | `#EE9D58` | `#274E61` | `#FEE4D9` | `#1A182F` |
| 克隆中心 | 浅粉医疗 | `#EABCBA` | `#835061` | `#FEE4D9` | `#45184D` |
| 指挥中心 | 莓红指挥 | `#A34053` | `#E3B6B1` | `#EE9D58` | `#662249` |
| 军工厂 | 钢蓝工业 | `#6AA4BE` | `#9972CA` | `#EE9D58` | `#756FB5` |
| 反应堆 | 青绿动力 | `#0D9099` | `#6AA4BE` | `#EE9D58` | `#032E30` |
| 战略中心 | 紫色情报 | `#AA6EB9` | `#E3B6B1` | `#0D9099` | `#8A67B6` |

描线色保留`#1A182F`，三档因子保留`0.40 / 0.72 / 1.0`；分区阈值由`0.12 / 0.55`改为`0.38 / 0.68`。2048px图的结构线由1.45px改为2.2px、外轮廓由2.6px改为4.4px，内部遮罩强度0.65。固定艺术光向、曝光与法线不改。平台主体仍为`#274E61`。

[本轮风格修正](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/style_revision.json) · [实际色阶可见性核对](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/style_visibility_validation.json) · [实际Blender颜色回读](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/palette_preservation_validation.json)

三档艺术明暗共用固定光向；结构内线较细，外轮廓稍重。单图原生 2048×2048；四图板可放大查看。各资产内保持同一正交尺度与中立姿态，组合图保持真实资源尺寸。

## 风格前后对照

![同机位线稿与明暗前后对照](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Style_Comparison_A_v6_to_A_v7.png)

[A_v6线稿与三档拆分核对](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Style_Breakdown_A_v6.png)

当前外轮廓仍为参考Freestyle，内线为参考遮罩；实际生产描边壳和图集在A通过后制作。灯片原单面发光用途保留，不强行给平面伪造体积和三档表面阴影。

## 六座建筑与配套图板

### AirBase · 空军基地

![空军基地四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Sheets/AirBase_Sheet_A_v7.png)

- 保留：圆形库体、侧置控制塔、通信天线、分瓣门与基座。
- 减面方向：优化冗余分段与隐藏叠面；保留规则圆周、门体和破坏可见内构。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/AirBase_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/AirBase_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/AirBase_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/AirBase_Back.png)

### CloningCenter · 克隆中心

![克隆中心四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Sheets/CloningCenter_Sheet_A_v7.png)

- 保留：主舱、五个培养舱、五路软管、入口门和显示器。
- 减面方向：整理舱壳与基座冗余；保留五舱、软管柔性及运动中可见内侧。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/CloningCenter_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/CloningCenter_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/CloningCenter_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/CloningCenter_Back.png)

### CommandCenter · 指挥中心

![指挥中心四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Sheets/CommandCenter_Sheet_A_v7.png)

- 保留：中心塔、环形层板、天线、支架与后部管路。
- 减面方向：优化层板和重复小件；保留主塔、支架、天线及全部破坏分件。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/CommandCenter_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/CommandCenter_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/CommandCenter_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/CommandCenter_Back.png)

### MilitaryFactory · 军工厂

![军工厂四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Sheets/MilitaryFactory_Sheet_A_v7.png)

- 保留：四段卷帘门、三路弯管、四角支腿与通信设备。
- 减面方向：整理壁板和支腿冗余；保留门行程、舱内结构与配套飞行无人机。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/MilitaryFactory_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/MilitaryFactory_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/MilitaryFactory_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/MilitaryFactory_Back.png)

### Reactor · 反应堆

![反应堆四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Sheets/Reactor_Sheet_A_v7.png)

- 保留：三向支撑、分瓣壳体、散热栅、顶盖和连杆。
- 减面方向：优化壳体与支座分段；保留三重径向结构、顶盖、连杆和活动内部件。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Reactor_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Reactor_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Reactor_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Reactor_Back.png)

### StrategyCenter · 战略中心

![战略中心四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Sheets/StrategyCenter_Sheet_A_v7.png)

- 保留：旋转座、定向阵列、天线、支架和基座。
- 减面方向：整理层板和重复件；保留阵列朝向、旋转座与天线。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/StrategyCenter_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/StrategyCenter_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/StrategyCenter_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/StrategyCenter_Back.png)

### Floor · 共用平台

![共用平台四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Sheets/Floor_Sheet_A_v7.png)

- 保留：原外轮廓、双层台面、坡道、边界件与出入口；6m规整板缝。
- 减面方向：优化边界重复件与隐藏重叠面；保留平台尺寸、坡度和出入口。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Floor_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Floor_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Floor_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Floor_Back.png)

[俯视补充图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Floor_Top.png)

### Lamp · 灯柱

![灯柱四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Sheets/Lamp_Sheet_A_v7.png)

- 保留：原支柱、弯折灯臂和灯头，保持原尺寸。
- 减面方向：原网格48面；三档保留必要几何，不强行削减。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Lamp_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Lamp_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Lamp_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Lamp_Back.png)

### Light · 发光贴片

![发光贴片四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Sheets/Light_Sheet_A_v7.png)

- 保留：原单面半透明贴片；1.97m方形、零厚度。
- 减面方向：原网格2面；正视沿贴片法线、左视为零厚度边缘、背面无正面图案，不补体积。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Light_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Light_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Light_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Light_Back.png)

### Drone · 配套无人机

![配套无人机四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Sheets/Drone_Sheet_A_v7.png)

- 保留：原机身、导流件和两侧结构；配色保持附图A_v3，本轮不随军工厂提亮。
- 减面方向：仅优化隐藏与冗余面；保留飞行动画、翼形及1m级原尺寸。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Drone_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Drone_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Drone_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Drone_Back.png)

[俯视补充图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Images/Drone_Top.png)

## 原尺寸组合参考

![组合效果](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Assembly_Hero_A_v7.png)

[组合俯视](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/Assembly_Top_A_v7.png)

组合仅展示体量与配色，不作为关卡布局方案。

## 结构与活动件基线

七套骨架及22个源动画已归档。每个动作采集了起点、四分之一、中点、四分之三和终点的局部骨骼姿态，用于识别活动件；这不是全部运动和动画兼容性的验收。

源FBX把每套UE的Root表示为Blender骨架对象，其余骨骼标识一致。后续成品导出必须还原并核对Root命名、层级、参考姿态和动画单位。平台源材质槽当前为WorldGridMaterial，因此本轮仅以其几何为基线，新配色在参考场景独立设计。

灯片保持源单面半透明用途，侧边为零厚度；六座的原半透明标识及其活动骨骼保留。源几何、拓扑和权重摘要在配色前后相等。

## 审核与后续

请针对 **SSF_Reference_A_v7 整套** 给出通过或需要修改的具体建筑、部件与配色。A通过后按本套设计重制可编辑分件、三档LOD和绑定；B针对实际Blender成品版本审核，B通过后再导入正式目录。

[模型制作技能](../../../.agents/skills/guli-model-production/SKILL.md)和[项目美术规范](../../../Progress/RequirementDocument/GuLiStrike美术规范.md)规定“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”。本轮依照用户指定流程停在A。

[技术清单](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/validation.json) · [参考场景](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v7/SSF_ReferenceDesign_v7.blend)
