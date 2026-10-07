# ControlRig 机甲实际 Blender 候选 B-v2

A-v2 已由用户明确通过，依据为“审核通过，blender已开，根据参考图和源模型一比一制作”。[A 放行记录](../Approvals/Approval_A_v2_20261004.json)锁定清单 SHA256 `ea47e95d2e5ee1f8d2bfe3ccd1fd9b249873e99fe31d93f9c9935b72af1ed9ac`。**当前 B-v2 待审核，预算与可迁移细线仍有差额；未导入正式 UE 目录。**

[可交互参考对照与动作](Review_B_v2.html) · [最终可编辑 Blender](ControlRigMech_B_v2_Production.blend) · [冻结清单](production_manifest.json) · [当前决定](../review_decisions.json)

最终 Blender SHA256：`8e94a4547875ba54b731770b77297a229fbe36e53112a634624bb6502dee88ef`。保持源四足、单主炮、活塞、足爪、软管、检修盖和原单侧天线；没有新增武器或玩法身份。按源部件与已审分色整理了 761 个可编辑对象，保留 86 个经源几何核对的镜像修改器、两个按源剖面制作的 32 分段旋转轮廓、受控减面、源法线转移和足爪表面贴合修改器。主要圆形关节盖保留完整源轮廓。不能把这项工作描述为所有部件均从空白重拓扑。

## 效果、三视图和同机位参考

四张均为原生 2048 × 2048，姿态、正交相机、尺度和配色沿用已审 A-v2。

| 视图 | 审核 A-v2 | 实际 B-v2 | 实际遮罩＋描边壳 |
|---|---|---|---|
| 三分之四 | [参考](../References_A_v2/ControlRigMech_A_v2_Hero.png) | [成品](ControlRigMech_B_v2_Hero.png) | [材质预览](Portable/ControlRigMech_B_v2_Portable_Hero.png) |
| 正面 | [参考](../References_A_v2/ControlRigMech_A_v2_Front.png) | [成品](ControlRigMech_B_v2_Front.png) | [材质预览](Portable/ControlRigMech_B_v2_Portable_Front.png) |
| 左侧 | [参考](../References_A_v2/ControlRigMech_A_v2_Left.png) | [成品](ControlRigMech_B_v2_Left.png) | [材质预览](Portable/ControlRigMech_B_v2_Portable_Left.png) |
| 背面 | [参考](../References_A_v2/ControlRigMech_A_v2_Back.png) | [成品](ControlRigMech_B_v2_Back.png) | [材质预览](Portable/ControlRigMech_B_v2_Portable_Back.png) |

`REVIEW_B_ReferenceMatched` 渲染的是与生产 LOD0 **位置、拓扑、角法线和权重哈希一致**的实际网格，线稿使用 Blender 原生 Freestyle；其展示材质关闭图集内线，场景排除外轮廓壳。`PORTABLE_SHADER_LODS` 使用实际独立 2K 内线遮罩和预算内/带差额的反向法线壳，关闭 Freestyle。两者都是真实 Blender 结果。当前遮罩细线比已审参考偏软，局部窄倒角和关节盖仍有简化痕迹，不能将主展示的线稿精度当成已迁移材质的精度。正式 UE 交付前需继续处理该差异。

三档线性系数 0.42 / 0.74 / 1.0，阈值 0 / 0.12 / 0.55，固定艺术光向 (0.35, -0.55, 0.76)。灰青 `#557B78` 主装甲、铁锈红 `#8E3A2A` 炮口/检修护甲、深青灰 `#2C3735` 骨架/软管、沙米色 `#D5C09C` 金属盖/功能镜片，线色 `#1B2422`。真实装甲与装配线，不渲染三角网格线。

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../../../../Progress/RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。


## 完整实际动作与结构核对

