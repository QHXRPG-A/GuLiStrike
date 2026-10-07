# ControlRig 机甲 B-v4：减少内部线稿，保留基础轮廓

依据用户“线稿含量低一些，保留基础轮廓”。已在当前 Blender 显示本版，默认场景 `STYLE_REALTIME_B_v4`，两个真实可旋转材质预览窗口。

[Blender源文件](ControlRigMech_B_v4_Production.blend) · [当前Blender截图](Blender_Live_B_v4.png) · [B-v3/B-v4同机位对照及完整动作](Review_B_v4.html) · [冻结清单](production_manifest.json)

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../../../../Progress/RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。

**基础轮廓壳的几何、法线、权重和宽度完整保留**，LOD0/1/2宽度仍0.018/0.020/0.025m。色块和三档系数0.42/0.74/1保持，所有成品图和动作均关闭Freestyle。原装甲、炮管、四足、关节和内部机械结构未删除；本次只改内部线的选择字段、线宽、强度及遮罩。

[仅基础轮廓](LineDiagnostics/ControlRigMech_B_v4_BaseOutlineOnly.png) / [基础轮廓＋少量内线](LineDiagnostics/ControlRigMech_B_v4_SparseLines.png)，来自同一真实模型，没有图片叠加。四图原生2048²，同姿态、同尺度、同正交相机：

| 视图 | 原B-v3 | 新B-v4 |
|---|---|---|
| 三分之四 | [较密内线](../Production_B_v3/ControlRigMech_B_v3_Hero.png) | [少量内线](ControlRigMech_B_v4_Hero.png) |
| 正面 | [较密内线](../Production_B_v3/ControlRigMech_B_v3_Front.png) | [少量内线](ControlRigMech_B_v4_Front.png) |
| 左侧 | [较密内线](../Production_B_v3/ControlRigMech_B_v3_Left.png) | [少量内线](ControlRigMech_B_v4_Left.png) |
| 背面 | [较密内线](../Production_B_v3/ControlRigMech_B_v3_Back.png) | [少量内线](ControlRigMech_B_v4_Back.png) |

## 保存回读与动作


[完整部署](AnimationPreviews/ControlRigMech_B_v4_Deploy.mp4) / [完整待机](AnimationPreviews/ControlRigMech_B_v4_Idle.mp4) / [完整行走](AnimationPreviews/ControlRigMech_B_v4_Walk.mp4)。全283帧按本版真实shader重渲染，1280²/15fps，源30fps取样、保留精确两端。视频已原生解码核对帧数、尺寸和9个首/中/尾检查点，均在有损编码容差内，[检查点报告](AnimationPreviews/movie_checkpoint_fidelity.json)；本轮未逐帧比对所有解码帧。源动作位移、伸缩、缩放和速度不变。

## 原预算及交付边界


本版是ControlRig机甲的局部线稿方向修订，不修改其他资产和全局规范v1.2。A-v2既有放行有效；B-v3收到减少线稿的修订要求，B-v4待用户针对本版决定，[审核记录](../review_decisions.json)。按用户已定计划与[制作技能](../../../../.agents/skills/guli-model-production/SKILL.md)/[美术规范§2](../../../../Progress/RequirementDocument/GuLiStrike美术规范.md#2-制作与审核流程)“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”，B通过后再正式导入；本轮UE目录、源包、玩法引用和C++均未改动。Blender SHA256 `bf566b78fb3fd03c516016ed0e32bc7ee8412929c303283985340d5d5b5d853b`。

> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../../CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。


