"""Reconcile actual B-v2 evidence into existing mutable art records and an incremental archive."""
from pathlib import Path
import json,hashlib
W=Path('D:/UE5.7/test1'); R=W/'ArtSource/Mechs/ControlRigMechStyle_20261004'; O=R/'Production_B_v2'
manifest=O/'production_manifest.json'; m=json.loads(manifest.read_text(encoding='utf-8')); digest=hashlib.sha256(manifest.read_bytes()).hexdigest()
dev=W/'Progress/DevelopmentDocumentation/20261004-ControlRig机甲美术统一.md'
s=dev.read_text(encoding='utf-8')
s=s.replace('status: in_progress','status: verification',1)
s=s.replace('summary: 用户已通过四色参考 A-v2，正在 Blender 按参考与源模型制作可编辑分件、完整152骨骼、原动作、图集和LOD，随后交付实际成品审核B。','summary: A-v2已获用户通过，实际Blender候选B-v2保留结构和152骨骼/三动作，交付2K三视图、同机位对照、完整视频和保守四LOD；预算及可迁移细线仍有差额。')
s=s.replace('next_action: 完成 Blender 成品与同机位参考对照、四档预算、实际部署/待机/行走预览，提交具体 B 版本。','next_action: 用户审核实际B-v2并反馈预算/造型/线稿；未通过或要求修改时继续制作，B放行后才导出回读和正式UE交付。')
s=s.replace('status_note: A-v2 已获用户明确通过，B-v1 制作中，正式 UE 副本未开始；减面不牺牲已审结构，预算不足时提交差额和视觉对比。','status_note: A-v2已通过，B-v1由助手终检撤回，当前B-v2待用户审核；四档预算不达标、可迁移细线偏软，UE正式副本和导出回读未开始。')
s=s.replace('本次是纯美术 A 阶段','本次是纯美术 A/B 制作阶段')
s=s.replace('首轮范围仅为只读源采集、参考设计和审核交付。参考研究场景没有重拓扑或主动减面，不能充当 B 成品。','首轮已完成只读源采集、参考设计和审核交付；当前按用户 A 放行制作真实 Blender 候选。参考研究场景没有重拓扑或主动减面，不能充当 B 成品。')
s=s.replace('| B 实际成品 | B-v1 | **制作中** | A 已放行；尚未提交完整 B 成品或取得 B 通过决定 |','| B 实际成品 | B-v2 | **待审核** | 实际模型、四图和完整三动作已提交；四档预算和可迁移细线有差额，B-v1由助手终检撤回 |')
s=s.replace('当前 A 指向 A-v2 且待审核。配色修改授权不视为 A 或 B 通过','当时 A 指向 A-v2 且待审核；后续用户已明确通过 A-v2。配色修改授权本身不视为 A 或 B 通过')
s=s.replace('尚无生产 LOD、正式贴图或性能结论。','现有保守四档候选 LOD 和原生 2K 图集，实际数值见后文；预算未达标，未取得性能结论。')
s=s.replace('- [ ] 按已审图重制、四档预算和实际 Blender/动画 B 交付。','- [x] 按已审图制作实际可编辑候选，交付四档预算差额、三视图和完整 Blender 动画；尚不代表预算/用户 B 通过。')
s=s.replace('没有声称动画兼容、正式 LOD、UE 外观或实战帧率通过。','新增 Blender 骨架、关键姿态与实际视频核验；未声称最终 UE 动画兼容、预算、UE 外观或实战帧率通过。')
rows='\n'.join(f"| LOD{x['lod']} | {x['body_triangles']:,} | {x['outline_triangles']:,} | {x['total_triangles']:,} | +{x['difference']['total']:,} |" for x in m['lods'])
section=f'''\n## 2026-10-04 A 放行与实际 Blender B-v2\n\n用户明确“审核通过，blender已开，根据参考图和源模型一比一制作”，据此通过 A-v2；[独立放行记录](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Approvals/Approval_A_v2_20261004.json)锁定已审清单，不改写旧冻结参考。\n\n[实际 B-v2 交付](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v2/README.md)与[对照/视频](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v2/Review_B_v2.html)为当前候选；[Blender 源](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v2/ControlRigMech_B_v2_Production.blend)已打开，保留源四足单炮、152骨骼层级、原动作、761个可编辑分件、86个核对过的镜像和两个32分段剖面旋转件。主视图与材质/描边壳预览分别登记，前者原生 Freestyle，后者内线仍偏软，未冒充 UE 等效通过。\n\n| 档位 | 本体三角面 | 描边三角面 | 合计 | 合计差额 |\n|---|---:|---:|---:|---:|\n{rows}\n\n四档本体各1区段，0–2另1描边区段；2K BaseColor/独立内线/功能/ORM已生成和打包，RGBA8未压缩合计64MiB。当前展示用角点颜色保留准确分色，功能遮罩与ORM未完整接入。**预算未达标，尤其远档差额明显，不记为已满足指挥官批量单位性能目标。**未擅自删主要机构，按计划提交差额与视觉对照。\n\nB-v1在终检发现整体低档压面破坏装配、视频编码重复首帧，已由助手撤回，未取得用户审核决定。B-v2改为保守角度整理、保留全部部件、转移近景法线，四档无退化、同向重复或反向重合面。视频改用独立逐帧条带，1280²/15fps，完整覆盖原3个动作及精确两端，解码比对成功；画幅避免足爪出画。\n\n源全部152骨骼/父子关系恢复，三点姿态矩阵最大元素差2.3842e-6，实际位移/伸缩/缩放保留。曾发现足爪下偏2.6cm，按各自源表面贴合后全量重渲染；末部署/待机/行走关键脚底源相对差约0.113mm，展开中关键姿态最大6.509mm。四档无无权重顶点/非有限值，LOD0顶点采样源表面P95为4.387mm、最大44.831mm；这些范围检查不代替全部时刻穿插/接地或UE兼容验收。\n\nB-v2清单SHA256 `{digest}`，Blender SHA256 `{m['final_blender_sha256']}`。A-v1/A-v2共41个冻结文件保持原样；[用户决定](../../ArtSource/Mechs/ControlRigMechStyle_20261004/review_decisions.json)将B-v2记为待审核。导出回读、正式UE副本、ControlRig副本、三档指挥官UE镜头和实战帧率尚未运行；全局规范仍v1.2。\n'''
assert '## 2026-10-04 A 放行与实际 Blender B-v2' not in s
dev.write_text(s+section,encoding='utf-8')
req=W/'Progress/RequirementDocument/20261004-ControlRig机甲美术统一.md'; s=req.read_text(encoding='utf-8')
s=s.replace('next_action: 按已通过的四色参考 A-v2 制作 Blender 成品，完成预算与实际动画预览后提交审核 B。','next_action: 用户审核实际Blender候选B-v2及预算差额，B放行后再导出回读和正式UE交付。')
s=s.replace('B 制作中，正式 UE 导入仍须 B 放行。','B-v2待审核，预算与可迁移细线有差额，正式 UE 导入仍须 B 放行。')
s+='\n当前 [真实 Blender B-v2](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v2/README.md)已交付2K效果/三视图、同机位参考、3个完整动作和保守四LOD差额；B-v1因助手终检问题撤回。B-v2未获用户通过，预算和可迁移细线未达目标，不提前正式导入。\n'
req.write_text(s,encoding='utf-8')
ledger=W/'Progress/DevelopmentDocumentation/GuLiStrike美术规范.md'; s=ledger.read_text(encoding='utf-8')
s+='\n## 2026-10-04 ControlRig A 放行与 Blender B-v2 候选\n\n用户明确“审核通过，blender已开，根据参考图和源模型一比一制作”，据此通过四色A-v2清单 `ea47e95d2e5ee1f8d2bfe3ccd1fd9b249873e99fe31d93f9c9935b72af1ed9ac`。当前[实际B-v2模型、三视图、参考对照及完整三动作](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v2/README.md)保留源结构和152骨骼，待用户审核；原A-v1/A-v2冻结历史保持。\n\nLOD0本体57,323、描边7,520，其他档采用保守整理以保留装配；全部预算未达标，远档差额明显。主展示为实际网格的Freestyle，独立遮罩/壳预览细线偏软，分别留档。B-v1已由助手因远档破坏和编码问题撤回，B-v2重新制作并解码比对。预算、技术验证、用户B决定和性能分记；未导入UE或改变Ground/重防号引用，全局规范仍v1.2。详见[实施记录](20261004-ControlRig机甲美术统一.md)及[本轮归档](../Archive/20261004-ControlRig机甲A放行与BlenderB_v2候选.md)。\n'
ledger.write_text(s,encoding='utf-8')
archive=W/'Progress/Archive/20261004-ControlRig机甲A放行与BlenderB_v2候选.md'
assert not archive.exists()
archive.write_text(f'''---
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
summary: 用户通过四色A-v2后完成实际Blender候选B-v2、2K三视图、同机位对照、完整三动作和保守四LOD差额；B待审核，预算与可迁移细线未达目标。
next_action: 用户审核B-v2及预算差额，修改项处理后再审，B通过后才导出回读及正式UE副本。
relations:
  work_items: [WORK-20261004-002]
status_note: A已通过；B-v1由助手终检撤回，B-v2待审核；UE正式目录未开始，未冒充预算或性能通过。
art_revision: '1.2'
---

# 2026-10-04：ControlRig A 放行与 Blender B-v2

用户原话“审核通过，blender已开，根据参考图和源模型一比一制作”放行已提交A-v2，哈希见[放行记录](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Approvals/Approval_A_v2_20261004.json)。实际[候选交付](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v2/README.md)含可编辑模型、2K四图、同机位参考、完整部署/待机/行走MP4、2K图集、四档保守LOD和逐项差额。源四足单炮、152骨骼/层级、比例与部署位移/缩放保留，Blender已打开本版。

LOD0本体57,323/描边7,520/总64,843；LOD1总58,414，LOD2总47,420，LOD3总38,428。全部未达原预算，保留结构后提交差额；不将该结果写为指挥官批量性能达标。主展示使用实际生产网格的Freestyle，可迁移遮罩/壳预览内线偏软、功能/ORM未完整接入，明确区分。后者需在正式UE交付前继续完善。

终检曾发现B-v1远档破坏装配、视频重复首帧和宽画幅足爪出画；助手撤回该版，未补写用户拒绝或通过。B-v2采用角度整理和近景法线转移、逐帧独立条带、1280平方画幅；解码首中尾与源帧比对，并检查完整动画。足爪减面原下偏2.6cm已通过源表面贴合修正，末部署/待机/行走关键源相对差约0.113mm，展开中最大6.509mm。四档保存回读无无权重/非有限/退化/同向重复或反向重合面；源近表面P95约4.387mm，范围和余项见审计，不声称覆盖全部运动碰撞。

B-v2清单SHA256 `{digest}`；Blender SHA256 `{m['final_blender_sha256']}`。旧A-v1/A-v2全部41个冻结文件保持原样，B-v1冻结文件亦保留为撤回历史。当前[审核决定](../../ArtSource/Mechs/ControlRigMechStyle_20261004/review_decisions.json)为A通过/B-v2待审核。规范v1.2、源资源包、C++和战斗引用保持；UE导出回读、正式模型/骨架/CR/动画副本、UE镜头和实战帧率尚未运行。

## 资源清单

| 项目内最终文件夹 | 来源与内容 | 依赖及边界 |
|---|---|---|
| `ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v2` | 从已审实际源及制作分件生成的Blender、2K图、图集、LOD与真实视频/报告；未重新从外部ZIP迁移 | 依赖本资产冻结A-v2、Source/三动画只读采样及Scripts；无示例地图或外部Actor/Object支持目录；未导入UE |
| `.../Production_B_v2/AnimationSource` | UE源3动画/152骨骼全30fps只读本地TQS；源完整位移与缩放 | 独立工作进程只读导出并退出自身，原包未保存；非运行时/PIE验收 |
| `.../Scripts` | 本轮原生Blender分件、骨架、法线、贴图/壳、保守LOD、真实动作渲染/解码与归档脚本 | B-v1已冻结脚本保留；新B-v2脚本另存，不改旧参考或冻结版本 |

按用户计划、[制作技能](../../.agents/skills/guli-model-production/SKILL.md)和[规范§2](../RequirementDocument/GuLiStrike美术规范.md#2-制作与审核流程)，B具体版本通过后才继续导出回读与 `/Game/GuLiStrike/Mechs/ControlRigMech` 正式交付。事实台账见[实施文档](../DevelopmentDocumentation/20261004-ControlRig机甲美术统一.md)。
''',encoding='utf-8')
print('B_V2_PROGRESS_RECORDED',digest)