[部署](AnimationPreviews/ControlRigMech_B_v2_Deploy.mp4) · [待机](AnimationPreviews/ControlRigMech_B_v2_Idle.mp4) · [行走](AnimationPreviews/ControlRigMech_B_v2_Walk.mp4)。预览 1280 × 1280 / 15fps，逐帧从原 30fps 曲线取样，包含精确两端；分别为 76 / 131 / 76 帧，完整覆盖原 5 / 8.666667 / 5 秒，视频多保留一个末端显示帧。[逐帧视频核验](AnimationPreviews/movie_frame_fidelity_report.json)将全部 283 个解码帧与对应实际渲染 PNG 比较，均在 H264 有损编码容差内；这只核验视频重现，不代替全部骨骼姿态或穿插检查。未改动画速度、位移、伸缩或缩放。源 152 骨骼全部名称和父子关系核对一致；恢复了 FBX 默认导入转为对象的 root。金属分件刚性绑定，原软管和连接件保留必要混合权重。三个动作的真实全帧 UE 只读采样保存在 [AnimationSource](AnimationSource/capture_manifest.json)，原包未保存。

构建时三点姿态矩阵与源采样最大元素差 2.3842e-6；这只证明 Blender 中的采样姿态恢复，不代表 UE 导出回读已通过。部署 base 原 87.38→192.8959 cm 位移与 cannon_02 原 0.516426→1 缩放保留。

曾发现减面足爪下偏 2.6 cm，已按各自源足爪表面贴合后重渲染所有预览。[脚底对照](motion_contact_audit.json)记录 9 个关键姿态：末部署/待机/行走脚底相对源差最大 0.113 mm，展开过程最大 6.509 mm；未改骨骼或抹除源部署变形。完整视频可查看活动件、软管和足爪运动，未声称自动覆盖所有时刻的穿插和接地。


## Blender 编辑、贴图与交付边界

打开文件默认显示实际成品渲染与可操作 3D 网格。[已打开的 Blender 截图](Blender_Live_B_v2.png)。原生模型为 Blender 5.2.2 LTS / EEVEE。编辑分件时切换 `PORTABLE_SHADER_LODS`，隐藏 `PRODUCTION_LODS_SINGLE_BODY_SECTION`，开启 `EDITABLE_MECHANICAL_PARTS` 的显示；源模型/法线辅助/原镜像右侧备份保持独立。修改分件后须重新生成对应 LOD/图集，展示副本不会自动重建。选择 Armature 后在 Action Editor 可切换三个 `ControlRigMech_Mech_*_Source30fps` 动作；源完整数据和转换脚本已保存。

四张 2048² PNG：[BaseColor](Textures/ControlRigMech_BaseColor_2K.png)、[独立内线](Textures/ControlRigMech_InternalLineMask_2K.png)、[功能遮罩](Textures/ControlRigMech_FunctionalMask_2K.png)、[ORM](Textures/ControlRigMech_ORM_2K.png)，均已打包到 .blend。BaseColor 不烘焙光照。当前 shader 用 `GuLi_PaletteLinear` 角点颜色保证微小岛分色准确，BaseColor 图集保留作可迁移数据；功能遮罩与 ORM 已生成但尚未完整接入展示 shader。RGBA8 未压缩四图合计 64 MiB，不等于 UE 压缩驻留预算或实战帧率。制作详情见 [渲染与预算记录](production_render_report.json)。

B 尚未取得用户通过；预算和可迁移细线仍需审核与处理。按用户计划、[制作技能](../../../../.agents/skills/guli-model-production/SKILL.md)及[美术规范 §2](../../../../Progress/RequirementDocument/GuLiStrike美术规范.md#2-制作与审核流程)的“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”流程，B 放行后再导出回读和正式建立 `/Game/GuLiStrike/Mechs/ControlRigMech`。本轮没有 UE 正式资源、ControlRig 副本、物理资产、挂点或玩法代码改动；原 source 无物理资产、挂点为 0。UE 镜头、ControlRig 副本运行、最终动画兼容、导出回读及实战帧率尚未验证。

> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../../CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。


