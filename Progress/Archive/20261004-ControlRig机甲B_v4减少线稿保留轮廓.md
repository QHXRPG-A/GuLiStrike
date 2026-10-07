---
schema: guli-progress/v1
id: ARC-20261004-012
work_id: WORK-20261004-002
kind: archive
role: root
title: ControlRig 机甲 B-v4 减少线稿保留轮廓
areas: [art, assets, rendering]
categories: [art]
status: recorded
verification: partial
created: '2026-10-04'
updated: '2026-10-04'
summary: 按用户要求减少内部线稿，保留基础轮廓和三档；B-v4已显示在当前Blender，四图/三动作及同机位比较留档，B待审核，原面数预算仍超标。
next_action: 用户审核B-v4实际成品及预算差额，B具体版本通过后才正式UE交付。
relations:
  work_items: [WORK-20261004-002]
status_note: 本体、基础轮廓、骨骼与动作严格保存回读一致；内线密度与强度修订，未执行UE导入或性能测量。
art_revision: '1.2'
---

# ControlRig B-v4：减少内部线稿，保留基础轮廓

用户原话“线稿含量低一些，保留基础轮廓”。[实际B-v4交付](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v4/README.md)减少短小内部线、降低主要接缝强度，原基础轮廓壳与三档保持，已放到当前Blender的真实可旋转材质预览。LOD0选边8,477→1,453，内线强度1→0.30、半宽0.010→0.006m；选边下降82.86%只统计模型选线，不代表画面或性能下降比例。四张原生2K、同机位B-v3/B-v4比较、基础轮廓开关图和当前原生Blender截图已留档。

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。


B-v4清单SHA256 `802ade7bbd94e336ec8d6b2fb7fa8dbe36efc7a27a20cb7ba37df34fc144da16`；Blender SHA256 `bf566b78fb3fd03c516016ed0e32bc7ee8412929c303283985340d5d5b5d853b`。B-v3全部341个冻结文件核对未变，B-v3审核历史记录用户减少线稿的修订要求；B-v4待决定，A-v2既有放行有效。实际资源位于 `ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v4`，依赖保留的A-v2/Source/B-v3及新B-v4脚本；制作分件保持，图集/生产网格需要按编辑结果重新生成。

当前记录见[实施文档](../DevelopmentDocumentation/20261004-ControlRig机甲美术统一.md#2026-10-04-b-v4-减少线稿保留基础轮廓)。按用户计划及[制作技能](../../.agents/skills/guli-model-production/SKILL.md)/[规范§2](../RequirementDocument/GuLiStrike美术规范.md#2-制作与审核流程)，具体B版本通过后再导出回读和正式UE导入。

> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../ArtSource/CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。

