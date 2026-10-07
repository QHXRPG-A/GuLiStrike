"""Finish current routes and record a scoped, user-authorized historical erratum."""
import json,re,zipfile,hashlib,os,difflib
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"Scripts/CommanderLOD"))
from common import require_unapproved_candidate
require_unapproved_candidate()
ROOT=Path(__file__).resolve().parents[2];ART=ROOT/'ArtSource/CommanderLOD_20261005';OUT=ART/'Reports'
backup=OUT/'document_text_before_erratum.zip'
def write(relative,text):
 file=ROOT/relative;before=file.read_bytes() if file.exists() else None
 with zipfile.ZipFile(backup,'a',zipfile.ZIP_DEFLATED) as archive:
  if before is not None and relative not in archive.namelist():archive.writestr(relative,before)
 file.write_text(text,encoding='utf8',newline='\n')
def amend(relative,fn):
 file=ROOT/relative;old=file.read_text(encoding='utf8');new=fn(old)
 if old!=new:write(relative,new)
with zipfile.ZipFile(backup) as archive:
 names=archive.namelist()
 for relative in names:
  if not relative.endswith('.md'):continue
  file=ROOT/relative;text=file.read_text(encoding='utf8');seen=set();lines=[]
  for line in text.splitlines():
   if line.startswith(('指挥官模型统一为 LOD0','> 2026-10-05 LOD 勘误：')):
    if line in seen:continue
    seen.add(line)
   lines.append(line)
  new=re.sub(r'\n{4,}','\n\n\n','\n'.join(lines)+'\n')
  if new!=text:write(relative,new)
 relative='Progress/DevelopmentDocumentation/20260916-Ship导入与扫荡者重防号风格重制.md'
 old=archive.read(relative).decode('utf8').splitlines();new=(ROOT/relative).read_text(encoding='utf8').splitlines()
 matcher=difflib.SequenceMatcher(None,old,new)
 for op,i,j,x,y in reversed(matcher.get_opcodes()):
  if op=='replace' and any('四档俯仰' in line for line in old[i:j]):new[x:y]=old[i:j]
 write(relative,'\n'.join(new)+'\n')
 relative='Progress/Gameplay/先驱号.md'
 original=archive.read(relative).decode('utf8')
 line=next(l for l in original.splitlines() if '四档总面数' in l)
 line=re.sub(r'四档总面数.*?阈值.*?。','当前三档候选合计37,338／15,905／1,944面，阈值1／.40／.06；正式引用待本版本B放行后切换。',line)
 amend(relative,lambda t:re.sub(r'^指挥官模型统一为 LOD0.*$',lambda m:line,t,count=1,flags=re.M))
 relative='Progress/Archive/20261005-彼之矛顶点动画制作与首轮源码交付.md'
 original=archive.read(relative).decode('utf8');table=original.split('## 资源清单',1)[1].split('源部署动画',1)[0]
 table=table.replace('各含四LOD','历史模型与工地副本；当前三档候选待B放行切换')
 table=table.replace('8张位置/旋转VAT与4张2K美术纹理','位置/旋转VAT与4张2K美术纹理；原始计数保留在机器回读')
 table=table.replace('4LOD实例','按距离档位的材质实例（当前制作规范共三档）').replace('LOD3取消轮廓、细线淡出','当前候选LOD2取消轮廓、细线淡出')
 amend(relative,lambda t:re.sub(r'## 资源清单.*?(?=源部署动画)',lambda m:'## 资源清单'+table,t,flags=re.S))
 # Preserve the original result dates; add a current-route clarification rather than changing their native-load claims.
 for relative in ['Progress/Archive/20261005-彼之矛顶点动画制作与首轮源码交付.md','Progress/Archive/20261005-彼之矛三档LOD进一步减面.md']:
  amend(relative,lambda t:t.replace('  work_items: [WORK-20261005-001]','  work_items: [WORK-20261005-001]\n  superseded_by: ARC-20261005-003') if 'superseded_by:' not in t else t)
 relative='Progress/Archive/20261005-彼之矛三档LOD进一步减面.md'
 amend(relative,lambda t:t.replace('next_action: 用户审核LOD_v2具体版本后，同步UE网格、动画纹理与对应索引参数；原生加载及玩法验收沿原任务继续。','next_action: 当前候选已转入CommanderLOD_3Tier_v1，待该具体版本B审核；历史LOD_v2仅作冻结证据。').replace('顶点动画重新采样32/24/16/8帧每动作','当前三档顶点动画每动作采样32/24/8帧').replace('175.69→104.72MiB，减少40.4%','当前三档97.03MiB（历史纹理预算保留在原始机器回读）'))

