"""Publish this approved asset delivery and update only its mutable project records."""
import json,hashlib,re,html,struct
from pathlib import Path
ROOT=Path('D:/UE5.7/test1');R=ROOT/'ArtSource/Mechs/ControlRigMechStyle_20261004';O=R/'UE_Delivery_v1'
def read(p):return json.loads(p.read_text(encoding='utf-8'))
def write(p,s):p.write_text(s,encoding='utf-8',newline='\n')
def dump(p,r):write(p,json.dumps(r,ensure_ascii=False,indent=2)+'\n')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
imported=read(O/'ue_import_report.json');preview=read(O/'ue_preview_readback_report.json');inventory=read(O/'final_live_inventory.json')
assert imported['success'] and preview['success'] and inventory['success']
assert len(preview['captures'])==19 and len(preview['component_pose_checks'])==9 and inventory['delivery_asset_count']==18
assert read(O/'blender_export_readback.json')['success'] and read(O/'native_ue_fbx_readback.json')['success']
assert read(O/'native_ue_fbx_readback.json')['sha256']==sha(O/'FBX/UE_Readback_AllLODs.fbx')
assert read(O/'reference_review_saved.json')['success']
frozen=R/'Production_B_v4/production_manifest.json'
assert sha(frozen)=='802ade7bbd94e336ec8d6b2fb7fa8dbe36efc7a27a20cb7ba37df34fc144da16'
for row in read(frozen)['files']:
    p=R/row['path'];assert p.stat().st_size==row['bytes'] and sha(p)==row['sha256'],str(p)
