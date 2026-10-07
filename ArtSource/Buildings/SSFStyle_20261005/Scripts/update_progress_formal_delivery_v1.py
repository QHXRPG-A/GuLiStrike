"""Append verified formal storage facts; preserve frozen source and historical archives."""
import json,hashlib,re,subprocess
from pathlib import Path
ROOT=Path('D:/UE5.7/test1');R=ROOT/'ArtSource/Buildings/SSFStyle_20261005';D=R/'UE_Delivery_v1'
delivery=json.loads((D/'formal_delivery.json').read_text(encoding='utf8'));assert delivery['success']
assert json.loads((D/'visual_qa.json').read_text(encoding='utf8'))['success']
before=json.loads((D/'progress_before_update.json').read_text(encoding='utf8'))
for path,sha in before.items():assert hashlib.sha256((ROOT/path).read_bytes()).hexdigest()==sha,('Concurrent edit; reread before updating',path)
archive=ROOT/'Progress/Archive/20261006-SSF建筑正式资源入库且暂不接入玩法.md'
assert not archive.exists()
probe=subprocess.run(['rg','-l','ARC-20261006-001',str(ROOT/'Progress/Archive'),'--glob','*.md'],capture_output=True,text=True,encoding='utf8')
assert probe.returncode==1,('Archive ID has been allocated',probe.stdout)
links='../../ArtSource/Buildings/SSFStyle_20261005/'
e=delivery['max_component_pose_error_cm_scale_degrees']
evidence=f"30档UE网格回读最大几何差{delivery['max_UE_mesh_roundtrip_error_m']:.9g}m，权重差{delivery['max_UE_skin_weight_error']:.9g}；22动画×3LOD×起/中/末共198组实际组件对照最大位置差{e[0]:.9g}cm、缩放差{e[1]:.9g}、旋转差{e[2]:.9g}°。对照基线为原可编辑动画按原运行采样率重采样后的局部四元数插值，原有1/2fps低采样动作保持；原压缩噪声差另行记录。"
shared=f"""## 2026-10-06 当前 B_v1 正式资源入库

用户查看实际Blender成品后明确“传ue，作为住正式资源存GuLiStrike中，暂不接入游戏中”。[本次放行](../../ArtSource/Buildings/SSFStyle_20261005/approval_B_import_20261006.json)只针对当前`SSF_Production_B_v1`的正式资源存储；Blender SHA256为`{delivery['release']['source_blend_sha256']}`，冻结清单为`{delivery['release']['manifest_sha256']}`。这是具体版本B存储放行，包含已披露差额，不推导全局预算上调、玩法或性能验收。

正式目录`/Game/GuLiStrike/Buildings/SSFStylized`已保存105个资源：10网格各3LOD、7套骨架/355骨骼、6源物理资产副本、22原名动画、38贴图、4母材质、16材质实例、1无人机蓝图副本和1独立机械动画压缩设置。原商城包、A_v1–A_v7及496个冻结B交付文件保持。源无人机未指派物理资产，正式副本继续不伪造物理资产。

{evidence} 骨架参考姿态按源恢复，三档UV/法线/材质区段及屏幕阈值保持；独立ACL设置使用0.001cm误差阈值和100cm虚拟顶点距离，保护机械活动件，原压缩设置未改。原时长、采样率、帧数、4条可编辑变换曲线与`Destoy`/`Edle`名称保留。

保存后独立重载105资产，原商城包引用为0；依赖只保留新目录、Engine、ACL和既有Script插件。54张实际UE图覆盖10资产×3LOD、六座35m/25°、300–700m/55°和1500m/55°预览；资源预览在未保存Entry世界执行，未编辑战场地图或游戏引用。固定光向、三档着色、内部遮罩、近中档壳、远档淡出和Base Color/Team Color接口已建立并读图检查。

[正式交付入口](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_v1/README.md)、[机器清单](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_v1/formal_delivery.json)、[独立重载/姿态/截图](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_v1/ue_validation.json)、[UE回读](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_v1/ue_mesh_readback.json)、[视觉记录](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_v1/visual_qa.json)。

24项本体预算差额和部分壳覆盖弱于参考的B_v1既有差异继续登记，原上限未变。当前版本已获存储放行；进一步减面、玩法接入、实战帧率和真实战场总览属于后续范围。本轮未运行PIE、联机或原生构建。上方参考/成品阶段的待审及未导入描述是历史事实，由本条最新放行与交付接续。
"""
def front(text,fields):
 for key,value in fields.items():
  text,n=re.subn(r'(?m)^'+re.escape(key)+r':[^\n]*',key+': '+value,text,count=1);assert n==1,key
 return text