write('ArtSource/Mechs/BiZhiMao_20261005/LOD_v2/README.md','''# 彼之矛 LOD_v2 · 冻结历史源

本目录保留旧模型、图像和原始机器回读，用于来源核对；不作为当前制作入口。当前总共LOD0近景、LOD1中景、LOD2远景。

[当前三档审核](../../../CommanderLOD_20261005/Review/index.html) · [当前Blender](../../../CommanderLOD_20261005/BiZhiMao/BiZhiMao_3Tier.blend) · [当前制作入口](../../../CommanderLOD_20261005/README.md)

冻结模型：[BiZhiMao_LOD_v2.blend](BiZhiMao_LOD_v2.blend)，SHA256 `587deb1ecb0dde63c21d20a6353e48a448a3cd0bccadba5cc7d0f4b9e1b69fcb`；近景与原152骨骼离线制作Rig保留。四色、稀疏基础轮廓、三档明暗及四向步态沿用已有审核范围。

原始[几何读回](Reports/geometry_quality.json)仅证明对应历史文件。新的实际版本B待审核，正式资源尚未切换。后续制作与网页生成使用 `Scripts/CommanderLOD`；此目录的旧生产命令已撤出默认入口。
''')
write('ArtSource/Mechs/ControlRigMechStyle_20261004/CURRENT.md','''# ControlRig 冻结源与审核记录

原ControlRig B-v4／UE-v1及已有导入放行保持。指挥官彼之矛派生模型的当前制作入口为[CommanderLOD三档版本](../../CommanderLOD_20261005/README.md)，[实际审核](../../CommanderLOD_20261005/Review/index.html)待本版本B决定。

[冻结Blender源](Production_B_v4/ControlRigMech_B_v4_Production.blend) · [原导入放行](Approvals/Approval_B_v4_Import_20261004.json) · [历史决定](review_decisions.json) · [原UE交付证据](UE_Delivery_v1/README.md)

当前指挥官模型总共LOD0近景、LOD1中景、LOD2远景。原A/B冻结源、原始回读和批准记录仅适用于对应版本；不自动放行新的三档候选。面数差额与未运行FPS分别记录。
''')
for relative in ['ArtSource/Mechs/BiZhiMao_20261005/README.md','ArtSource/MechanicalAnimation_20260929/README.md']:
 current=os.path.relpath(ART/'README.md',(ROOT/relative).parent).replace('\\','/')
 amend(relative,lambda t:'> 当前指挥官三档制作入口：['+current+']('+current+')。以下是冻结来源与历史操作记录，旧脚本已撤出生产默认入口。\n\n'+t if '当前指挥官三档制作入口：' not in t else t)
