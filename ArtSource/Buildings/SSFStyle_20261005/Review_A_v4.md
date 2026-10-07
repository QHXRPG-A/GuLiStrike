# SSF 建筑参考审核 A v4

**当前版本：SSF_Reference_A_v4。A 待审核，B 未开始。** 本套是基于原模型的参考设计，成品重制、减面、三档 LOD 和正式 UE 导入在相应审核后执行。

![六座建筑总览](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Overview_Buildings.png)

按本轮“深色的颜色再浅一些，不要有太深的颜色”反馈，保留六座独立色系并提亮深色：暖橙、浅粉、柔莓红、浅钢蓝、浅青绿和浅紫色。设备与机械框架同步变浅，结构线改为中等明度蓝灰，三渲二暗部也提高亮度。无人机跟随军工厂，平台改为浅蓝。

下表是当前实际采用的HEX。原色卡选色经过保持色相的提亮，派生色值已明确记录，不能把它们误写成色卡原HEX。

| 建筑 | 色系 | 主体 | 设备 | 点缀 | 框架 |
|---|---|---|---|---|---|
| 空军基地 | 暖橙航空 | `#EE9D58` | `#69A6C3` | `#FEE4D9` | `#9590C6` |
| 克隆中心 | 浅粉医疗 | `#EABCBA` | `#BD93A1` | `#FEE4D9` | `#B983C3` |
| 指挥中心 | 莓红指挥 | `#D18B98` | `#E3B6B1` | `#EE9D58` | `#C383A8` |
| 军工厂 | 钢蓝工业 | `#6DA6BF` | `#AF91D5` | `#EE9D58` | `#9590C6` |
| 反应堆 | 青绿动力 | `#2EAEB6` | `#6DA6BF` | `#EE9D58` | `#50A3A6` |
| 战略中心 | 紫色情报 | `#BC8EC9` | `#E3B6B1` | `#2EAEB6` | `#A58BC7` |

本轮描线色：`#5F8A9E`；三档明暗因子由 `0.40 / 0.72 / 1.0` 改为 `0.78 / 0.90 / 1.0`，保留明暗层次并减少大片压暗。平台主体为 `#69A6C3`。6m规整板缝继续作为参考设计。

三档艺术明暗共用固定光向；结构内线较细，外轮廓稍重。单图原生 2048×2048；四图板可放大查看。各资产内保持同一正交尺度与中立姿态，组合图保持真实资源尺寸。

## 六座建筑与配套图板

### AirBase · 空军基地

![空军基地四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Sheets/AirBase_Sheet_A_v4.png)

- 保留：圆形库体、侧置控制塔、通信天线、分瓣门与基座。
- 减面方向：优化冗余分段与隐藏叠面；保留规则圆周、门体和破坏可见内构。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/AirBase_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/AirBase_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/AirBase_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/AirBase_Back.png)

### CloningCenter · 克隆中心

![克隆中心四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Sheets/CloningCenter_Sheet_A_v4.png)

- 保留：主舱、五个培养舱、五路软管、入口门和显示器。
- 减面方向：整理舱壳与基座冗余；保留五舱、软管柔性及运动中可见内侧。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/CloningCenter_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/CloningCenter_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/CloningCenter_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/CloningCenter_Back.png)

### CommandCenter · 指挥中心

![指挥中心四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Sheets/CommandCenter_Sheet_A_v4.png)

- 保留：中心塔、环形层板、天线、支架与后部管路。
- 减面方向：优化层板和重复小件；保留主塔、支架、天线及全部破坏分件。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/CommandCenter_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/CommandCenter_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/CommandCenter_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/CommandCenter_Back.png)

### MilitaryFactory · 军工厂

![军工厂四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Sheets/MilitaryFactory_Sheet_A_v4.png)

- 保留：四段卷帘门、三路弯管、四角支腿与通信设备。
- 减面方向：整理壁板和支腿冗余；保留门行程、舱内结构与配套飞行无人机。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/MilitaryFactory_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/MilitaryFactory_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/MilitaryFactory_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/MilitaryFactory_Back.png)

