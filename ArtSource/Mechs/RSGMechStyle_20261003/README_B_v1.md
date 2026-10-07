# RSG 六足机器人 — Blender B-v1 实际成品待审核

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../../../Progress/RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。

![实际 Blender B-v1](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/ReviewImages/RSG_B_v1_Hero.png)

[Blender 实际源文件](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/RSGMech_Production_B_v1.blend) · [与已审参考同机位对照](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/ReferenceComparison.md) · [头部凹块细节](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/ReviewImages/RSG_B_v1_HeadDetail.png)

四张成品图均 2048 × 2048：[三分之四](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/ReviewImages/RSG_B_v1_Hero.png) · [正面](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/ReviewImages/RSG_B_v1_Front.png) · [左侧](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/ReviewImages/RSG_B_v1_Left.png) · [背面](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/ReviewImages/RSG_B_v1_Back.png)

## 七个原动作预览

真实成品 EEVEE 渲染，1080 × 1080、30fps。落地镜头随竖直位移跟踪，死亡镜头扩大视野；不改变原骨骼动作。视频包含首尾样本，比原动作区间多一帧播放时间。所有 MP4 已原生解码核对尺寸与帧数。

- [待机](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/AnimationPreviews/A_FPS_Mech_Idle_01.mp4)
- [前行](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/AnimationPreviews/A_FPS_Mech_WalkForward_01.mp4)
- [后行](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/AnimationPreviews/A_FPS_Mech_WalkBackward_01.mp4)
- [左行](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/AnimationPreviews/A_FPS_Mech_Walk_L_01.mp4)
- [右行](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/AnimationPreviews/A_FPS_Mech_Walk_R_01.mp4)
- [落地](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/AnimationPreviews/A_FPS_Mech_Landing_01.mp4)
- [死亡](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/AnimationPreviews/A_FPS_Mech_Death_01.mp4)


## 制作与绑定记录

编辑集合保留 208 个主分件、184 个镜像修改器及减面/骨架修改器；源集合只作隐藏对照。全尺寸比例取自原模型，外包尺寸差小于 6mm。恢复真正的 DeformationSystem 根骨骼，44 根骨骼名称和父子关系与原 UE 基线一致；所有成品顶点为单骨骼权重 1。原七动作已保存为 Actions 与默认静音的 NLA 轨道。

动画查看：选 Armature，打开 Action Editor，选择 A_FPS_Mech 动作；当前默认无活动动作，展示参考姿态。切换 LOD 时只显示对应 03_LODn_Review 集合中的本体/描边。

全 311 帧源姿态检查：最大骨骼位置差 0.021mm，刚性分件距离差 0.0063mm，六足最低点相对源模型最大变化 9.3mm。源动画自身相对零高度平面仍有接地偏差：待机约 -0.008m、落地 -0.172m、四向行走最低 -0.274m、死亡最低 -0.831m。本轮保持原动画，未更改 IK/曲线；这项记录不代表无穿地或游戏内接地验收通过。

## 材质、贴图与版本


[BaseColor 2K](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/Textures/T_RSGMech_BaseColor.png) · [独立线稿/灯遮罩 2K](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/Textures/T_RSGMech_LineMask.png)

两图约 1.81MiB PNG，已打包进 .blend。按 RGBA8 保守估计，基础层合计 32MiB、完整 mip 链约 42.67MiB；实际 UE GPU 压缩和实战帧率待引擎交付验证。制作环境 Blender 5.2.2 LTS / UE5.7，导出回读和正式 UE 版本尚未产生。

[A-v3 用户通过记录](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/approval_A_v3_20261003.json) · [红框定位与改色记录](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/color_amendment_confirmed_20261003.json) · [面数/区段/骨架/纹理/视频核对](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/review_technical_validation.json) · [全帧刚性与接地对照](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/motion_geometry_checks.json) · [B-v1 文件哈希清单](D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/review_manifest.json)

## 审核与后续

当前 B-v1 为实际成品待审核，没有登记用户 B 通过。通过后按原计划先导出回读，再新建 /Game/GuLiStrike/Robots/RSGMech 正式副本，原资源包保留，完成物理资产、七动画副本、等效 UE 着色与三档指挥官镜头验证。

依据用户原计划以及 [模型制作技能](D:/UE5.7/test1/.agents/skills/guli-model-production/SKILL.md) 与 [美术规范](D:/UE5.7/test1/Progress/RequirementDocument/GuLiStrike美术规范.md) 中的“用户审核 B → UE 导入”，本次交付停在 B，不提前写入正式 UE 资源。

> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。

