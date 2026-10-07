---
schema: guli-progress/v1
id: ARC-20261004-011
work_id: WORK-20261004-002
kind: archive
role: root
title: ControlRig 机甲 B-v3 实际线稿与三档明暗
areas: [art, assets, rendering]
categories: [art]
status: recorded
verification: partial
created: '2026-10-04'
updated: '2026-10-04'
summary: 按用户实时线稿反馈修订B-v3，实际材质接入结构线/轮廓与三档明暗，当前Blender双3D预览可旋转；四图、真实三动作和预算差额已交付，B待审核。
next_action: 用户审核B-v3视觉与预算差额；B具体版本通过后才导出回读及正式UE交付。
relations:
  work_items: [WORK-20261004-002]
status_note: A-v2通过仍有效，B-v3待审核；全部本体及LOD0–2描边预算未满足，UE与实战帧率未验证。
art_revision: '1.2'
---

# 2026-10-04：ControlRig B-v3 实际线稿与三档明暗

用户指出“所以线稿没搞？”并再次明确“三渲二，线稿，三档明暗”。B-v2的主展示使用Freestyle，实际shader内线不足；此前直接回答“做了”不准确。当前[B-v3实际交付](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v3/README.md)已将机械结构边缘距离/独立2K内线遮罩、反向骨骼壳、三档阶梯材质应用于真实模型，所有新图和动作关闭Freestyle。当前Blender显示两个可旋转3D材质窗口，原生截图与实际线条开关对照留档。

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。


B-v3清单SHA256 `7b9196d4c356751e7eeef763a4a4e3c57966161e132ea7324aef648eebe0e2bf`；Blender SHA256 `6ceea9e11e49203cd00e8cdfbd24f75de99c6ff2af6ba5239a641d3b08b037c1`。A冻结41文件、B-v1冻结352文件、B-v2冻结355文件全部原哈希不变。审核决定将B-v2记为按用户反馈修订，B-v3待审核，A-v2原放行有效；没有补写用户B通过。规范v1.2、UE源包、C++、玩法/战斗引用均未改。导出回读、正式UE副本、ControlRig副本、UE三档指挥官镜头和实战帧率未执行。

## 资源清单


按用户计划及[制作技能](../../.agents/skills/guli-model-production/SKILL.md)的“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”，待B-v3具体版本决定再推进正式交付。当前事实与检查范围见[实施文档](../DevelopmentDocumentation/20261004-ControlRig机甲美术统一.md#2026-10-04-b-v3-实际线稿与三档明暗)。

> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../ArtSource/CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。


