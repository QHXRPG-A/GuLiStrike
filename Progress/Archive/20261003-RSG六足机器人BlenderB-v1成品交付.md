---
schema: guli-progress/v1
id: ARC-20261003-004
work_id: ''
kind: archive
role: root
title: RSG 六足机器人 Blender B-v1 成品交付
areas: [art, assets, rendering]
categories: [art]
status: recorded
verification: partial
created: '2026-10-03'
updated: '2026-10-03'
summary: 指挥官LOD说明已按2026-10-05用户指令勘误，当前总共三档；历史源保留，新的实际版本待审核。
next_action: 用户审核实际 B-v1，通过后执行导出回读与正式 UE 新副本导入。
relations:
  work_items: [WORK-20261003-001]
status_note: B 用户决定待收到；未写入正式 UE。原动画接地偏差已记录，Blender 骨骼/刚性检查不等同 UE 动画兼容终验。
---

# 2026-10-03：RSG Blender B-v1 实际成品

用户明确“审核通过，blender已开，开始制作…”后，锁定 [A-v3 用户通过记录](../../ArtSource/Mechs/RSGMechStyle_20261003/approval_A_v3_20261003.json)，未重问 A。Blender 实际生产保留六足、双侧上下炮组、原比例与关节，完成 208 编辑主件、184 镜像对、44 真正骨骼及原七动作。

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。

用户制作中用红框明确头球中央凹嵌面板改天蓝，沿原源组件 28551 的面板边界改色，外围浅黄保留；球后方暖白面板保持。两张 2K 图集均打包在 `.blend`；三档明暗与独立内部线稿、外轮廓为实际模型材质/几何，成品渲染不借用 Freestyle。

实际七个 MP4 已原生解码核对尺寸与帧数；骨骼名称和层级一致，刚性绑定成立。源动作自身的接地偏差、落地大竖直位移保留并记录，未伪报脚部接地或 UE 验收通过。未保存 UE 包，正式导入、物理资产、新七动画副本、回读及实战验证待 B 放行；需求/开发根文档更新为当前状态。

> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../ArtSource/CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。

