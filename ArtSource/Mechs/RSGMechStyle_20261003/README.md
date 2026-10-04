# RSG 六足机器人 · 审核 A-v1

当前状态：**参考待审核 A，Blender 重制与审核 B 未开始，正式 UE 未导入。**

用户于 2026-10-03 要求实施 RSG 六足机器人美术统一方案，并明确首轮停在审核 A。风格选择和实施授权没有记为图片或成品通过。

## 审核入口

[交互审图页](review_A_v1.html)支持四视图切换、源灰模/配色参考对照和灰模叠加。四张主参考均为 **2048 × 2048**，同一原始参考姿态、同一正交比例（13.2464m 视野高度）；左侧按源 `_L` 骨骼所在侧定义。

| 效果图 | 正面 | 左侧 | 背面 |
|---|---|---|---|
| [三分之四](References_A_v1/RSG_A_v1_Hero_SourceStyle.png) | [正交](References_A_v1/RSG_A_v1_Front_SourceStyle.png) | [正交](References_A_v1/RSG_A_v1_Left_SourceStyle.png) | [正交](References_A_v1/RSG_A_v1_Back_SourceStyle.png) |

![A-v1 效果图](References_A_v1/RSG_A_v1_Hero_SourceStyle.png)

主参考直接使用实际源网格做材质、展示法线与线稿研究，确保六足、双侧炮组、比例、关节位置与装配在各图中一致。没有重制、减面或添加生产描边壳；这份 `.blend` 是参考研究场景，不能作为 B 成品。

## 配色与光影

| 区域 | sRGB 色值 | 执行说明 |
|---|---|---|
| 主装甲、前部圆弧甲、六腿胫甲、背部环形护架 | `#F1E7D5` 暖白 | 大块连续色面，保留源曲面与规则圆周 |
| 膝甲、足部护板、顶部主色块、炮组外侧面板 | `#E97868` 珊瑚红 | 成对、重复件一致，集中视觉识别 |
| 底盘、暴露连杆、炮管、天线主体 | `#48413E` 深暖灰 | 清楚区分装甲与运动骨架 |
| 轴承、炮口等金属结构 | `#756A60` 暖灰 | 保留有限金属层次，不做高反射写实噪点 |
| 少量状态窗、背部圆形功能指示、天线端部 | `#F4B65C` 琥珀 | 作为拟定功能灯区，保持面积小；本轮不实现功能逻辑 |
| 结构内线与外轮廓 | `#241F20` 暖黑 | 装配接缝/主要折角/色块边界，排除拓扑线框 |

展示使用三档法线光照，明度乘数 `1 / 0.74 / 0.42`，阈值 `0.55 / 0.12`。参考的线条由 Freestyle 显示（内部约 1.65px，外轮廓约 2.2px @ 2K）；生产阶段将改为已计划的独立内线遮罩与简化描边壳。这些展示参数不证明 UE 材质已交付。

## 源结构、尺寸与活动件

源模型：`/Game/Assets/RSG_UnderWater_Pack/FPS/Models/Mech/SK_FPS_Mech`。外包尺寸为 **宽 10.8577m × 前后 8.8608m × 高 7.8912m（含天线）**。保持源尺度；不将角色缩成普通人形尺寸。

| 分件组 | 骨骼/运动依据 | A-v1 保留与 B 要求 |
|---|---|---|
| 根与底盘 | `DeformationSystem → Root_M` | 保留根层级和基座参考姿态 |
| 前、中、后三对机械腿 | `FrontLeg / MiddleLeg / BackLeg 1–5 / End`，各有 `_L/_R` | 六足完整；髋、膝、踝轴心与运动可见连杆按源位置保留，B 采用刚性单骨骼权重 |
| 上部装甲与环形护架 | `Top_M / TopEnd_M` | 保留圆弧前甲、开口环形后护架和原装配间隙 |
| 双侧上/下独立炮组 | `ShotgunTop_L/R`、`ShotgunBot_L/R` | 保留炮组数量、上下关系、炮口、支架与原轴心，不合并或删掉下炮组 |
| 双天线及指示附件 | 随所属源部件 | 保留两根天线和主要剪影；灯区为配色设计 |