for c in preview['captures']:assert struct.unpack('>II',(O/'Previews'/c['name']).read_bytes()[16:24])==(2048,2048)
pose_max=max(x['max_152_bone_global_position_delta_cm'] for x in preview['component_pose_checks'])
angle_max=max(x['max_rotation_delta_deg'] for x in preview['component_pose_checks'])
counts=[(57323,12794,70117,30117),(55592,9410,65002,48002),(46308,5562,51870,45870),(38395,0,38395,36395)]
table='| LOD | 本体三角面 | 描边三角面 | 合计 | 超预算 | UE渲染顶点 | 区段 | 屏幕尺寸 |\n|---|---:|---:|---:|---:|---:|---:|---:|\n'
for i,((b,o,t,d),lod) in enumerate(zip(counts,imported['lods'])):table+=f"| {i} | {b:,} | {o:,} | {t:,} | +{d:,} | {lod['render_vertices']:,} | {lod['actual_UE_sections']} | {[1,.40,.16,.06][i]} |\n"
external=sorted({d for a in inventory['assets'] for d in a['dependencies'] if not d.startswith(('/Game/GuLiStrike/Mechs/ControlRigMech','/Script/'))})
dump(O/'source_integrity_report.json',{'success':True,'frozen_B_v4_files_checked':len(read(frozen)['files']),'manifest_sha256':sha(frozen),'Blender_sha256':sha(R/'Production_B_v4/ControlRigMech_B_v4_Production.blend'),'UE_binary_files_read_or_hashed':False})
readme=f'''# ControlRig 机甲 B-v4：UE-v1 正式交付

用户“导入至ue”放行当前 B-v4，已保存至 `/Game/GuLiStrike/Mechs/ControlRigMech`。当前 UE 打开正式模型编辑器，并留有独立展示关卡；三渲二、少量内部结构线、基础外轮廓和四色分区已重建。[实际 UE 图集](Review_UE_v1.html) · [导入放行](../Approvals/Approval_B_v4_Import_20261004.json) · [源版本](../Production_B_v4/README.md)。冻结源文件不改写，旧文件中的“待审核”是提交时状态。

## 使用入口

- 模型：`/Game/GuLiStrike/Mechs/ControlRigMech/Meshes/SKM_ControlRigMech`。
- 骨架：`Skeleton/SKEL_ControlRigMech`；152 骨骼与原名称、层级、参考姿态兼容。
- 动作：`Animations/Mech_Deploy`、`Mech_Idle`、`Mech_Walk`，时长 5 / 8.666667 / 5 秒。
- ControlRig：`Rigs/CR_ControlRigMech`，预览正式模型，31 图/811 节点/1057 连接及控制默认值对照原 Rig 一致。
- 展示：`/Game/GuLiStrike/Mechs/ControlRigMech/Preview/LVL_ControlRigMech_B_v4`，单位 Actor `CRM_B_v4`，相机 `CRM_ReviewCamera`；独立美术展示，没有玩法接入。

上述相对路径均位于正式根目录。交付16个生产资产，另2个展示资产（地图、背景材质）。原 `/Game/Assets/ControlRig/Characters/Mech` 保留；源本就没有物理资产和挂点，正式副本同样为null/0。[原生资产及依赖清单](final_live_inventory.json)。外部依赖：{', '.join('`'+x+'`' for x in external)}。

## 着色与网格

主装甲灰青 `#557B78`、检修/炮口铁锈红 `#8E3A2A`、骨架深青灰 `#2C3735`、关节沙米 `#D5C09C`，线色 `#1B2422`。固定艺术光向按UE坐标转换，法线点积阈值0.12/0.55，线性三档0.42/0.74/1。Unlit三档材质、独立2K内线遮罩和三个UV通道保留；内线强度LOD0–3为0.30/0.22/0.10/0，半宽0.006/0.007/0.008m。

轮廓采用实际Blender反向蒙皮壳，0–2宽度0.018/0.020/0.025m；LOD3没有描边壳。每档一本体区段，0–2另一个描边区段；资产共5个材质槽（4档本体+共享描边）。导出副本仅清理0/4/26/33个相同顶点索引的重复面，未移动顶点或删改主要机构。

{table}
**四档均超原预算。导入放行不等于预算或批量实战帧率通过。** 逐角距离UV会增加GPU顶点拆分。4张源图集均2048²，RGBA8源数据合计64MiB；UE原生查询有时回传32²等当前纹理资源尺寸，源尺寸与查询时数值分开登记；压缩见清单，未测纹理驻留显存。准确配色由顶点色驱动；BaseColor图集交付，功能/ORM作为配套保留，未强行加入改变已审外观的PBR效果。

## 回读和实际动画

[Blender导出回读](blender_export_readback.json)：四档位置、UV、颜色和权重匹配，全部152骨骼、单位及根缩放正确。[UE重新导出回读](native_ue_fbx_readback.json)：实际四档三角面/区段、3UV和四色+线色字节保留；[角法线对照](native_normal_comparison.json)P99约0.033°、最大约0.628°。UE导出FBX对其他LOD使用局部区段材质名字，不能用该名字代替正式网格的原生LOD材质映射；后者已逐档核对。

三个动画由源UE的原生30fps全部152骨骼局部T/Q/S采样，经AnimationDataController重建到正式骨架，不是二进制复制。部署的真实位移、伸缩、缩放与原速保留，未归一化。原始局部姿态三点回读匹配；[实际组件回读](ue_preview_readback_report.json)对照源与正式组件的三动作首/中/尾，共9×152骨骼，最大位置差{pose_max:.8f}cm、旋转差{angle_max:.8f}°。UE播放的压缩姿态与原始轨道有约0.8mm差异，源组件也存在；不能把压缩/原始差异误记为导入误差。原生API确认源/正式三动作均无Float曲线、无Notify且additive类型一致；额外数据见资产清单。

19张原生UE2048²包含三分之四/正/左/背、LOD1–3、9个动作检查点和指挥官三档镜头。指挥官参数读取项目真实Camera JSON及战场范围；近景35m/25°、战术300m/55°、全览90°。完整视口无HUD，展示截图不等于实际HUD构图、连续LOD过渡、ControlRig全部运行控制或全部动画帧穿插验收。最终渲染通过，未声称像素级跨渲染器一致；曝光、显示变换和抗锯齿仍会改变观感。

## 留档与边界

源B-v4清单SHA256 `802ade7bbd94e336ec8d6b2fb7fa8dbe36efc7a27a20cb7ba37df34fc144da16`；Blender SHA256 `bf566b78fb3fd03c516016ed0e32bc7ee8412929c303283985340d5d5b5d853b`。[冻结源完整性](source_integrity_report.json)及[UE交付文件清单](delivery_manifest.json)分别登记。UE资产通过编辑器API核验，未读取或哈希uasset/umap内容。

未改玩法身份、C++、Ground/重防号引用或原资源包；未编译、未运行PIE、未测实战FPS。当前完成该B-v4版本的UE导入，预算优化与实战性能仍是待处理项。复现脚本位于Scripts；已有加载资产通过当前Editor处理，独立导入脚本仅在其安全工作进程入口执行，不能与当前Editor并发写同一资产。
'''
write(O/'README.md',readme)
cards=[]
for c in preview['captures']:
    title=c['name'].replace('UE_B_v4_','').replace('.png','')
    cards.append(f'<figure><a href="Previews/{html.escape(c["name"])}"><img loading="lazy" src="Previews/{html.escape(c["name"])}" alt="{html.escape(title)}"></a><figcaption>{html.escape(title)}</figcaption></figure>')