reqp=ROOT/'Progress/RequirementDocument/20261005-SSF建筑美术统一与三档LOD.md';req=reqp.read_text(encoding='utf8')
req=front(req,{'updated':"'2026-10-06'",'summary':'当前实际B_v1已获正式存储放行，105资源保存重载、三档LOD、22动画和实际UE预览完成；暂不接入游戏，原预算差额继续登记。',
 'next_action':'后续按具体任务决定进一步减面或保留当前差额；游戏接入和实战性能另行处理。',
 'status_note':'用户指定当前可见Blender B_v1导入正式目录；存储交付完成，原商城包和冻结源保持，24项本体差额未转为全局预算规则。'})
req=req.replace('- [ ] 用户审核实际成品版本B。','- [x] 用户明确放行当前实际B_v1正式资源存储，版本与原话已登记。')
req=req.replace('- [ ] 导出回读、正式UE导入及实际指挥官镜头验收。','- [x] 导出回读、正式UE副本保存重载、实际资源镜头及动画姿态检查；实战地图/性能未执行。')
req+='\n'+shared

devp=ROOT/'Progress/DevelopmentDocumentation/20261005-SSF建筑美术统一与三档LOD.md';dev=devp.read_text(encoding='utf8')
dev=front(dev,{'status':'done','verification':'partial','updated':"'2026-10-06'",'summary':'当前B_v1已按用户指令保存105个GuLiStrike正式资源，三档LOD、22动画、独立引用和UE画面检查完成；不接入游戏。',
 'next_action':'后续单独决定本体预算调整及游戏接入；当前正式存储交付可直接复用。',
 'status_note':'当前可见B_v1的正式存储交付已完成；原24项本体差额和壳覆盖差异继续披露，未宣称预算严格达标或实战性能通过。'})
dev=dev.replace('当前为实际成品 **B_v1待审核**','当前为实际成品 **B_v1已放行并完成正式资源存储**')
dev=dev.replace('- [ ] 用户审核B_v1实际成品、描边表现和24项本体预算例外。','- [x] 用户明确放行当前可见B_v1正式存储；24项本体预算与壳差异继续随版本披露，原上限未变。')
dev=dev.replace('- [ ] 正式UE导入、实际镜头与动画一致性：未运行。','- [x] 正式UE副本导入、保存重载、独立资源镜头与22动画×3LOD姿态检查完成。')
dev+='\n'+shared

normp=ROOT/'Progress/RequirementDocument/GuLiStrike美术规范.md';norm=normp.read_text(encoding='utf8')
norm=front(norm,{'updated':"'2026-10-06'"})
old='B与24项本体预算例外待审核，部分真实壳轮廓比参考细，正式UE尚未导入；'
new='用户2026-10-06明确将当前可见B_v1导入作为正式资源存储，见[B存储放行](../../ArtSource/Buildings/SSFStyle_20261005/approval_B_import_20261006.json)；105正式副本已保存到`/Game/GuLiStrike/Buildings/SSFStylized`，三档LOD、22动画、引用重载和实际UE画面检查完成，见[交付](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_v1/README.md)。暂不接入游戏，24项本体差额和部分真实壳比参考细的既有差异随本版本保留，原预算及全局规范未上调；'
assert old in norm;norm=norm.replace(old,new,1)

ledgerp=ROOT/'Progress/DevelopmentDocumentation/GuLiStrike美术规范.md';ledger=ledgerp.read_text(encoding='utf8')
ledger=front(ledger,{'updated':"'2026-10-06'"});ledger+='\n'+shared

rows=[]
for a in delivery['assets']:
 key=a['key'];sourcefolder='/Game/Assets/SSF_Buildings/Buildings/'+('MilitaryFactory' if key=='Drone' else key)
 count=sum(x['key']==key for x in delivery['animations_detail'])
 description=(f"骨骼网格、独立骨架、{count}个动画"+('及源物理资产副本' if a['physics'] else '，源未指定物理资产')) if a['type']=='SkeletalMesh' else '静态网格'
 if key=='Drone':description+='、原行为无人机蓝图副本'
 rows.append(f"| `/Game/GuLiStrike/Buildings/SSFStylized/{key}` | `{sourcefolder}`及冻结Blender B_v1 | {description}、三档LOD与配套材质/贴图 | 新Shared与自身骨架/动画；原包保持 |")
