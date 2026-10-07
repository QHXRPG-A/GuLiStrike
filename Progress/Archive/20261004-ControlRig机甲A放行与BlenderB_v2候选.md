---
schema: guli-progress/v1
id: ARC-20261004-010
work_id: WORK-20261004-002
kind: archive
role: root
title: ControlRig 机甲 A 放行与实际 Blender B-v2 候选
areas: [art, assets, rendering]
categories: [art]
status: recorded
verification: partial
created: '2026-10-04'
updated: '2026-10-04'
summary: 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。
next_action: 用户审核B-v2及预算差额，修改项处理后再审，B通过后才导出回读及正式UE副本。
relations:
  work_items: [WORK-20261004-002]
status_note: A已通过；B-v1由助手终检撤回，B-v2待审核；UE正式目录未开始，未冒充预算或性能通过。
art_revision: '1.2'
---

# 2026-10-04：ControlRig A 放行与 Blender B-v2

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。


B-v2清单SHA256 `79018077be8ed6c1ce1d1679594fcb8b01040dd85918d169251f1629818d6fa9`；Blender SHA256 `8e94a4547875ba54b731770b77297a229fbe36e53112a634624bb6502dee88ef`。旧A-v1/A-v2全部41个冻结文件保持原样，B-v1冻结文件亦保留为撤回历史。当前[审核决定](../../ArtSource/Mechs/ControlRigMechStyle_20261004/review_decisions.json)为A通过/B-v2待审核。规范v1.2、源资源包、C++和战斗引用保持；UE导出回读、正式模型/骨架/CR/动画副本、UE镜头和实战帧率尚未运行。

## 资源清单

| 项目内最终文件夹 | 来源与内容 | 依赖及边界 |
|---|---|---|
| `ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v2` | 从已审实际源及制作分件生成的Blender、2K图、图集、LOD与真实视频/报告；未重新从外部ZIP迁移 | 依赖本资产冻结A-v2、Source/三动画只读采样及Scripts；无示例地图或外部Actor/Object支持目录；未导入UE |
| `.../Production_B_v2/AnimationSource` | UE源3动画/152骨骼全30fps只读本地TQS；源完整位移与缩放 | 独立工作进程只读导出并退出自身，原包未保存；非运行时/PIE验收 |
| `.../Scripts` | 本轮原生Blender分件、骨架、法线、贴图/壳、保守LOD、真实动作渲染/解码与归档脚本 | B-v1已冻结脚本保留；新B-v2脚本另存，不改旧参考或冻结版本 |

按用户计划、[制作技能](../../.agents/skills/guli-model-production/SKILL.md)和[规范§2](../RequirementDocument/GuLiStrike美术规范.md#2-制作与审核流程)，B具体版本通过后才继续导出回读与 `/Game/GuLiStrike/Mechs/ControlRigMech` 正式交付。事实台账见[实施文档](../DevelopmentDocumentation/20261004-ControlRig机甲美术统一.md)。

> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../ArtSource/CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。