write(O/'Review_UE_v1.html','<!doctype html><html lang="zh-CN"><meta charset="UTF-8"><title>ControlRig B-v4 UE-v1</title><style>body{font:16px system-ui;background:#f7f2e8;color:#2c3735;margin:32px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(380px,1fr));gap:24px}figure{margin:0}img{width:100%;display:block}figcaption{padding:8px}a{color:inherit}</style><h1>ControlRig B-v4 · UE-v1 实际预览</h1><p>四色、少量结构线、基础轮廓、三档明暗。四档预算仍超标，未测实战FPS。19张图均UE原生2048²，点击原图；三档镜头无HUD。</p><p><a href="README.md">交付与验证边界</a></p><main>'+''.join(cards)+'</main></html>')
decisions=read(R/'review_decisions.json');decisions['formal_UE'].update(status='imported_and_saved',verification='partial; art import/readback/render passed, budget unmet and FPS not measured',delivery_path='UE_Delivery_v1/README.md',preview_report='UE_Delivery_v1/ue_preview_readback_report.json',production_asset_count=16,preview_asset_count=2,source_pack_preserved=True);dump(R/'review_decisions.json',decisions)
write(R/'CURRENT.md','''# 当前交付：ControlRig B-v4 / UE-v1

按用户“导入至ue”放行当前简线B-v4，已保存到 `/Game/GuLiStrike/Mechs/ControlRigMech`；当前UE保留正式模型和独立展示地图。四色、三档明暗、少量结构线与基础轮廓、152骨骼、三个动作及ControlRig、四档LOD均已交付。原包保留，玩法/C++不改。

[UE交付与预算](UE_Delivery_v1/README.md) · [原生UE预览](UE_Delivery_v1/Review_UE_v1.html) · [Blender源](Production_B_v4/ControlRigMech_B_v4_Production.blend) · [导入放行](Approvals/Approval_B_v4_Import_20261004.json) · [当前决定](review_decisions.json)。

四档仍超预算，FPS未测，当前导入不代表性能验收。A-v2与旧B-v1–4冻结记录均保留，冻结文件中的审核状态是当时提交快照，最新决定以独立放行及当前决定文件为准。
''')
def replace_meta(p,values):
    s=p.read_text(encoding='utf-8')
    for k,v in values.items():s,n=re.subn(r'^'+re.escape(k)+r':.*$',k+': '+v,s,count=1,flags=re.M);assert n,(p,k)
    return s
