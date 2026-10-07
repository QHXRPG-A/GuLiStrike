---
schema: guli-progress/v1
id: ARC-20261004-008
work_id: WORK-20261004-002
kind: archive
role: root
title: ControlRig 机甲实际源采集与参考 A-v1 交付
areas: [art, assets, rendering]
categories: [art]
status: recorded
verification: partial
created: '2026-10-04'
updated: '2026-10-04'
summary: 恢复源资产只读查询，确认 ControlRig 四足单炮机甲的152骨骼与高面数基线，交付并冻结一致的八张2K参考和灰模，停在用户审核A。
next_action: 用户审核 A-v1，明确放行后按已审图开展 Blender 重制。
relations:
  work_items: [WORK-20261004-002]
status_note: 用户已批准实施计划，尚未通过具体参考版本A；B与UE正式副本未开始，预算与兼容性未验收。
art_revision: '1.2'
---

# 2026-10-04：ControlRig 机甲参考 A-v1 交付

## 本轮结果

按用户当前 ControlRig 方案完成首轮 A 交付：实际源只读采集、结构/骨架/ControlRig/动画登记，四张设计参考与四张同机位灰模，配色、活动件和生产预算说明，版本与 SHA256 留档。没有重制生产网格或导入正式资源。

| 文件或目录 | 内容 |
|---|---|
| [ArtSource 入口](../../ArtSource/Mechs/ControlRigMechStyle_20261004/README.md) | A-v1 图纸、灰模、配色、活动件、接口差异和后续预算 |
| [冻结清单](../../ArtSource/Mechs/ControlRigMechStyle_20261004/References_A_v1/reference_manifest.json) | 27 个参考、源导出、场景、数据及脚本文件的 SHA256 |
| [源资产清单](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Source/source_manifest.json) | 实际主模型、152 骨骼、44 项资产、材质、LOD、CR 引用及 FBX |
| [Rig/动画基线](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Source/rig_animation_baseline.json) | 31 个图表/函数模型、811 节点、1057 连接及默认值；三个动画起/中/终点全部骨骼采样 |
| [审核决定](../../ArtSource/Mechs/ControlRigMechStyle_20261004/review_decisions.json) | A 待审核、B 未开始、UE 正式交付未开始 |
| [需求](../RequirementDocument/20261004-ControlRig机甲美术统一.md) / [实施记录](../DevelopmentDocumentation/20261004-ControlRig机甲美术统一.md) | 当前用户计划、事实、双审与下一步 |

## 实际源与关键发现

源 `/Game/Assets/ControlRig/Characters/Mech/Meshes/SKM_Mech` 是四足单主炮 SkeletalMesh，骨架 `SK_Mech` 有 152 骨骼；CR_Mech 实际 preview 和 skeleton 引用已确认。源只有 LOD0：284,700 三角面、203,324 顶点、14 材质区段。参考姿态尺寸为宽922.64、前后长1323.33、高689.26 cm，含上仰炮管。源 mesh 无物理资产、挂点数0。

三个源动画部署/待机/行走分别为5/8.666667/5秒，均30fps、152骨骼轨道。部署中的底盘抬升和 `cannon_02` X缩放0.516426→1是源动作，后续保留，不能套用只有旋转的缩放归一化处理。

FBX仍为284,700三角面，Blender回读为284,660；已证明是40个同顶点索引重复面被导入校验去重。Blender默认把原`root`表现为Armature对象、骨骼列表151个，生产导出需另行恢复完整152层级。当前参考场景不能当作已兼容成品骨架。

只读资产读取通过已有源码版普通 Editor 启动脚本恢复，FBX导出使用图形后端、禁用材质烘焙；原生编译和游玩未运行。工作进程仅退出自身，没有保存原包、修改玩法或战斗引用。

## 参考版本与验证

参考 **A-v1**：暖白、珊瑚红、深暖灰骨架、暖钢灰关节、少量琥珀功能灯；三档艺术光照、结构内线和外轮廓。四图同源姿态、正交尺度、色区；各图及四灰模原生2048×2048。实际查看四图与灰模，结构和主要左右色区一致；曲面、圆关节和源机构保留。

参考着色前后位置、拓扑与权重哈希相同，保持源导入法线；图片尺寸与文件完整性、源统计与重复面解释、全部27文件SHA256已检查。参考描边采用Freestyle，不据此声称生产壳面数或材质区段达标。

冻结清单 SHA256：`93324b32cb12fb9273bdb3d7ad5c60ce59798655f30bb657f53cca7f18db4fb4`。规范仍为v1.2，当前计划确认不等于A或B审核通过。用户尚未针对本版本给出通过决定。

## 下一阶段与边界

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。


> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../ArtSource/CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。

