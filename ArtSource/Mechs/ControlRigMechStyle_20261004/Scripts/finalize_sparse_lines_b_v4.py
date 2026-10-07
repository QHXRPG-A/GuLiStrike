"""Freeze the line-density revision, with actual before/after images and motion checkpoints."""
from pathlib import Path
from datetime import datetime,timezone
import json,hashlib
import numpy as np
from PIL import Image
from html.parser import HTMLParser
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004');O=R/'Production_B_v4'
assert not (O/'production_manifest.json').exists()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
old=R/'Production_B_v3/production_manifest.json';assert sha(old)=='7b9196d4c356751e7eeef763a4a4e3c57966161e132ea7324aef648eebe0e2bf'
prior=json.loads(old.read_text(encoding='utf-8'))
for item in prior['files']:
 f=R/item['path'];assert f.suffix.lower() not in ('.uasset','.umap','.uexp','.ubulk','.pak')
 assert f.stat().st_size==item['bytes'] and sha(f)==item['sha256'],item['path']
report=json.loads((O/'line_revision_report.json').read_text(encoding='utf-8'))
readback=json.loads((O/'saved_line_readback_report.json').read_text(encoding='utf-8'))
blend=O/'ControlRigMech_B_v4_Production.blend';digest=sha(blend)
assert readback['saved_blender_sha256']==digest
motions=json.loads((O/'AnimationPreviews/animation_preview_report.json').read_text(encoding='utf-8'))
assert all(x['source_blend_at_render_sha256']==digest for x in motions)
assert all(x['line_revision_report_sha256']==sha(O/'line_revision_report.json') for x in motions)
checkpoints=[]
for clip,count in [('Deploy',76),('Idle',131),('Walk',76)]:
 for label,frame in [('Start',1),('Middle',count//2+1),('End',count)]:
  a=np.array(Image.open(O/f'AnimationPreviews/Mech_{clip}_Frames/{frame:04d}.png').convert('RGB'),dtype=np.float32)
  b=np.array(Image.open(O/f'AnimationPreviews/Decoded_{clip}_{label}.png').convert('RGB'),dtype=np.float32)
  assert a.shape==b.shape==(1280,1280,3)
  delta=np.abs(a-b);mae=float(delta.mean());fg=float(delta[np.min(a,axis=2)<190].mean())
  assert mae<2 and fg<6,(clip,label,mae,fg)
  checkpoints.append({'clip':clip,'label':label,'frame':frame,'rgb_mae_0_255':mae,'foreground_rgb_mae_0_255':fg})
(O/'AnimationPreviews/movie_checkpoint_fidelity.json').write_text(json.dumps({'version':'B-v4','source_blender_sha256':digest,'passed':True,'decoded_checkpoints':9,'scope':'Three delivered MP4s: frame count and dimensions plus start/middle/end fidelity. Does not claim every decoded frame was compared.','checkpoints':checkpoints},indent=2),encoding='utf-8')
views=[('Hero','三分之四'),('Front','正面'),('Left','左侧'),('Back','背面')]
lod0=report['lods'][0]
tables='\n'.join(f"| LOD{x['lod']} | {x['body_triangles']:,} | {x['outline_triangles']:,} | {x['total_triangles']:,} | +{x['total_delta']:,} | {x['internal_line_strength']:.2f} |" for x in report['lods'])
text=f'''# ControlRig 机甲 B-v4：减少内部线稿，保留基础轮廓

依据用户“线稿含量低一些，保留基础轮廓”。已在当前 Blender 显示本版，默认场景 `STYLE_REALTIME_B_v4`，两个真实可旋转材质预览窗口。

[Blender源文件](ControlRigMech_B_v4_Production.blend) · [当前Blender截图](Blender_Live_B_v4.png) · [B-v3/B-v4同机位对照及完整动作](Review_B_v4.html) · [冻结清单](production_manifest.json)

内部线只保留较长的主要装甲接缝，过滤短于0.18m或相邻最大面面积小于0.03m²的细碎线，重新原生烘焙独立2K遮罩。LOD0选择边由{lod0['old_selected_edges']:,}降至{lod0['selected_edges']:,}，减少{lod0['selected_edge_reduction_percent']:.2f}%；该数字是模型空间的选线数量，不能当作画面黑线像素或GPU成本下降比例。内部线强度由1降到0.30，半宽由0.010m降到0.006m；LOD1/2/3强度为0.22/0.10/0。未绘制拓扑对角线。

**基础轮廓壳的几何、法线、权重和宽度完整保留**，LOD0/1/2宽度仍0.018/0.020/0.025m。色块和三档系数0.42/0.74/1保持，所有成品图和动作均关闭Freestyle。原装甲、炮管、四足、关节和内部机械结构未删除；本次只改内部线的选择字段、线宽、强度及遮罩。

[仅基础轮廓](LineDiagnostics/ControlRigMech_B_v4_BaseOutlineOnly.png) / [基础轮廓＋少量内线](LineDiagnostics/ControlRigMech_B_v4_SparseLines.png)，来自同一真实模型，没有图片叠加。四图原生2048²，同姿态、同尺度、同正交相机：

| 视图 | 原B-v3 | 新B-v4 |
|---|---|---|
'''+ '\n'.join(f'| {label} | [较密内线](../Production_B_v3/ControlRigMech_B_v3_{view}.png) | [少量内线](ControlRigMech_B_v4_{view}.png) |' for view,label in views)+f'''

## 保存回读与动作

[保存回读](saved_line_readback_report.json)确认四档本体位置、三角索引、角法线、UV0、逐角颜色和权重严格等于B-v3；描边壳形状/法线/权重/宽度相同；152骨骼名/父级/参考矩阵、完整三个动作的全部曲线/关键帧/手柄/插值严格相同。该检查只涉及本次线稿修订，原UE兼容、预算和性能余项不因此通过。

[完整部署](AnimationPreviews/ControlRigMech_B_v4_Deploy.mp4) / [完整待机](AnimationPreviews/ControlRigMech_B_v4_Idle.mp4) / [完整行走](AnimationPreviews/ControlRigMech_B_v4_Walk.mp4)。全283帧按本版真实shader重渲染，1280²/15fps，源30fps取样、保留精确两端。视频已原生解码核对帧数、尺寸和9个首/中/尾检查点，均在有损编码容差内，[检查点报告](AnimationPreviews/movie_checkpoint_fidelity.json)；本轮未逐帧比对所有解码帧。源动作位移、伸缩、缩放和速度不变。

## 原预算及交付边界

| 档位 | 本体三角面 | 描边三角面 | 合计 | 合计超出 | 内线强度 |
|---|---:|---:|---:|---:|---:|
{tables}

线稿减少没有降低网格面数，四档仍超原预算，不能记为批量性能改善。各档一本体区段，LOD0–2另一个描边区段。四张2K图集已打包：[BaseColor](Textures/ControlRigMech_BaseColor_2K.png) / [稀疏内线](Textures/ControlRigMech_InternalLineMask_2K.png) / [功能遮罩](Textures/ControlRigMech_FunctionalMask_2K.png) / [ORM](Textures/ControlRigMech_ORM_2K.png)。颜色仍用逐角颜色，保留三个UV通道；功能/ORM接入和UE等效shader、导出回读、ControlRig副本、指挥官镜头及FPS验证待后续。

本版是ControlRig机甲的局部线稿方向修订，不修改其他资产和全局规范v1.2。A-v2既有放行有效；B-v3收到减少线稿的修订要求，B-v4待用户针对本版决定，[审核记录](../review_decisions.json)。按用户已定计划与[制作技能](../../../../.agents/skills/guli-model-production/SKILL.md)/[美术规范§2](../../../../Progress/RequirementDocument/GuLiStrike美术规范.md#2-制作与审核流程)“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”，B通过后再正式导入；本轮UE目录、源包、玩法引用和C++均未改动。Blender SHA256 `{digest}`。
'''
(O/'README.md').write_text(text,encoding='utf-8')
cards=''.join(f'''<article><h2>{label} · 同机位</h2><div class="compare"><img src="../Production_B_v3/ControlRigMech_B_v3_{view}.png" alt="B-v3较密线条"><img class="after" src="ControlRigMech_B_v4_{view}.png" alt="B-v4简线"><span class="a">B-v3</span><span class="b">B-v4</span></div><input aria-label="{label}线条对照" type="range" min="0" max="100" value="50" oninput="this.previousElementSibling.querySelector('.after').style.clipPath='inset(0 0 0 '+this.value+'%)'"><a href="ControlRigMech_B_v4_{view}.png">原生2K</a> · <a href="../References_A_v2/ControlRigMech_A_v2_{view}.png">原已审A-v2参考</a></article>''' for view,label in views)
videos=''.join(f'<article><h2>{label}</h2><video controls loop preload="metadata" poster="AnimationPreviews/ControlRigMech_{clip}_Start.png" src="AnimationPreviews/ControlRigMech_B_v4_{clip}.mp4"></video></article>' for clip,label in [('Deploy','完整部署'),('Idle','完整待机'),('Walk','完整行走')])
doc=f'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ControlRig B-v4 简线对照</title><style>body{{margin:0;background:#f7f2e8;color:#2c3735;font:16px/1.65 system-ui,"Microsoft YaHei",sans-serif}}main{{max-width:1440px;margin:auto;padding:24px}}a{{color:#8e3a2a}}.grid{{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:20px}}article{{padding:12px;border:1px solid #d5c09c;background:white}}img,video{{display:block;width:100%}}.compare{{position:relative}}.after{{position:absolute;inset:0;clip-path:inset(0 0 0 50%)}}.a,.b{{position:absolute;top:8px;padding:4px 8px;background:#2c3735;color:white}}.a{{left:8px}}.b{{right:8px}}input{{width:100%;accent-color:#8e3a2a}}@media(max-width:800px){{.grid{{grid-template-columns:1fr}}}}</style><main><h1>ControlRig B-v4 · 少量内部线，保留基础轮廓</h1><p>主要接缝更淡，细碎内线减少；外轮廓壳、配色与三档明暗保留。实际材质渲染，关闭Freestyle。</p><p><a href="ControlRigMech_B_v4_Production.blend">Blender文件</a> · <a href="Blender_Live_B_v4.png">当前Blender截图</a> · <a href="README.md">制作、预算和检查范围</a></p><div class="grid">{cards}</div><h2>完整实际动作</h2><div class="grid">{videos}</div><p>仅基础轮廓：<img src="LineDiagnostics/ControlRigMech_B_v4_BaseOutlineOnly.png" alt="基础轮廓"></p><p>B-v4待审核，面数预算未改善，UE未导入。<a href="production_manifest.json">本版冻结清单</a></p></main></html>'''
(O/'Review_B_v4.html').write_text(doc,encoding='utf-8')
files=[blend,O/'README.md',O/'Review_B_v4.html',O/'Blender_Live_B_v4.png',O/'line_revision_report.json',O/'saved_line_readback_report.json']
files+=[O/f'ControlRigMech_B_v4_{v}.png' for v,_ in views]
for name in ('Textures','LineDiagnostics','AnimationSource','AnimationPreviews'):files+=sorted(f for f in (O/name).rglob('*') if f.is_file())
for name in ('reduce_line_density_b_v4.py','prepare_motion_b_v4.py','render_motion_b_v4.py','verify_movie_checkpoints_b_v4.py','readback_line_revision_b_v4.py','finalize_sparse_lines_b_v4.py'):files.append(R/'Scripts'/name)
entries=[]
for f in files:
 assert f.suffix.lower() not in ('.uasset','.umap','.uexp','.ubulk','.pak')
 item={'path':f.relative_to(R).as_posix(),'bytes':f.stat().st_size,'sha256':sha(f)}
 if f.suffix.lower()=='.png':
  with Image.open(f) as im:item['width'],item['height']=im.size;im.verify()
 entries.append(item)
class Links(HTMLParser):
 def handle_starttag(self,tag,attrs):
  for k,v in attrs:
   if k in ('href','src') and v and not v.startswith('#') and v!='production_manifest.json':assert (O/v).is_file(),v
Links().feed(doc)
m={'version':'B-v4','created_utc':datetime.now(timezone.utc).isoformat(),'art_revision':'1.2','stage':'actual_Blender_candidate_submitted_for_B','A':'approved','B':'pending','user_B_approval':None,'revises_version':'B-v3','revision_evidence':'线稿含量低一些，保留基础轮廓','final_blender_sha256':digest,'source_B_v3_manifest_sha256':sha(old),'B_v3_frozen_files_validated':len(prior['files']),'line_report':report,'readback':readback,'motion_frames_rendered':283,'decoded_movie_checkpoints_checked':9,'budget_result':'unchanged, over original caps','UE_formal_import':'not run','UE_export_readback':'pending B approval','formal_target_after_B':'/Game/GuLiStrike/Mechs/ControlRigMech','runtime_fps':'not measured','files':entries}
(O/'production_manifest.json').write_text(json.dumps(m,ensure_ascii=False,indent=2),encoding='utf-8');manifesthash=sha(O/'production_manifest.json')
dec=json.loads((R/'review_decisions.json').read_text(encoding='utf-8'));olddec=dec['B'];assert olddec['submitted_version']=='B-v3' and olddec['status']=='pending'
olddec.update(status='revision_requested',user_decision='降低内部线稿含量，保留基础轮廓',user_message_evidence='线稿含量低一些，保留基础轮廓',decision_date='2026-10-04');dec['B_history'].append(olddec)
dec['B']={'status':'pending','submitted_version':'B-v4','manifest_path':'Production_B_v4/production_manifest.json','manifest_sha256':manifesthash,'blender_sha256':digest,'user_decision':None,'user_message_evidence':None,'budget_result':'over_budget_delta_unchanged','line_direction':'sparse internal lines; base outline preserved'}
(R/'review_decisions.json').write_text(json.dumps(dec,ensure_ascii=False,indent=2),encoding='utf-8')
(R/'CURRENT.md').write_text(f'''# 当前候选：ControlRig B-v4 简线版

按用户“线稿含量低一些，保留基础轮廓”，已减少细碎内线、降低主要接缝强度，外轮廓壳与三档明暗保留，当前Blender可旋转预览已显示。

[Blender文件](Production_B_v4/ControlRigMech_B_v4_Production.blend) · [新旧对照与动作](Production_B_v4/Review_B_v4.html) · [制作及检查范围](Production_B_v4/README.md)。B-v4清单SHA256 `{manifesthash}`，Blender SHA256 `{digest}`。

A-v2放行有效，B-v4待审核；线稿简化未减少模型面数，预算仍超标，UE未导入。旧B-v3文件完整保留，决定见[审核记录](review_decisions.json)。
''',encoding='utf-8')
print('B4_FROZEN',manifesthash,'BLENDER',digest,'FILES',len(entries),'DECODED_CHECKPOINTS',len(checkpoints))
