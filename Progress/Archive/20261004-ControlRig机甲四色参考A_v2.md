---
schema: guli-progress/v1
id: ARC-20261004-009
work_id: WORK-20261004-002
kind: archive
role: root
title: ControlRig 机甲按用户色板修订参考 A-v2
areas: [art, assets, rendering]
categories: [art]
status: recorded
verification: partial
created: '2026-10-04'
updated: '2026-10-04'
summary: 按用户四色色板修订 ControlRig 机甲参考，交付并冻结原生2K效果与三视图，保持结构、骨架、姿态及源基线，A-v2待审核。
next_action: 用户审核 A-v2，明确通过具体版本后按已审图开展 Blender 重制。
relations:
  work_items: [WORK-20261004-002]
status_note: A-v1按用户改色要求记为修改后再审，冻结历史保留；A-v2待审核，B和正式UE副本未开始。
art_revision: '1.2'
---

# 2026-10-04：ControlRig 机甲四色参考 A-v2

## 修改依据与结果

用户提供色板并明确“改成这种配色”，标注四色 `#2C3735 / #8E3A2A / #557B78 / #D5C09C`。据此修订 `/Game/Assets/ControlRig/Characters/Mech` 当前参考：灰青主装甲、铁锈红检修护甲和炮口外罩、深青灰骨架/软管、沙米色关节盖/金属件及原功能镜片；线稿用深青灰派生深色。源四足单炮、完整结构、原件分色边界、同姿态/同尺度及三档明暗保留。

[A-v2 入口](../../ArtSource/Mechs/ControlRigMechStyle_20261004/References_A_v2/README.md)含三分之四效果和正/左/背正交图，全部原生2048×2048，并链接原同机位灰模、活动件和接口说明。参考 `.blend` 为真实源网格的材质/相机研究，没有重拓扑或主动减面，不当作生产成品。[原 A-v1 阶段](20261004-ControlRig机甲参考A_v1交付.md)保持历史结论与全部冻结文件。

## 文件版本与验证

- [A-v2 冻结清单](../../ArtSource/Mechs/ControlRigMechStyle_20261004/References_A_v2/reference_manifest.json)：14个图纸、场景、设置、说明、色板、脚本或共享灰模；清单SHA256 `ea47e95d2e5ee1f8d2bfe3ccd1fd9b249873e99fe31d93f9c9935b72af1ed9ac`。
- [可编辑研究场景](../../ArtSource/Mechs/ControlRigMechStyle_20261004/References_A_v2/ControlRigMech_A_v2_ReferenceStudy.blend)与[设置](../../ArtSource/Mechs/ControlRigMechStyle_20261004/References_A_v2/reference_setup.json)：保留实际源几何/权重哈希和相机；三档线性亮度1/0.74/0.42，内线1.4px、轮廓2.25px。
- A-v1 27个冻结文件及其清单均重新验证哈希相同；没有覆盖旧参考、源FBX、源清单、灰模或旧脚本。A-v2位置/拓扑/权重与A-v1相同，另核对物体变换、骨骼层级/姿态、自定义角法线和逐面材质索引不变。
- 实际查看四张新图，主要色区、圆关节、四足单炮及机械装配一致；检查四图分辨率和完整性。用户色板副本与临时附件字节哈希一致。
- [审核决定](../../ArtSource/Mechs/ControlRigMechStyle_20261004/review_decisions.json)：保留用户原话和A-v1修改后再审历史，当前A-v2待审核，B/正式UE未开始。

指挥官模型统一为 LOD0 近景、LOD1 中景、LOD2 远景；当前规范与候选见[本次迁移](../RequirementDocument/20261005-指挥官三档LOD纠正与资源迁移.md)。本轮保留已审近景，新的实际版本 B 待审核，正式资源待放行后切换。

## 资源清单

| 最终项目内文件夹 | 内容、来源与用途 | 依赖及验证边界 |
|---|---|---|
| `ArtSource/Mechs/ControlRigMechStyle_20261004/References_A_v2/Inputs` | 用户附件原图副本 `UserPalette_20261004.jpg`；来源 `C:/Users/a/AppData/Local/Temp/codex-clipboard-c3aa84b1-b5f6-42da-b3f0-f88bd525adcb.jpg`；用于配色依据与后续复核 | 无额外依赖、示例地图或Actor/Object支持目录；原字节复制及哈希核对完成，仅ArtSource留档，未导入UE |
| `ArtSource/Mechs/ControlRigMechStyle_20261004/References_A_v2` | 本轮生成的四张2K图、参考研究场景、设置、说明和冻结清单 | 依赖同资产已有A-v1源研究场景与共享灰模/源数据；新增渲染和校验脚本位于同根`Scripts`；不复制无关素材，不迁入源包其他资源或新UE目录 |

## 审核与后续边界

用户本轮只要求改配色，不能记录为具体图纸通过。按原计划及[制作技能](../../.agents/skills/guli-model-production/SKILL.md)、[美术规范v1.2 §2](../RequirementDocument/GuLiStrike美术规范.md#2-制作与审核流程)，停在A-v2；A通过后才重制，B通过后才导出回读和正式导入 `/Game/GuLiStrike/Mechs/ControlRigMech`。

需求与最新阶段见[需求](../RequirementDocument/20261004-ControlRig机甲美术统一.md)、[实施台账](../DevelopmentDocumentation/20261004-ControlRig机甲美术统一.md)。本修订只影响本资产当前参考，不改变全局规范v1.2、Ground/重防号身份、C++或战斗引用。

> 2026-10-05 LOD 勘误：按用户明确指令更正以上相关段落和预算说明；其他历史内容保留。修改范围及原文校验见[勘误清单](../../ArtSource/CommanderLOD_20261005/Reports/document_erratum.json)。冻结模型及原始机器回读不作为当前制作入口。