req=ROOT/'Progress/RequirementDocument/20261004-ControlRig机甲美术统一.md';dev=ROOT/'Progress/DevelopmentDocumentation/20261004-ControlRig机甲美术统一.md'
note='A-v2通过；用户“导入至ue”放行当前B-v4，UE正式副本与实际渲染/动作回读已交付；四档预算超标，实战FPS未测。'
next_action='处理四档预算差额，并在真实指挥官玩法中评估连续LOD切换、ControlRig运行控制和实战FPS。'
rs=replace_meta(req,{'summary':'ControlRig B-v4四色简线机甲已按用户明确放行导入UE，保留152骨骼、三动作、ControlRig及四档LOD；预算与性能仍待验收。','next_action':next_action,'status_note':note})
rs=rs.replace('- [ ] 实际 Blender 成品、四档预算、完整动画预览及用户 B 决定。','- [x] 实际Blender B-v4、完整动画预览与预算差额已交付；用户“导入至ue”放行具体版本，预算未通过。').replace('- [ ] B 放行后的导出回读、正式副本与 UE 三档镜头验证。','- [x] B放行后的导出回读、UE正式副本、原生渲染及三档镜头独立预览；实战/HUD/连续切换仍待验证。')
write(req,rs+'\n## 2026-10-04 B-v4 导入放行与 UE 交付\n\n'+note+' [当前交付](../../ArtSource/Mechs/ControlRigMechStyle_20261004/UE_Delivery_v1/README.md)覆盖模型、兼容骨架、三动画、ControlRig、材质和贴图，另保存独立展示地图。前文B待审核及UE未开始描述是当时阶段事实；本条及front matter为当前状态。\n')
ds=replace_meta(dev,{'summary':'用户“导入至ue”放行B-v4，正式UE16生产资产与2展示资产已保存；原生着色、四LOD和9个实际组件姿态检查完成，预算超标/FPS未测。','next_action':next_action,'status_note':note})
ds=ds.replace('| B 当前实际成品 | B-v4 | **待审核** | 少量内部线、基础轮廓及三档明暗已在当前Blender显示，四图与三个完整动作重交付；预算仍超标 |','| B 当前实际成品 | B-v4 | **导入放行** | 用户“导入至ue”，明确针对当前简线版；独立记录锁定源清单和Blender哈希，预算仍超标 |').replace('| UE 正式副本 | 未开始 | **未运行** | B 未放行，未导入正式目录 |','| UE 正式副本 | UE-v1 / B-v4 | **已保存** | 16生产资产+2展示资产，实际渲染及源/正式组件三动作9个检查点通过；性能未验收 |').replace('- [ ] 用户 B 放行后的导出回读、正式 UE 副本和三档镜头检查。','- [x] 用户B-v4导入放行后的导出回读、正式UE副本及三档指挥官镜头独立预览。')
ds+=f'''\n## 2026-10-04 B-v4 正式 UE 导入

用户“导入至ue”明确放行当前B-v4，[独立记录](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Approvals/Approval_B_v4_Import_20261004.json)锁定清单SHA256 `802ade7bbd94e336ec8d6b2fb7fa8dbe36efc7a27a20cb7ba37df34fc144da16`及Blender SHA256 `bf566b78fb3fd03c516016ed0e32bc7ee8412929c303283985340d5d5b5d853b`。冻结B-v4全部{len(read(frozen)['files'])}个文件核对未变；前文及旧冻结文件的B待审核/UE未导入是历史状态。最新源与决定入口为[CURRENT](../../ArtSource/Mechs/ControlRigMechStyle_20261004/CURRENT.md)。

正式根 `/Game/GuLiStrike/Mechs/ControlRigMech` 保存16个生产资产：模型、兼容骨架、部署/待机/行走、ControlRig、2母材质+4LOD实例、4贴图；另存展示地图和背景材质。当前UE打开正式模型，展示地图 `/Game/GuLiStrike/Mechs/ControlRigMech/Preview/LVL_ControlRigMech_B_v4` 中Actor `CRM_B_v4`、相机 `CRM_ReviewCamera` 已保存。原包、玩法身份、C++与战斗引用未改；源physics=null、sockets=0不虚构配套。

UE保留四种分色、三个UV、0.42/0.74/1三档、稀疏内线0.30/0.22/0.10/0、反向蒙皮壳和LOD区段2/2/2/1。正常渲染发现Custom代码保留字`line`编译错误，已修成`internalInk`并保存，重渲染19张原生2048²：同机位四图、LOD1–3、三动作首中尾9图和项目三档镜头。截图通过SceneCapture2D原生导出，未手工改图；导出gamma、曝光和渲染器差异不冒充像素级一致。

{table}
导出副本只清理同顶点索引重复面0/4/26/33，源Blender不改。全部LOD超预算，不认定批量性能通过。2K源纹理RGBA8合计64MiB，原生查询时资源尺寸可随加载变化，实际查询值/压缩与依赖见[最终原生清单](../../ArtSource/Mechs/ControlRigMechStyle_20261004/UE_Delivery_v1/final_live_inventory.json)，未测驻留显存。颜色仍由顶点色驱动，功能/ORM配套未新增改变已审外观的PBR效果。

导出回读核对全部152骨骼/名称/层级/根单位、位置/UV/颜色/权重；UE网格参考姿态最大局部位移差0.000192cm、旋转0.000282°。原生UE再次导出回Blender确认全部四档面数/区段/3UV/色板，角法线P99约0.033°，最大约0.628°。LOD0权重原生检查40,903顶点，39,948刚性/955柔性/无无效权重。

UE无法直接改AnimSequence只读Skeleton关系，最终通过AnimationDataController在正式骨架上重建源30fps全部152骨骼局部T/Q/S；保留部署位移、伸缩、缩放、动作时长/速率/压缩设置，不进行通用归一化。真实组件与源组件9×152骨骼最大位置差{pose_max:.8f}cm、旋转{angle_max:.8f}°；行走中点原始轨道与播放约0.8mm差异，源播放也存在，排除导入误差。额外事件数据另列清单，ControlRig 31图/811节点/1057连接/默认值匹配，全部运行时控制未逐一操作。

项目近景35m/25°、战术300m/55°与90°全览按实际Camera JSON/资源战场范围独立预览，无HUD；实际HUD构图、连续LOD过渡、完整运行时穿插/接地及实战FPS未验收。纯美术交付不编译、不运行PIE。验证状态保持partial（预算/实战余项），本轮UE导入已完成；[完整交付](../../ArtSource/Mechs/ControlRigMechStyle_20261004/UE_Delivery_v1/README.md)、[增量归档](../Archive/20261004-ControlRig机甲B_v4正式UE导入.md)。
'''
assert len(ds.encode('utf-8'))<30720;write(dev,ds)
ledger=ROOT/'Progress/DevelopmentDocumentation/GuLiStrike美术规范.md';ls=ledger.read_text(encoding='utf-8')
start=ls.index('## 2026-10-04 ControlRig B-v3/B-v4 实际线稿');assert '## ' not in ls[start+5:]
ls=ls[:start]+'''## 2026-10-04 ControlRig B-v4 / UE-v1

用户要求少量内线、保留基础轮廓后，B-v4完成；再以“导入至ue”放行本版。正式根`/Game/GuLiStrike/Mechs/ControlRigMech`保存模型/152骨骼/三动作/ControlRig/三档简线材质/四LOD及展示图；原包保留，预算超标、FPS未测，规范v1.2。见[当前交付](../../ArtSource/Mechs/ControlRigMechStyle_20261004/UE_Delivery_v1/README.md)、[B-v4历史](../Archive/20261004-ControlRig机甲B_v4减少线稿保留轮廓.md)、[UE导入归档](../Archive/20261004-ControlRig机甲B_v4正式UE导入.md)。
'''
assert len(ls.encode('utf-8'))<=30720;write(ledger,ls)
archive=ROOT/'Progress/Archive/20261004-ControlRig机甲B_v4正式UE导入.md';assert not archive.exists()
archive_text=f'''---
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
summary: 用户明确放行简线B-v4，正式UE模型/骨架/三动作/ControlRig/材质/贴图/四LOD已保存，19张原生预览及9次源组件姿态对照通过；预算超标，FPS未测。
next_action: {next_action}
relations:
  work_items: [WORK-20261004-002]
status_note: 纯美术导入完成；审核放行不代替预算和实战性能验收，未编译或运行PIE，原包和玩法/C++不改。
art_revision: '1.2'
---

# 2026-10-04：ControlRig B-v4 正式UE导入

用户“导入至ue”放行当前简线成品，[独立放行](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Approvals/Approval_B_v4_Import_20261004.json)锁定B-v4清单SHA256 `802ade7bbd94e336ec8d6b2fb7fa8dbe36efc7a27a20cb7ba37df34fc144da16`及Blender `bf566b78fb3fd03c516016ed0e32bc7ee8412929c303283985340d5d5b5d853b`。{len(read(frozen)['files'])}个冻结源文件核对未变，不重写旧审核快照；[完整交付](../../ArtSource/Mechs/ControlRigMechStyle_20261004/UE_Delivery_v1/README.md)和[当前决定](../../ArtSource/Mechs/ControlRigMechStyle_20261004/review_decisions.json)登记新状态。

## 资源清单

以下路径均以`/Game/GuLiStrike/Mechs/ControlRigMech`为正式根，外部制作来源`ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v4`及其AnimationSource；原包为`/Game/Assets/ControlRig/Characters/Mech`，全部保留。

| 最终文件夹 | 来源 | 内容/用途 | 依赖与边界 |
|---|---|---|---|
| `Meshes` | 已审Blender四LOD导出 | 一个正式骨骼网格，四LOD | 152骨骼、三UV/逐角四色和反向壳；未读取资产二进制 |
| `Skeleton` | 原SK_Mech原生复制 | 兼容骨架 | 名称/层级/参考姿态保持；源无物理资产/挂点 |
| `Animations` | 源三动画原生30fps完整TQS | 部署、待机、行走3个资产 | UE DataController重建，不是二进制复制；实际位移/缩放不归一化 |
| `Rigs` | 原CR_Mech原生复制 | ControlRig副本 | 正式preview，31图/811节点/1057连接一致，保留核心ControlRig依赖 |
| `Materials` | B-v4已审实际shader | 三档简线/基础轮廓2母材质及4LOD实例 | Unlit，LOD区段2/2/2/1；不新加PBR效果 |
| `Textures` | B-v4四张2K图集 | BaseColor、内线、功能、ORM | 配色用顶点色，内线NonColor；源2048²与查询时纹理资源尺寸分记，驻留显存未测 |
| `Preview` | 本轮UE展示制作 | 一独立地图及背景材质 | Actor CRM_B_v4与CRM_ReviewCamera，Engine基本Cube/灯/后处理；无玩法逻辑 |

共16个生产资产+2个展示资产。原包未保存/覆盖，源44资产中的其他未用贴图/示例/场景不迁入。当前关卡即`/Game/GuLiStrike/Mechs/ControlRigMech/Preview/LVL_ControlRigMech_B_v4`，正式模型编辑器已打开；没有外部Actor支持目录。实际外部依赖及全部包路径见[原生清单](../../ArtSource/Mechs/ControlRigMechStyle_20261004/UE_Delivery_v1/final_live_inventory.json)，核心Engine/ControlRig依赖保留，正式资产引用经原生查询不再指向原Mech目录。

## 验证与差额

四色和0.42/0.74/1三档、少量内线与基础壳原生重建。独立Blender导出回读、原生UE再导出回读均成功，位置/UV/颜色/权重/全部152骨骼单位匹配。法线方向P99约0.033°，最大约0.628°。源/新实际组件首中尾9×152骨骼最大位置差{pose_max:.8f}cm、旋转{angle_max:.8f}°；源播放本身相对raw在行走中点约0.8mm压缩误差，未抹掉真实位移/缩放。材质初次图形编译的HLSL保留字已修正；19张最终原生2048²覆盖同机位四视图、四LOD、三动作9点及项目三档指挥官镜头。

{table}
导出副本只删除0/4/26/33个同顶点索引重复面，原Blender不变。四档均超预算，选线减少不能代替网格优化/FPS结论。项目镜头独立预览无HUD，未验收HUD构图、连续LOD切换、ControlRig全部运行控制、全时刻穿插/接地或实战性能。未编译、不运行PIE，未改Ground/重防号WM01、C++与战斗引用。当前整体verification=partial保留预算与实战余项，UE导入阶段已完成。
'''
write(archive,archive_text)
selected=[O/'README.md',O/'Review_UE_v1.html']+list((O/'Scripts').glob('*.py'))+list((O/'FBX').glob('*.fbx'))+list((O/'Previews').glob('UE_B_v4_*.png'))
for name in ['blender_export_readback.json','ue_import_report.json','native_ue_fbx_readback.json','native_normal_comparison.json','ue_preview_readback_report.json','final_live_inventory.json','source_integrity_report.json','reference_review_saved.json','preview_attempt_raw_vs_component.json','live_shader_fix.json']:selected.append(O/name)
files=[{'path':str(p.relative_to(O)).replace('\\','/'),'bytes':p.stat().st_size,'sha256':sha(p)} for p in sorted(set(selected)) if p.is_file()]
dump(O/'delivery_manifest.json',{'version':'UE-v1','approved_source':'B-v4','approved_source_manifest_sha256':sha(frozen),'formal_root':'/Game/GuLiStrike/Mechs/ControlRigMech','assets_via_native_API':inventory['assets'],'source_B_integrity_report':'source_integrity_report.json','files':files,'budget':'over_budget','FPS':'not_measured','verification':'partial overall; import/readback/actual rendered stage passed'})
print(json.dumps({'success':True,'files':len(files),'archive':str(archive),'dev_bytes':dev.stat().st_size,'ledger_bytes':ledger.stat().st_size,'max_actual_pose_delta_cm':pose_max},ensure_ascii=False))
