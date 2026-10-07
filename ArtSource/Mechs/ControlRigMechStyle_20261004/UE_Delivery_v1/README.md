# ControlRig 机甲 B-v4：UE-v1 正式交付

用户“导入至ue”放行当前 B-v4，已保存至 `/Game/GuLiStrike/Mechs/ControlRigMech`。当前 UE 打开正式模型编辑器，并留有独立展示关卡；三渲二、少量内部结构线、基础外轮廓和四色分区已重建。[实际 UE 图集](Review_UE_v1.html) · [导入放行](../Approvals/Approval_B_v4_Import_20261004.json) · [源版本](../Production_B_v4/README.md)。冻结源文件不改写，旧文件中的“待审核”是提交时状态。

## 使用入口

- 模型：`/Game/GuLiStrike/Mechs/ControlRigMech/Meshes/SKM_ControlRigMech`。
- 骨架：`Skeleton/SKEL_ControlRigMech`；152 骨骼与原名称、层级、参考姿态兼容。
- 动作：`Animations/Mech_Deploy`、`Mech_Idle`、`Mech_Walk`，时长 5 / 8.666667 / 5 秒。
- ControlRig：`Rigs/CR_ControlRigMech`，预览正式模型，31 图/811 节点/1057 连接及控制默认值对照原 Rig 一致。
- 展示：`/Game/GuLiStrike/Mechs/ControlRigMech/Preview/LVL_ControlRigMech_B_v4`，单位 Actor `CRM_B_v4`，相机 `CRM_ReviewCamera`；独立美术展示，没有玩法接入。

上述相对路径均位于正式根目录。交付16个生产资产，另2个展示资产（地图、背景材质）。原 `/Game/Assets/ControlRig/Characters/Mech` 保留；源本就没有物理资产和挂点，正式副本同样为null/0。[原生资产及依赖清单](final_live_inventory.json)。外部依赖：`/ControlRig/Controls/DefaultGizmoLibraryNormalized`, `/Engine/Animation/DefaultAnimCurveCompressionSettings`, `/Engine/Animation/DefaultRecorderBoneCompression`, `/Engine/Animation/DefaultVariableFrameStrippingSettings`, `/Engine/BasicShapes/Cube`, `/Engine/EditorResources/LightIcons/S_LightError`, `/Engine/EngineMaterials/WorldGridMaterial`。

## 着色与网格

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../../../../Progress/RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。


## 回读和实际动画


三个动画由源UE的原生30fps全部152骨骼局部T/Q/S采样，经AnimationDataController重建到正式骨架，不是二进制复制。部署的真实位移、伸缩、缩放与原速保留，未归一化。原始局部姿态三点回读匹配；[实际组件回读](ue_preview_readback_report.json)对照源与正式组件的三动作首/中/尾，共9×152骨骼，最大位置差0.00184006cm、旋转差0.00031597°。UE播放的压缩姿态与原始轨道有约0.8mm差异，源组件也存在；不能把压缩/原始差异误记为导入误差。原生API确认源/正式三动作均无Float曲线、无Notify且additive类型一致；额外数据见资产清单。


## 留档与边界

源B-v4清单SHA256 `802ade7bbd94e336ec8d6b2fb7fa8dbe36efc7a27a20cb7ba37df34fc144da16`；Blender SHA256 `bf566b78fb3fd03c516016ed0e32bc7ee8412929c303283985340d5d5b5d853b`。[冻结源完整性](source_integrity_report.json)及[UE交付文件清单](delivery_manifest.json)分别登记。UE资产通过编辑器API核验，未读取或哈希uasset/umap内容。

未改玩法身份、C++、Ground/重防号引用或原资源包；未编译、未运行PIE、未测实战FPS。当前完成该B-v4版本的UE导入，预算优化与实战性能仍是待处理项。复现脚本位于Scripts；已有加载资产通过当前Editor处理，独立导入脚本仅在其安全工作进程入口执行，不能与当前Editor并发写同一资产。

> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../../CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。


