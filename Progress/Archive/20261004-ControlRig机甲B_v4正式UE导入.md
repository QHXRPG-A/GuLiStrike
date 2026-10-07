---
schema: guli-progress/v1
id: ARC-20261004-013
work_id: WORK-20261004-002
kind: archive
role: root
title: ControlRig机甲B-v4正式UE导入
areas: [art, assets, rendering]
categories: [art]
status: recorded
verification: partial
created: '2026-10-04'
updated: '2026-10-04'
summary: 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。
next_action: 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。
relations:
  work_items: [WORK-20261004-002]
status_note: 纯美术导入完成；审核放行不代替预算和实战性能验收，未编译或运行PIE，原包和玩法/C++不改。
art_revision: '1.2'
---

# 2026-10-04：ControlRig B-v4 正式UE导入

用户“导入至ue”放行当前简线成品，[独立放行](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Approvals/Approval_B_v4_Import_20261004.json)锁定B-v4清单SHA256 `802ade7bbd94e336ec8d6b2fb7fa8dbe36efc7a27a20cb7ba37df34fc144da16`及Blender `bf566b78fb3fd03c516016ed0e32bc7ee8412929c303283985340d5d5b5d853b`。333个冻结源文件核对未变，不重写旧审核快照；[完整交付](../../ArtSource/Mechs/ControlRigMechStyle_20261004/UE_Delivery_v1/README.md)和[当前决定](../../ArtSource/Mechs/ControlRigMechStyle_20261004/review_decisions.json)登记新状态。

## 资源清单

以下路径均以`/Game/GuLiStrike/Mechs/ControlRigMech`为正式根，外部制作来源`ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v4`及其AnimationSource；原包为`/Game/Assets/ControlRig/Characters/Mech`，全部保留。

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。


共16个生产资产+2个展示资产。原包未保存/覆盖，源44资产中的其他未用贴图/示例/场景不迁入。当前关卡即`/Game/GuLiStrike/Mechs/ControlRigMech/Preview/LVL_ControlRigMech_B_v4`，正式模型编辑器已打开；没有外部Actor支持目录。实际外部依赖及全部包路径见[原生清单](../../ArtSource/Mechs/ControlRigMechStyle_20261004/UE_Delivery_v1/final_live_inventory.json)，核心Engine/ControlRig依赖保留，正式资产引用经原生查询不再指向原Mech目录。

## 验证与差额


> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../ArtSource/CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。

