from pathlib import Path
import json,hashlib,re
P=Path('D:/UE5.7/test1');R=P/'ArtSource/Mechs/ControlRigMechStyle_20261004';O=R/'Production_B_v4'
p=O/'production_manifest.json';m=json.loads(p.read_text(encoding='utf-8'));manifesthash=hashlib.sha256(p.read_bytes()).hexdigest();blendhash=m['final_blender_sha256']
assert m['version']=='B-v4' and m['B']=='pending'
archive=P/'Progress/Archive/20261004-ControlRig机甲B_v4减少线稿保留轮廓.md';assert not archive.exists()
ids=[]
for f in (P/'Progress/Archive').glob('20261004-*.md'):
 match=re.search(r'^id: ARC-20261004-(\d+)',f.read_text(encoding='utf-8')[:4096],re.M)
 if match:ids.append(int(match[1]))
archiveid=f'ARC-20261004-{max(ids,default=0)+1:03d}'
def fields(text,values):
 head,body=text.split('\n---\n',1)
 for name,value in values.items():
  head,n=re.subn(r'^'+name+r':.*$',name+': '+value,head,flags=re.M);assert n==1,name
 return head+'\n---\n'+body
req=P/'Progress/RequirementDocument/20261004-ControlRig机甲美术统一.md';text=req.read_text(encoding='utf-8')
text=fields(text,{'next_action':'用户审核B-v4少量内线/基础轮廓及预算差额，具体版本B通过后再导出回读和正式UE交付。','status_note':'A-v2已通过；用户要求降低B-v3线稿含量，B-v4已在当前Blender显示，基础轮廓与三档保持；B待审核，原预算仍超标。'})
text+='\n\n## 2026-10-04 线稿密度修订\n\n用户明确“线稿含量低一些，保留基础轮廓”，仅调整本ControlRig机甲：内部短线减少、主要接缝更淡，基础轮廓壳和三档明暗保留。当前[B-v4实际交付](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v4/README.md)已显示于当前Blender，附新旧同机位对照及完整三个动作；B-v3归入修订历史，B-v4待用户决定。线稿简化不改变面数预算，未扩大为全局美术规则或UE导入授权。\n'
req.write_text(text,encoding='utf-8')
dev=P/'Progress/DevelopmentDocumentation/20261004-ControlRig机甲美术统一.md';text=dev.read_text(encoding='utf-8')
text=fields(text,{'summary':'B-v4按用户反馈减少细碎内部线、减淡主要接缝，完整保留基础轮廓与三档明暗，当前Blender已显示；本体/绑定/动作不变，B待审核且预算超标。','next_action':'用户审核B-v4的简线效果和原预算差额；通过具体B版本后再导出回读及正式UE交付。','status_note':'A-v2已通过，B-v3按用户要求降低线稿含量进入修订，B-v4待审核；面数预算未变化，UE未导入。'})
text=text.replace('| B 当前实际成品 | B-v3 | **待审核** | 真实结构线、外壳与三档材质已在当前Blender显示，四图及三个完整动作重渲染；预算仍超标 |','| B 实际成品历史 | B-v3 | **用户要求降低线稿含量** | “线稿含量低一些，保留基础轮廓”；旧冻结文件保留 |\n| B 当前实际成品 | B-v4 | **待审核** | 少量内部线、基础轮廓及三档明暗已在当前Blender显示，四图与三个完整动作重交付；预算仍超标 |')
section=f'''

## 2026-10-04 B-v4 减少线稿，保留基础轮廓

依据用户“线稿含量低一些，保留基础轮廓”，新增[B-v4简线成品](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v4/README.md)和[新旧同机位对照](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v4/Review_B_v4.html)。实际内线字段过滤长度小于0.18m或相邻最大面面积小于0.03m²的短小细节，重新烘焙2K独立遮罩。LOD0选边8,477→1,453，选边数量下降82.86%；不能将此数字当作画面黑线像素或GPU性能改善比例。内线强度1→0.30、半宽0.010→0.006m，LOD1/2/3强度0.22/0.10/0。

基础轮廓壳形状、法线、权重与0.018/0.020/0.025m宽度保持；三档0.42/0.74/1、配色、四足/单炮/关节和机械结构保持。当前Blender默认STYLE_REALTIME_B_v4，两个可旋转3D材质窗口显示本版，Freestyle关闭。保存回读严格确认四档本体位置/三角表面/角法线/UV0/逐角颜色/权重、152骨骼名/父级/参考矩阵与完整源动作的曲线/关键帧/手柄/插值均等于B-v3，只有内线字段、遮罩和线稿材质参数改变。

四张同姿态/尺度的原生2K图和全部283帧实际shader动作重新渲染。完整部署/待机/行走MP4已解码核对尺寸、帧数及9个首/中/尾检查点，RGB与对应PNG误差在有损编码容差内；本轮不声称全部解码帧逐帧比对。未改变源部署位移、伸缩或缩放。

面数保持LOD0本体57,323/描边12,794/合计70,117，LOD1合计65,006，LOD2合计51,896，LOD3合计38,428，原四档差额仍30,117/48,006/45,896/36,428。未改变预算结论、UE等效shader/导出回读/ControlRig副本/指挥官镜头或实战FPS余项。

B-v4清单SHA256 `{manifesthash}`，Blender SHA256 `{blendhash}`；旧B-v3全部341个冻结文件哈希核对未变。当前B-v4待用户针对本版决定，A-v2放行有效，B-v3记为用户要求减少线稿的历史。规范仍v1.2，本次只针对ControlRig机甲，不改UE正式目录、源包、玩法身份或C++；[增量归档](../Archive/20261004-ControlRig机甲B_v4减少线稿保留轮廓.md)。
'''
assert '## 2026-10-04 B-v4 减少线稿' not in text;text+=section;dev.write_text(text,encoding='utf-8')
archive.write_text(f'''---
schema: guli-progress/v1
id: {archiveid}
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

保存回读核对四档本体的表面/角法线/UV0/逐角颜色/权重、外壳形状/法线/权重/宽度、152骨骼名/父级/参考矩阵及源全部动画曲线/键/手柄/插值严格不变；三档0.42/0.74/1与四色分区保持，Freestyle关闭。三个完整动作全283帧以新shader重新渲染，保留位移/伸缩/缩放，原生解码核对尺寸/帧数与9个首中尾检查点通过；本轮未将全部解码帧逐一比对。

面数和原预算差额不变：LOD0总70,117、LOD1总65,006、LOD2总51,896、LOD3总38,428，均超预算，减少线稿不等于改善网格/FPS。UE等效shader、导出回读、ControlRig副本、指挥官镜头及实战帧率未执行。本轮没有外部迁移或UE正式资产/玩法/C++修改，只针对本机甲；全局规范仍v1.2。

B-v4清单SHA256 `{manifesthash}`；Blender SHA256 `{blendhash}`。B-v3全部341个冻结文件核对未变，B-v3审核历史记录用户减少线稿的修订要求；B-v4待决定，A-v2既有放行有效。实际资源位于 `ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v4`，依赖保留的A-v2/Source/B-v3及新B-v4脚本；制作分件保持，图集/生产网格需要按编辑结果重新生成。

当前记录见[实施文档](../DevelopmentDocumentation/20261004-ControlRig机甲美术统一.md#2026-10-04-b-v4-减少线稿保留基础轮廓)。按用户计划及[制作技能](../../.agents/skills/guli-model-production/SKILL.md)/[规范§2](../RequirementDocument/GuLiStrike美术规范.md#2-制作与审核流程)，具体B版本通过后再导出回读和正式UE导入。
''',encoding='utf-8')
ledger=P/'Progress/DevelopmentDocumentation/GuLiStrike美术规范.md';text=ledger.read_text(encoding='utf-8');marker='## 2026-10-04 ControlRig B-v3 实际线稿';assert text.count(marker)==1
tail='''## 2026-10-04 ControlRig B-v3/B-v4 实际线稿

B-v3完成实际线稿与三档材质后，用户要求“线稿含量低一些，保留基础轮廓”。B-v4减少短内线、减淡主要接缝，基础轮廓与三档保持，当前Blender已显示；预算未改善，B待审核、UE未导入，规范v1.2。见[当前记录](20261004-ControlRig机甲美术统一.md#2026-10-04-b-v4-减少线稿保留基础轮廓)、[B-v3归档](../Archive/20261004-ControlRig机甲B_v3实际线稿与三档明暗.md)与[B-v4归档](../Archive/20261004-ControlRig机甲B_v4减少线稿保留轮廓.md)。
'''
updated=text.split(marker)[0]+tail;assert len(updated.encode('utf-8'))<=30*1024
ledger.write_text(updated,encoding='utf-8')
print('B4_PROGRESS_RECORDED',archiveid,'LEDGER_BYTES',ledger.stat().st_size)