relative='Progress/DevelopmentDocumentation/GuLiStrike美术规范.md'
amend(relative,lambda t:t.replace('《GuLiStrike 美术规范》v1.2','《GuLiStrike 美术规范》v1.3')+('''
## 2026-10-05：v1.3 指挥官模型三档勘误

用户明确统一Soldiers六种兵种为LOD0近景、LOD1中景、LOD2远景，保留近景、既有配色/线稿例外、ID和变形路线。重防号仍按WM01/ID2路由，玩家Ground不迁移。规则变更已确认；`CommanderLOD_3Tier_v1`实际成品B尚未通过，正式资源未切换。

[当前审核与哈希](../../ArtSource/CommanderLOD_20261005/review_manifest.json)、[候选实景](../../ArtSource/CommanderLOD_20261005/Review/index.html)、[迁移开发](20261005-指挥官三档LOD纠正与资源迁移.md)、[勘误归档](../Archive/20261005-指挥官三档LOD纠正与候选资源交付.md)。冻结源/原始回读继续保留，当前生成脚本使用三档入口。原生编译、PIE、联机和实战帧率未运行。
''' if 'v1.3 指挥官模型三档勘误' not in t else ''))
relative='Progress/Gameplay/彼之矛.md'
amend(relative,lambda t:t.replace('../../ArtSource/Mechs/BiZhiMao_20261005/Review/index.html','../../ArtSource/CommanderLOD_20261005/Review/index.html').replace('实际画面与逐帧动作见','当前三档候选区从`(48000,-34500,600)`cm开始，实际画面与逐帧动作见'))
relative='Progress/Gameplay/指挥官.md'
amend(relative,lambda t:t+('''
## 2026-10-05：单位模型LOD制作规则

Soldiers六种单位模型总共LOD0近景、LOD1中景、LOD2远景，保持既有距离、屏幕占比及滞回公共策略；玩家Ground不纳入。本轮[三档候选](../../ArtSource/CommanderLOD_20261005/Review/index.html)已保存，正式引用待对应版本B放行后整组切换。此项不更改传送技能等级、镜头模式或兵种玩法参数。
''' if '单位模型LOD制作规则' not in t else ''))
relative='Progress/DevelopmentDocumentation/20261005-指挥官三档LOD纠正与资源迁移.md'
amend(relative,lambda t:t.replace('status: in_progress','status: verification').replace('next_action: 完成实际候选预览、审核Map与引用回读，提交对应版本B审核。','next_action: 用户审核CommanderLOD_3Tier_v1具体成品后切换正式资源组；彼之矛原生配置另待获准编译加载。').replace('status_note: 已完成源查询、候选模型与导出回读，审核场景和最终检查进行中；正式资源未切换。','status_note: 六种候选、真实预览、审核Map和静态/美术回读已交付；实际版本B待用户，正式资源未切换。').replace('- [ ] UE候选完整资源组','- [x] UE候选完整资源组').replace('- [ ] 三档实际对比','- [x] 三档实际对比').replace('- [ ] 当前脚本默认路由','- [x] 当前脚本默认路由').replace('车辆近景14,676面；中景5,889面；远景1,508面，远景比1,500目标多8面','UE整车近景14,676面、中景5,893面、远景1,510面，远景比1,500目标多10面；FBX为14,676／5,889／1,508面，远景多8面')+('''
## 交付证据与差额

版本：`CommanderLOD_3Tier_v1`，规范v1.3，实际版本B待用户决定。每种候选模型恰有三档；[UE候选回读](../../ArtSource/CommanderLOD_20261005/Reports/candidate_readback.json)、[资源组](../../ArtSource/CommanderLOD_20261005/Reports/resource_groups.json)、[面数和材质区段](../../ArtSource/CommanderLOD_20261005/Reports/metrics.json)、[纹理预算](../../ArtSource/CommanderLOD_20261005/Reports/texture_budget.json)。

车辆每台8个可见组件、原兼容骨架和材质保持；源实例没有动画类，Blender提供轮轴机构示意，不能冒充游戏移动验收。先驱号7个原VAT动作、重防号/扫荡者原机械WPO路线保持。彼之矛每档UV编号与6UV、像素、材质索引通过；运行无骨骼。

复制LOD时出现材质区段映射丢失，已修复候选并同步默认生成脚本，重新采集实际UE图。重防号保留最简源档后远景剪影较破碎，描边遮盖明显；几何与所选源严格相同，源/候选实际画面并列提交，未擅自重建主要机构或判为通过。

[9档所选几何与独立导出对照](../../ArtSource/CommanderLOD_20261005/Reports/selected_geometry_readback.json)、[机械分区UV](../../ArtSource/CommanderLOD_20261005/Reports/mechanical_uv_readback.json)、[车辆权重/骨架](../../ArtSource/CommanderLOD_20261005/Reports/skin_readback.json)、[材质区段](../../ArtSource/CommanderLOD_20261005/Reports/material_section_readback.json)、[正式资源与冻结源仍保持](../../ArtSource/CommanderLOD_20261005/Reports/formal_after.json)。

审核样机在`/Game/Maps/LVL_CommanderMassPrototype`，18组、60个网格Actor、18标签及地面，EditorOnly、无碰撞/导航占位；保存后组件引用回读。项目三档镜头提供候选艺术参考，图片强制LOD0，不能替代自动LOD切换实战验收。

静态检查、美术回读、文档链接/元数据检查已运行。没有执行原生编译、Live Coding、PIE、联机或FPS；旧编辑器模块尚无新的逐顶点VAT DataAsset字段，彼之矛候选纹理/材质可预览，正式运行配置需获准编译加载后完成。
''' if '## 交付证据与差额' not in t else ''))
print('DOCUMENT_ROUTES_FINALIZED')