### Reactor · 反应堆

![反应堆四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Sheets/Reactor_Sheet_A_v4.png)

- 保留：三向支撑、分瓣壳体、散热栅、顶盖和连杆。
- 减面方向：优化壳体与支座分段；保留三重径向结构、顶盖、连杆和活动内部件。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Reactor_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Reactor_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Reactor_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Reactor_Back.png)

### StrategyCenter · 战略中心

![战略中心四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Sheets/StrategyCenter_Sheet_A_v4.png)

- 保留：旋转座、定向阵列、天线、支架和基座。
- 减面方向：整理层板和重复件；保留阵列朝向、旋转座与天线。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/StrategyCenter_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/StrategyCenter_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/StrategyCenter_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/StrategyCenter_Back.png)

### Floor · 共用平台

![共用平台四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Sheets/Floor_Sheet_A_v4.png)

- 保留：原外轮廓、双层台面、坡道、边界件与出入口；6m规整板缝。
- 减面方向：优化边界重复件与隐藏重叠面；保留平台尺寸、坡度和出入口。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Floor_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Floor_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Floor_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Floor_Back.png)

[俯视补充图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Floor_Top.png)

### Lamp · 灯柱

![灯柱四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Sheets/Lamp_Sheet_A_v4.png)

- 保留：原支柱、弯折灯臂和灯头，保持原尺寸。
- 减面方向：原网格48面；三档保留必要几何，不强行削减。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Lamp_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Lamp_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Lamp_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Lamp_Back.png)

### Light · 发光贴片

![发光贴片四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Sheets/Light_Sheet_A_v4.png)

- 保留：原单面半透明贴片；1.97m方形、零厚度。
- 减面方向：原网格2面；正视沿贴片法线、左视为零厚度边缘、背面无正面图案，不补体积。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Light_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Light_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Light_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Light_Back.png)

### Drone · 配套无人机

![配套无人机四视图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Sheets/Drone_Sheet_A_v4.png)

- 保留：原机身、导流件和两侧结构；沿用军工厂配色。
- 减面方向：仅优化隐藏与冗余面；保留飞行动画、翼形及1m级原尺寸。
- 原生单图：[三分之四效果图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Drone_Hero.png) · [正面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Drone_Front.png) · [左侧正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Drone_Left.png) · [背面正交图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Drone_Back.png)

[俯视补充图](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Images/Drone_Top.png)

## 原尺寸组合参考

![组合效果](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Assembly_Hero_A_v4.png)

[组合俯视](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/Assembly_Top_A_v4.png)

组合仅展示体量与配色，不作为关卡布局方案。

## 结构与活动件基线

七套骨架及22个源动画已归档。每个动作采集了起点、四分之一、中点、四分之三和终点的局部骨骼姿态，用于识别活动件；这不是全部运动和动画兼容性的验收。

源FBX把每套UE的Root表示为Blender骨架对象，其余骨骼标识一致。后续成品导出必须还原并核对Root命名、层级、参考姿态和动画单位。平台源材质槽当前为WorldGridMaterial，因此本轮仅以其几何为基线，新配色在参考场景独立设计。

灯片保持源单面半透明用途，侧边为零厚度；六座的原半透明标识及其活动骨骼保留。源几何、拓扑和权重摘要在配色前后相等。

## 审核与后续

请针对 **SSF_Reference_A_v4 整套** 给出通过或需要修改的具体建筑、部件与配色。A通过后按本套设计重制可编辑分件、三档LOD和绑定；B针对实际Blender成品版本审核，B通过后再导入正式目录。

[模型制作技能](../../../.agents/skills/guli-model-production/SKILL.md)和[项目美术规范](../../../Progress/RequirementDocument/GuLiStrike美术规范.md)规定“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”。本轮依照用户指定流程停在A。

[技术清单](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/validation.json) · [参考场景](D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/References_A_v4/SSF_ReferenceDesign_v4.blend)
