# RSG 六足机器人 · 审核 A-v3

2026-10-03 用户修订：“背后的环也换成天蓝色，头部的圆球换成浅黄色”。当前可审版本为 **A-v3，待审核 A**。

![A-v3 效果图](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/References_A_v3/RSG_A_v3_Hero_SourceStyle.png)

| 效果图 | 正面 | 左侧 | 背面 |
|---|---|---|---|
| [三分之四](References_A_v3/RSG_A_v3_Hero_SourceStyle.png) | [正交](References_A_v3/RSG_A_v3_Front_SourceStyle.png) | [正交](References_A_v3/RSG_A_v3_Left_SourceStyle.png) | [正交](References_A_v3/RSG_A_v3_Back_SourceStyle.png) |

四图均为 **2048 × 2048**，来自同一实际源网格、同一参考姿态与相机设置。正面、左侧、背面均为正交视图；视野高度延续约 13.2464m。

## 本次配色

| 区域 | A-v3 色值 | 修改 |
|---|---|---|
| 背后开口环形护架，含连续侧段 | 天蓝 `#87CEEB` | 原暖白改为天蓝 |
| 头部中央圆球/圆弧前甲 | 浅黄 `#F4E4A1` | 原暖白改为柔和浅黄，使用哑光三档着色 |
| 六腿胫甲内嵌件与左右上炮外盖 | 天蓝 `#87CEEB` | 延续 A-v2 的 8 个改色分件 |

其余暖白 `#F1E7D5`、珊瑚 `#E97868`、深暖灰 `#48413E`、暖灰轴件 `#756A60`、琥珀功能灯 `#F4B65C`、暖黑结构线 `#241F20` 沿用前版。浅黄头球属于装甲底色，功能指示仍使用琥珀。

源结构、尺寸、活动件与原骨骼说明见 [源基线与 A-v1 说明](README.md)。背环和头球本轮仅改色，保持原装配与活动件关系。源六足、双侧炮组、关节、规则圆周和剪影保持一致。

## 来源与核对

- [参考材质 Blender 场景](References_A_v3/RSG_A_v3_ReferenceMaterialStudy.blend)、[配色与相机记录](References_A_v3/reference_setup.json)、[可重现渲染脚本](Scripts/render_style_reference_a_v3.py)、[版本哈希清单](References_A_v3/reference_manifest.json)。
- 网格位置、拓扑、对象变换的修改前后摘要完全相同；相机与 A-v2 一致，先前 8 个天蓝分件全部保留。已查看四图的配色、遮挡和结构一致性。
- A-v1、A-v2 和共用灰模保持原文件，发布前校验旧版本哈希。
- 内置 imagegen 的[改色 prompt](References_A_v3/imagegen_color_prompt.txt)与[原始配色试图](References_A_v3/RSG_A_v3_ImagegenColorDraft.png)留档。试图原生 1254 × 1254，未选入审核主图；四张审核主图使用结构一致的源模型 2K 材质参考渲染。

## 审核与后续

| 阶段 | 当前状态 |
|---|---|
| A-v1 / A-v2 | 历史参考保留，未收到通过决定 |
| A-v3 | **待用户明确通过具体版本**；本次改色要求不作为批准 |
| Blender 成品 B | 未开始；A 通过后严格按已审图重制、减面、绑定与制作 LOD |
| UE 正式交付 | 未开始；B 通过后导出回读，再导入 `/Game/GuLiStrike/Robots/RSGMech` |

当前 Blender 文件为源网格参考材质研究，尚未制作生产模型、目标 LOD 或动画兼容性验收；本轮未写入 UE 正式资源。

[需求](../../../Progress/RequirementDocument/20261003-RSG六足机器人美术统一.md) · [制作与审核记录](../../../Progress/DevelopmentDocumentation/20261003-RSG六足机器人美术统一.md)
