# RSG 六足机器人 · 审核 A-v2

2026-10-03 用户修订要求：**“加一点天蓝色”**。当前参考版本更新为 **A-v2，待审核 A**；这条配色修改要求不代表 A 已通过。

## 四张主参考

| 效果图 | 正面 | 左侧 | 背面 |
|---|---|---|---|
| [三分之四](References_A_v2/RSG_A_v2_Hero_SourceStyle.png) | [正交](References_A_v2/RSG_A_v2_Front_SourceStyle.png) | [正交](References_A_v2/RSG_A_v2_Left_SourceStyle.png) | [正交](References_A_v2/RSG_A_v2_Back_SourceStyle.png) |

![A-v2 效果图](References_A_v2/RSG_A_v2_Hero_SourceStyle.png)

每张 **2048 × 2048**。在 A-v1 实际源模型参考材质场景中仅调整 8 个已有分件的配色，四图沿用同一姿态、同一几何、同一机位与正交比例。没有重制或减面生产模型，未导入正式 UE 目录。

## 新增天蓝色点缀

色值 **`#87CEEB`**，使用与原方案一致的三档光照和结构线。

| 区域 | 数量 | 配色修改 |
|---|---:|---|
| 六腿胫甲内部的检修/内嵌件 | 6（三对） | 原珊瑚红内嵌件改为天蓝；保持外部暖白胫甲与珊瑚膝甲 |
| 上炮组外侧的小型盖板 | 2（左右一对） | 原珊瑚红局部外盖改为天蓝，周边珊瑚结构保留 |

大面积暖白、珊瑚红、深暖灰骨架与炮管、暖灰轴承、琥珀功能指示和暖黑线稿延续 A-v1。蓝色集中于小面积既有分件，作为次级点缀；不新增发光蓝灯、装饰条或结构件。

部件、骨骼和尺寸说明见 [A-v1 源结构说明](README.md)。同机位灰模仍使用原 [Baseline](Baseline/blender_source_inspection.json)，不覆盖旧参考或灰模文件。

## 来源与检查

- [参考 Blender 场景](References_A_v2/RSG_A_v2_ReferenceMaterialStudy.blend)、[相机/配色变更记录](References_A_v2/reference_setup.json)、[制作脚本](Scripts/render_style_reference_a_v2.py)。
- [A-v2 哈希清单](References_A_v2/reference_manifest.json)记录四图、源场景与变更来源。A-v1 已冻结哈希保持一致。
- 修改前后网格位置、拓扑及对象变换摘要完全一致；8 个改色分件均有镜像同色对应件，源六足、炮组和关节位置保持。
- 主参考由 Blender 统一渲染；使用内置 imagegen 留档了[配色试图 prompt](References_A_v2/imagegen_color_prompt.txt)和[原始试图](References_A_v2/RSG_A_v2_ImagegenColorDraft.png)。试图为 1254 × 1254，且局部面板表达有偏差，不替代四张结构一致的 2K 主参考。

## 审核记录

| 版本/阶段 | 当前决定 |
|---|---|
| A-v1 | 保留历史版本；用户要求加入少量天蓝后，当前提交版本改为 A-v2 |
| A-v2 | **待用户明确审核**；配色修改指示不是批准 |
| Blender B | 未开始，A 通过后严格按已审图重制 |
| UE 正式交付 | 未开始，B 通过后导出回读再导入 `/Game/GuLiStrike/Robots/RSGMech` |

[需求](../../../Progress/RequirementDocument/20261003-RSG六足机器人美术统一.md) · [制作记录](../../../Progress/DevelopmentDocumentation/20261003-RSG六足机器人美术统一.md)
