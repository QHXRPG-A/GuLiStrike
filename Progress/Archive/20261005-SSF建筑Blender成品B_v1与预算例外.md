---
schema: guli-progress/v1
id: ARC-20261005-011
work_id: ''
kind: archive
role: root
title: SSF建筑Blender成品B_v1与预算例外
areas: [art, assets, rendering]
categories: [art]
status: recorded
verification: partial
created: '2026-10-05'
updated: '2026-10-05'
summary: A_v7依用户制作指令放行；实际B_v1十类资产、三档LOD、22完整动作与回读交付，24档本体超额及用户B待审，正式UE未导入。
next_action: 用户审核具体SSF_Production_B_v1外观/运动及24项本体差额，决定预算例外或调整后再审；B后导出回读并正式UE交付。
relations:
  work_items: [WORK-20261005-003]
status_note: 原生几何/骨架/动作与视频检查通过，技术通过不替代用户B或预算例外；部分描边弱于参考，导出/引擎与帧率未验收。
art_revision: '1.3'
---

# 2026-10-05：SSF A_v7放行后实际 Blender B_v1

## 依据与范围

用户明确“开始制作，严格一比一按照参考图和原模型制作”，作为已展示A_v7的制作授权，[A决定](../../ArtSource/Buildings/SSFStyle_20261005/approval_A.json)固定清单 `d1d3dcabdac432395edd1554da297de47e555a94c9e6ff77893043a7487c0173`。未把该指令当作尚未展示成品的B或正式UE通过。处理六座建筑与Floor/Lamp/Light/Drone，保留22原动画名及源商店包。

## 实际制作与资源清单

| 最终项目内目录 | 来源 | 内容与用途 | 依赖与边界 |
|---|---|---|---|
| `ArtSource/Buildings/SSFStyle_20261005/Production_B_v1` | `/Game/Assets/SSF_Buildings/Buildings` 的API导出源与已审A_v7 | Blender实际分件/镜像/修改器、十类资产三档LOD、7骨架355骨骼、22原名Action、38新贴图、原始渲染/图板/视频与回读 | 原骨骼/轴心、柔性克隆软管及半透明显示保留；源贴图与样本在同任务Source/AnimationSource；不迁入示例关卡/外部Actor目录，不修改UE源包。 |
| `ArtSource/Buildings/SSFStyle_20261005` | 本任务现有参考/输入/Source | A授权记录、B具体审核页、版本决定与冻结清单 | A_v1–A_v7冻结不改；工作中间blend/日志不列为正式成品交付。 |
| `/Game/GuLiStrike/Buildings/SSFStylized` | B审核通过后拟交付 | 本轮尚未创建正式资产；规划网格/骨架/动画/物理资产/材质/蓝图副本 | FBX导出回读、UE引用/材质/三档镜头留在B后；本行只定位后续正式资源目标。 |

清理兼容权重的重合点、冗余共面/退化面及安全分段，保持每档全部792制作分件。真实Root与原名称/层级恢复，镜像不交换左右武器权重，金属刚性，克隆软管按源柔性。固定艺术光、A_v7色块/三档因子、内部RG遮罩、真实绑定描边壳与远档线稿淡出已制作。

六座建筑、平台和无人机各三档，共24档本体超出原上限，[实际差额](../../ArtSource/Buildings/SSFStyle_20261005/Production_B_v1/Budget_Exceptions.md)与十套同机位LOD对比提交例外；原预算未更改。描边全部达预算，但仅在主要构件覆盖，部分轮廓弱于参考Freestyle，明确提交B。灯柱48面/灯片2面不强行减少，近中最多3区段。

## 证据与验证

[完整B审核](../../ArtSource/Buildings/SSFStyle_20261005/Review_B_v1.md)包括42张2K效果/正交图、十套实际三视图图板、十套参考同机位/LOD对比、两张4K原尺寸组合、实际材质拆分。22完整MP4三个真实LOD同播，15fps共3012帧，准确起止；30fps源6003姿态留档。全部22MP4实际解码检查帧数/1920×768/15fps，110回读检查帧与原渲染对照。

[网格/权重/材质核对](../../ArtSource/Buildings/SSFStyle_20261005/Production_B_v1/native_validation.json)与[实际22动作×3LOD变形](../../ArtSource/Buildings/SSFStyle_20261005/Production_B_v1/animation_deformation_validation.json)通过，后者每动作5点对全部顶点最大位置差5.888243316e-6m，实际骨骼矩阵最大差4.768371582e-6。助手[视觉读图](../../ArtSource/Buildings/SSFStyle_20261005/Production_B_v1/visual_qa.json)单列，不等于用户B。

成品Blender SHA256 `6f386c2e4782ce6ab6e81ae0f2380a6345fa05cdc6fc3dcf9d260c13ed3a806e`；[冻结清单](../../ArtSource/Buildings/SSFStyle_20261005/Production_B_v1/production_manifest.json)SHA256 `47fa0b49979828e331ca702d526f18d65b63ff639bd72404390288f6894adf29`；[交付核对](../../ArtSource/Buildings/SSFStyle_20261005/DeliveryValidation_B_v1.json)验证496个文件及七个历史参考版本。38贴图文件大小与RGBA8等价预算记录，不宣称引擎压缩/实际驻留或帧率达标。

## 尚未完成与用户决定

B_v1及24项本体预算例外均待用户明确决定。Blender与源运动检查只证明本阶段制作，FBX导出回读和UE正式副本/物理资产/22动画/无人机蓝图、引用重载、自动LOD与项目镜头尚未执行，实战帧率未测。未运行PIE/原生构建或覆盖商城源包。按用户流程与模型技能提交实际可审产物后止于B，全局美术规范仍v1.3。
