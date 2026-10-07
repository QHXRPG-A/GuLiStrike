# ControlRig 机甲 B-v3：实际模型三渲二、线稿、三档明暗

用户指出“所以线稿没搞？”并再次明确“三渲二，线稿，三档明暗”。B-v2 的主展示依赖 Freestyle，实际材质内线未达到同样效果；此前直接回答“做了”不准确。本版修正可旋转实际模型的材质与描边，不沿用展示图作为材质完成证据。**A-v2 已通过，B-v3 待审核，预算仍超标，UE 正式导入未执行。**

[当前 Blender 文件](ControlRigMech_B_v3_Production.blend) · [同机位参考/实际线条开关/动作对照](Review_B_v3.html) · [当前 Blender 原生截图](Blender_Live_B_v3.png) · [版本清单](production_manifest.json)

最终 Blender SHA256：`6ceea9e11e49203cd00e8cdfbd24f75de99c6ff2af6ba5239a641d3b08b037c1`。文件默认场景 `STYLE_REALTIME_B_v3`，两个窗口均为真实 3D 材质预览，左三分之四、右背面；可以直接旋转。已移除 B-v3 文件内旧的 `REVIEW_B_ReferenceMatched` 展示场景，旧 B-v2 文件完整保留。

## 实际着色与线稿

三档为亮部 / 中间色 / 阴影，线性系数 1.0 / 0.74 / 0.42，Constant 阶梯、阈值 0 / 0.12 / 0.55，固定艺术光向 (0.35, -0.55, 0.76)。分色沿已审四色：`#557B78` 主装甲、`#8E3A2A` 炮口/检修护甲、`#2C3735` 骨架/软管、`#D5C09C` 关节盖与原镜片；线色 `#1B2422`。

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../../../../Progress/RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。

实际同机位开关图：[关闭线条](LineDiagnostics/ControlRigMech_B_v3_NoLines.png) / [仅结构线](LineDiagnostics/ControlRigMech_B_v3_InternalOnly.png) / [仅轮廓壳](LineDiagnostics/ControlRigMech_B_v3_OutlineOnly.png) / [全部线条](LineDiagnostics/ControlRigMech_B_v3_AllLines.png)。这些来自同一个实际模型和材质。

## 2K 图纸与参考

四图原生 2048 × 2048，姿态、正交相机及尺度沿用已审 A-v2。

| 视图 | 已审 A-v2 | 实际 B-v3 |
|---|---|---|
| 三分之四 | [参考](../References_A_v2/ControlRigMech_A_v2_Hero.png) | [实际成品](ControlRigMech_B_v3_Hero.png) |
| 正面 | [参考](../References_A_v2/ControlRigMech_A_v2_Front.png) | [实际成品](ControlRigMech_B_v3_Front.png) |
| 左侧 | [参考](../References_A_v2/ControlRigMech_A_v2_Left.png) | [实际成品](ControlRigMech_B_v3_Left.png) |
| 背面 | [参考](../References_A_v2/ControlRigMech_A_v2_Back.png) | [实际成品](ControlRigMech_B_v3_Back.png) |

## 预算差额


## 动作、法线和制作源

[部署](AnimationPreviews/ControlRigMech_B_v3_Deploy.mp4) / [待机](AnimationPreviews/ControlRigMech_B_v3_Idle.mp4) / [行走](AnimationPreviews/ControlRigMech_B_v3_Walk.mp4)。本版实际 shader + 壳重新渲染全部 283 帧：1280²、15fps，原动作 30fps 取样，分别 76 / 131 / 76 帧，保留精确两端及完整 5 / 8.666667 / 5 秒。所有视频帧已解码与对应原生渲染帧比较，均在有损编码容差内，[报告](AnimationPreviews/movie_frame_fidelity_report.json)。部署原位移、伸缩和缩放保留；没有修改动作速度。检查了首、中、尾实际图和完整预览，未声称自动覆盖全部时刻穿插。


761 个制作分件、86 个已核对镜像、源表面修正和源完整三动作仍保留。隐藏 `PRODUCTION_LODS_SINGLE_BODY_SECTION` 并显示 `EDITABLE_MECHANICAL_PARTS` 可继续编辑制作分件；该集合是制作源，修改后须重新生成 LOD/结构距离 UV/图集，不会自动更新本版生产网格。源四足、单炮、比例、关节轴心和原单侧天线保留，不虚构全部从空白重拓扑。B-v2 的源表面/脚底检查作为未改变本体与绑定的基线留档，不能冒充新增全时段碰撞检测。

## 贴图、迁移与审核边界

四张 2K：[BaseColor](Textures/ControlRigMech_BaseColor_2K.png)、[独立内线遮罩](Textures/ControlRigMech_InternalLineMask_2K.png)、[功能遮罩](Textures/ControlRigMech_FunctionalMask_2K.png)、[ORM](Textures/ControlRigMech_ORM_2K.png)。贴图已打包，目录内完整副本可随文件移动。当前颜色仍使用 `GuLi_PaletteLinear` 逐角颜色准确分色，功能遮罩和 ORM 尚未完整接入。四张 RGBA8 未压缩共 64 MiB，不是 UE 压缩驻留预算。

正式 UE 等效材质需保留全部三个 UV 通道及逐角颜色，再重建阶梯与结构距离逻辑；该迁移、UE 法线/动画导出回读、ControlRig 副本、三档指挥官镜头和实战帧率都未执行。当前 Blender 视觉已可直接审核，不能据此认定 UE 外观或批量性能通过。源无物理资产、挂点为 0；本轮无 UE 源包、玩法身份、C++ 或战斗引用修改。

A-v2 原用户放行仍有效，[决定记录](../review_decisions.json)未把重复强调视觉要求当作 B 通过。按用户已定计划及[制作技能](../../../../.agents/skills/guli-model-production/SKILL.md)的“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”，B-v3 具体版本通过后才导出回读并导入 `/Game/GuLiStrike/Mechs/ControlRigMech`。旧 A/B 冻结文件全部哈希复核未改。

> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../../CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。