UE 源清单记录 **44 根骨骼、7 个动画**：待机、前/后/左/右行走、落地、死亡。动画预览与变形/接地检查在 B 阶段完成，本轮未宣称它们已在重制模型上兼容。

FBX 在 Blender 导入后最上层 `DeformationSystem` 被表现为 Armature 对象，骨骼数据中有 43 根；源 UE 的 44 根完整层级和参考姿态已另存。之后重制绑定及导出回读必须保持该根，不得直接假设 FBX 自动往返无损。

## 减面执行约束（A 通过后）

源 LOD0 本体 68,124 三角面、57,588 顶点。本轮只确认几何基线；目标 LOD 还未制作。

| LOD | 本体上限 | 描边上限 | 合计上限 | 屏幕尺寸 |
|---|---:|---:|---:|---:|
| 0 | 32,000 | 8,000 | 40,000 | 1.0 |
| 1 | 14,000 | 3,000 | 17,000 | 0.40 |
| 2 | 5,000 | 1,000 | 6,000 | 0.16 |
| 3 | 2,000 | 0 | 2,000 | 0.06 |

优先隐藏/重复面和冗余圆柱分段，保留圆形轴承、环形护架、曲面前甲、主要炮组、运动可见内部件。最远档淡出内细线。每档一本体材质区段，近中景另有描边区段；默认统一 2K 图集。预算不达标先报告差额与视觉对比。

## 文件与制作来源

- [UE 源清单与 44 骨骼原姿态](Source/source_manifest.json)、[只读导出结果](Source/export_response.json)、[原始源 FBX](Source/SK_FPS_Mech_Source_LOD0.fbx)。
- [源灰模 Blender 场景](Baseline/RSG_SourceInspection.blend)、[源几何回读](Baseline/blender_source_inspection.json)、[连通分件分析](Baseline/source_connected_parts.json)。
- [A-v1 参考材质研究场景](References_A_v1/RSG_A_v1_ReferenceMaterialStudy.blend)、[相机/配色/法线/对称配色记录](References_A_v1/reference_setup.json)、[版本及哈希清单](References_A_v1/reference_manifest.json)。
- [只读 UE 导出脚本](Scripts/export_source_ue.py)、[灰模渲染](Scripts/render_source_baseline.py)、[结构分析](Scripts/analyze_source_parts.py)、[参考渲染](Scripts/render_style_reference.py)。使用 Blender 5.2.2 LTS。
- 使用 **内置 imagegen** 做过一张效果润色草图，[完整 prompt](References_A_v1/imagegen_Hero_prompt.txt)与[原始草图](References_A_v1/RSG_A_v1_Hero_ImagegenDraft.png)已留档。它的原生尺寸是 1254 × 1254，未满足 2K 要求，且含额外连续高光，**不属于提交审核的四张主图**。没有使用 API/CLI 替代生成，也没有把该草图放大冒充 2K 原图。

## 版本与审核记录

| 阶段 | 版本 | 决定 | 下步 |
|---|---|---|---|
| 参考 A | A-v1（以版本清单中的四张主图为准） | **待用户审核** | 明确通过该版本，或指出需要修订的部位/配色/线条 |
| Blender B | 未开始 | 未审核 | A 通过后严格还原并提交真实成品与 7 动画预览 |
| UE 正式资源 | 未开始 | 未验收 | B 通过后导出回读，再交付 `/Game/GuLiStrike/Robots/RSGMech` |

修改已发布图片必须新建相邻 A-v2 等版本并重新记录哈希；不覆盖已审图。参考与 B 审核相互独立。

[项目需求](../../../Progress/RequirementDocument/20261003-RSG六足机器人美术统一.md) · [制作记录](../../../Progress/DevelopmentDocumentation/20261003-RSG六足机器人美术统一.md) · [项目美术规范](../../../Progress/RequirementDocument/GuLiStrike美术规范.md)