archive_text=f"""---
schema: guli-progress/v1
id: ARC-20261006-001
work_id: ''
kind: archive
role: root
title: SSF建筑B_v1正式资源入库且暂不接入玩法
areas: [art, assets, rendering]
categories: [art]
status: recorded
verification: partial
created: '2026-10-06'
updated: '2026-10-06'
summary: 当前可见B_v1获用户正式存储放行，105资源、三档LOD、22动画与实际UE预览交付；保留原包，不接入玩法。
next_action: 后续单独决定进一步减面、玩法接入及实战性能。
relations:
  work_items: [WORK-20261005-003]
status_note: 正式存储范围技术和视觉检查完成，24项本体差额及部分壳覆盖差异保留，原预算未上调；无实战帧率结论。
---

# 2026-10-06：SSF 建筑正式资源入库

## 决定与边界

用户已看到实际Blender成品，随后明确“传ue，作为住正式资源存GuLiStrike中，暂不接入游戏中”。放行版本为`SSF_Production_B_v1`，Blender SHA256 `{delivery['release']['source_blend_sha256']}`，冻结清单 `{delivery['release']['manifest_sha256']}`；[授权记录]({links}approval_B_import_20261006.json)固定具体版本、目标和不接入玩法的范围。原A/B文件与2026-10-05归档保持历史。

## 资源清单

商城来源为原`/Game/Assets/SSF_Buildings/Buildings`，已审制作源在`ArtSource/Buildings/SSFStyle_20261005/Production_B_v1`。正式目标按最终项目内文件夹列出：

| 最终项目内文件夹 | 来源 | 内容与用途 | 依赖及迁移边界 |
|---|---|---|---|
{chr(10).join(rows)}
| `/Game/GuLiStrike/Buildings/SSFStylized/Shared` | 冻结B_v1图集/源Logo和灯片遮罩、源ACL设置 | 4母材质、2共用贴图、1独立机械动画压缩设置 | Engine、ACL及原有Script类；原ACL设置未改 |

共105资源：7骨骼网格、3静态网格、7骨架、6物理资产、22动画、38贴图、4母材质、16材质实例、1无人机蓝图和1压缩设置。10网格各三档LOD，阈值1.0/0.10/0.035；原动画名称、时长、帧数、采样率与4条可编辑变换曲线保留。无人机源未指派物理资产，继续不额外创建。

本轮未迁入商城示例地图、ExternalActors/ExternalObjects支持目录或其他资源包；未修改战场地图、玩法代码、游戏蓝图和数据表引用。仅复制原无人机行为蓝图，并使它引用新Drone资源。没有正式关卡或临时审核Map被保存。

## 检查与证据

{evidence}

原骨骼名、父级、轴心及参考姿态保留。实际UE导出FBX含全部三档，4UV、法线、配色/LOD/Team编码、权重和真实材质区段回读完成；独立ACL误差阈值0.001cm、虚拟顶点距离100cm保护机械件。保存后独立重载105个资源，原包引用0；新组允许Engine/ACL/既有Script依赖。

54张2048px实际UE截图与助手读图覆盖全部10资产×3LOD及六座建筑近景/战术/总览距离。预览在未保存Entry世界独立执行；35m/25°近景用于局部结构检查，300–700m/55°及1500m/55°用于资源辨识和自动LOD，不代表战场组合或实战帧率。

[正式清单]({links}UE_Delivery_v1/formal_delivery.json) · [资源入口]({links}UE_Delivery_v1/README.md) · [保存重载/组件姿态/截图]({links}UE_Delivery_v1/ue_validation.json) · [UE网格回读]({links}UE_Delivery_v1/ue_mesh_readback.json) · [视觉记录]({links}UE_Delivery_v1/visual_qa.json) · [冻结源核对]({links}UE_Delivery_v1/frozen_source_validation.json)

## 保留的差异

当前B_v1的24档本体面数仍高于原上限；描边在预算内，但部分壳覆盖和线宽比参考Freestyle弱。这些差异已随可见成品提交，当前具体版本获正式存储放行；原预算不自动改变，也不推导全局例外或性能通过。冻结496文件未改，原商城包保持。

本轮不运行PIE、联机、原生构建、游戏接入或性能测试。[需求](../RequirementDocument/20261005-SSF建筑美术统一与三档LOD.md)和[开发](../DevelopmentDocumentation/20261005-SSF建筑美术统一与三档LOD.md)继续记录原预算及后续范围。
"""
for path,text in ((reqp,req),(devp,dev),(normp,norm),(ledgerp,ledger)):
 path.write_text(text,encoding='utf8')
archive.write_text(archive_text,encoding='utf8')
decisions=json.loads((R/'review_decisions.json').read_text(encoding='utf8'))
decisions['UE_formal_import_performed']=True;decisions['ue_formal_import_performed']=True
decisions['formal_UE_delivery']='UE_Delivery_v1/formal_delivery.json';decisions['gameplay_integrated']=False
decisions['budget_exception_decision']['version_specific_storage_release']='current B_v1 retained with disclosed differences; original caps unchanged'
(R/'review_decisions.json').write_text(json.dumps(decisions,ensure_ascii=False,indent=2),encoding='utf8')
im=json.loads((D/'ue_import.json').read_text(encoding='utf8'));im['stage']='formal_resource_storage_complete'
im['independent_reload_and_visual_review_complete']=True
(D/'ue_import.json').write_text(json.dumps(im,ensure_ascii=False,indent=2),encoding='utf8')
result={'success':True,'before':before,'after':{str(p.relative_to(ROOT)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in (reqp,devp,normp,ledgerp,archive)}}
(D/'progress_update.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'success':True,'archive':str(archive)},ensure_ascii=False))
