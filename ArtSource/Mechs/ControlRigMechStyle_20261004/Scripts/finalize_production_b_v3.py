"""Freeze the actual real-time Blender line revision after native image and movie QA."""
from pathlib import Path
from datetime import datetime,timezone
from html.parser import HTMLParser
import hashlib,json
from PIL import Image
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004');O=R/'Production_B_v3'
assert not (O/'production_manifest.json').exists(),'Frozen versions must never be overwritten'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
old={
 'References_A_v1/reference_manifest.json':'93324b32cb12fb9273bdb3d7ad5c60ce59798655f30bb657f53cca7f18db4fb4',
 'References_A_v2/reference_manifest.json':'ea47e95d2e5ee1f8d2bfe3ccd1fd9b249873e99fe31d93f9c9935b72af1ed9ac',
 'Production_B_v1/production_manifest.json':'00fa36bd0bc046db39011d5fbbeeaf7d8442d95989c0ace028ed80bdee462853',
 'Production_B_v2/production_manifest.json':'79018077be8ed6c1ce1d1679594fcb8b01040dd85918d169251f1629818d6fa9'}
validated={}
for name,digest in old.items():
 p=R/name;assert sha(p)==digest,name
 items=json.loads(p.read_text(encoding='utf-8'))['files']
 for item in items:
  f=R/item['path'];assert f.suffix.lower() not in ('.uasset','.umap','.uexp','.ubulk','.pak')
  assert f.stat().st_size==item['bytes'] and sha(f)==item['sha256'],item['path']
 validated[name]=len(items)
line=json.loads((O/'realtime_line_report.json').read_text(encoding='utf-8'))
audit=json.loads((O/'realtime_readback_audit.json').read_text(encoding='utf-8'))
motions=json.loads((O/'AnimationPreviews/animation_preview_report.json').read_text(encoding='utf-8'))
fidelity=json.loads((O/'AnimationPreviews/movie_frame_fidelity_report.json').read_text(encoding='utf-8'))
blend=O/'ControlRigMech_B_v3_Production.blend';blendhash=sha(blend)
assert audit['bone_count']==152 and not audit['freestyle_in_actual_render']
assert fidelity['passed'] and fidelity['checked_frames']==283 and fidelity['source_blend_sha256']==blendhash
assert all(x['source_blend_at_render_sha256']==blendhash for x in motions)
assert all(x['saved_body_readback_report_sha256']==sha(O/'realtime_readback_audit.json') for x in motions)
assert all(sha(O/f"AnimationPreviews/ControlRigMech_B_v3_{x['clip']}.mp4")==x['mp4_sha256'] for x in fidelity['clips'])
lods=line['lods'];screens=[1.,.40,.16,.06]
for x,screen in zip(lods,screens):x['screen_size']=screen
budget='\n'.join(f"| LOD{x['lod']} | {x['body_triangles']:,} | {x['outline_triangles']:,} | {x['total_triangles']:,} | {x['total_cap']:,} | +{x['total_delta']:,} | {x['screen_size']:.2f} |" for x in lods)
views=[('Hero','三分之四'),('Front','正面'),('Left','左侧'),('Back','背面')]
normmax=max(x['max_corner_normal_angle_degrees'] for x in audit['body_checks'])
normalzero=[x['original_zero_corner_normals_recalculated'] for x in audit['body_checks']]
md=f'''# ControlRig 机甲 B-v3：实际模型三渲二、线稿、三档明暗

用户指出“所以线稿没搞？”并再次明确“三渲二，线稿，三档明暗”。B-v2 的主展示依赖 Freestyle，实际材质内线未达到同样效果；此前直接回答“做了”不准确。本版修正可旋转实际模型的材质与描边，不沿用展示图作为材质完成证据。**A-v2 已通过，B-v3 待审核，预算仍超标，UE 正式导入未执行。**

[当前 Blender 文件](ControlRigMech_B_v3_Production.blend) · [同机位参考/实际线条开关/动作对照](Review_B_v3.html) · [当前 Blender 原生截图](Blender_Live_B_v3.png) · [版本清单](production_manifest.json)

最终 Blender SHA256：`{blendhash}`。文件默认场景 `STYLE_REALTIME_B_v3`，两个窗口均为真实 3D 材质预览，左三分之四、右背面；可以直接旋转。已移除 B-v3 文件内旧的 `REVIEW_B_ReferenceMatched` 展示场景，旧 B-v2 文件完整保留。

## 实际着色与线稿

三档为亮部 / 中间色 / 阴影，线性系数 1.0 / 0.74 / 0.42，Constant 阶梯、阈值 0 / 0.12 / 0.55，固定艺术光向 (0.35, -0.55, 0.76)。分色沿已审四色：`#557B78` 主装甲、`#8E3A2A` 炮口/检修护甲、`#2C3735` 骨架/软管、`#D5C09C` 关节盖与原镜片；线色 `#1B2422`。

结构线选取真实装甲边界、分色边界与机械折角，不绘制三角网格对角线。使用两个额外 UV 通道保存边缘距离并与独立原生烘焙 2K 遮罩组合，近景半宽约 0.010 m、边缘过渡 0.002 m。内线强度 LOD0–3 为 1 / 0.85 / 0.6 / 0。外轮廓为有骨骼权重的反向壳，Geometry Nodes 宽度可调，LOD0–2 为 0.018 / 0.020 / 0.025 m；小零件、关节盖和内骨架依靠表面结构线，主要青/红装甲使用描边壳。实际渲染和材质预览均关闭 Freestyle，未使用后期描线或图片叠加。

实际同机位开关图：[关闭线条](LineDiagnostics/ControlRigMech_B_v3_NoLines.png) / [仅结构线](LineDiagnostics/ControlRigMech_B_v3_InternalOnly.png) / [仅轮廓壳](LineDiagnostics/ControlRigMech_B_v3_OutlineOnly.png) / [全部线条](LineDiagnostics/ControlRigMech_B_v3_AllLines.png)。这些来自同一个实际模型和材质。

## 2K 图纸与参考

四图原生 2048 × 2048，姿态、正交相机及尺度沿用已审 A-v2。

| 视图 | 已审 A-v2 | 实际 B-v3 |
|---|---|---|
'''+ '\n'.join(f'| {label} | [参考](../References_A_v2/ControlRigMech_A_v2_{view}.png) | [实际成品](ControlRigMech_B_v3_{view}.png) |' for view,label in views)+f'''

## 预算差额

| 档位 | 本体三角面 | 描边三角面 | 合计 | 合计上限 | 差额 | 屏幕尺寸 |
|---|---:|---:|---:|---:|---:|---:|
{budget}

四档本体保留 B-v2 减面结果，没有为线条修改顶点位置、三角表面、原 UV0、权重或骨骼。外壳对照本版更完整，描边成本增加。四档本体均超标，LOD0–2 描边也超标；本体差额分别 {', '.join(str(x['body_delta']) for x in lods)}，描边差额分别 {', '.join(str(x['outline_delta']) for x in lods)}。各档一个本体材质区段，0–2 另一个描边区段。按原计划提交差额和真实视觉对比，未擅自删主要机构或将其记为预算通过。

真实同机位 LOD：[0](LODComparison/ControlRigMech_B_v3_LOD0_SameCamera.png) / [1](LODComparison/ControlRigMech_B_v3_LOD1_SameCamera.png) / [2](LODComparison/ControlRigMech_B_v3_LOD2_SameCamera.png) / [3](LODComparison/ControlRigMech_B_v3_LOD3_SameCamera.png)。额外逐角 UV 距离产生顶点拆分需求，LOD0 本体上限可达 171,969 个逐角实例；这不是 UE 实测顶点数，三角面预算不能替代 GPU 顶点/带宽或帧率测量。

## 动作、法线和制作源

[部署](AnimationPreviews/ControlRigMech_B_v3_Deploy.mp4) / [待机](AnimationPreviews/ControlRigMech_B_v3_Idle.mp4) / [行走](AnimationPreviews/ControlRigMech_B_v3_Walk.mp4)。本版实际 shader + 壳重新渲染全部 283 帧：1280²、15fps，原动作 30fps 取样，分别 76 / 131 / 76 帧，保留精确两端及完整 5 / 8.666667 / 5 秒。所有视频帧已解码与对应原生渲染帧比较，均在有损编码容差内，[报告](AnimationPreviews/movie_frame_fidelity_report.json)。部署原位移、伸缩和缩放保留；没有修改动作速度。检查了首、中、尾实际图和完整预览，未声称自动覆盖全部时刻穿插。

[保存回读审计](realtime_readback_audit.json)确认 152 骨骼名、父级与参考矩阵严格等于 B-v2；四档本体顶点、三角索引、UV0、组名和权重严格等于 B-v2，无无权重顶点，最大权重和误差约 1.36e-7。复制自定义角法线时隔离三角法线编码扇区，避免旧扇区重编码产生非单位法线；输出均为有效单位法线。原 B-v2 四档存在 {normalzero} 个零角法线，本版由 Blender 按局部表面重算。有效源角法线重编码最大方向差 {normmax:.4f}°，99百分位各档低于 0.007°；不宣称法线位级一致。

761 个制作分件、86 个已核对镜像、源表面修正和源完整三动作仍保留。隐藏 `PRODUCTION_LODS_SINGLE_BODY_SECTION` 并显示 `EDITABLE_MECHANICAL_PARTS` 可继续编辑制作分件；该集合是制作源，修改后须重新生成 LOD/结构距离 UV/图集，不会自动更新本版生产网格。源四足、单炮、比例、关节轴心和原单侧天线保留，不虚构全部从空白重拓扑。B-v2 的源表面/脚底检查作为未改变本体与绑定的基线留档，不能冒充新增全时段碰撞检测。

## 贴图、迁移与审核边界

四张 2K：[BaseColor](Textures/ControlRigMech_BaseColor_2K.png)、[独立内线遮罩](Textures/ControlRigMech_InternalLineMask_2K.png)、[功能遮罩](Textures/ControlRigMech_FunctionalMask_2K.png)、[ORM](Textures/ControlRigMech_ORM_2K.png)。贴图已打包，目录内完整副本可随文件移动。当前颜色仍使用 `GuLi_PaletteLinear` 逐角颜色准确分色，功能遮罩和 ORM 尚未完整接入。四张 RGBA8 未压缩共 64 MiB，不是 UE 压缩驻留预算。

正式 UE 等效材质需保留全部三个 UV 通道及逐角颜色，再重建阶梯与结构距离逻辑；该迁移、UE 法线/动画导出回读、ControlRig 副本、三档指挥官镜头和实战帧率都未执行。当前 Blender 视觉已可直接审核，不能据此认定 UE 外观或批量性能通过。源无物理资产、挂点为 0；本轮无 UE 源包、玩法身份、C++ 或战斗引用修改。

A-v2 原用户放行仍有效，[决定记录](../review_decisions.json)未把重复强调视觉要求当作 B 通过。按用户已定计划及[制作技能](../../../../.agents/skills/guli-model-production/SKILL.md)的“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”，B-v3 具体版本通过后才导出回读并导入 `/Game/GuLiStrike/Mechs/ControlRigMech`。旧 A/B 冻结文件全部哈希复核未改。
'''
(O/'README.md').write_text(md,encoding='utf-8')
cards=''.join(f'''<article><h2>{label} · 同姿态同尺度</h2><div class="compare"><img src="../References_A_v2/ControlRigMech_A_v2_{view}.png" alt="已审A-v2"><img class="after" src="ControlRigMech_B_v3_{view}.png" alt="实际B-v3"><span class="tag a">A-v2 参考</span><span class="tag b">B-v3 实际材质</span></div><input aria-label="{label}参考对照" type="range" min="0" max="100" value="50" oninput="this.previousElementSibling.querySelector('.after').style.clipPath='inset(0 0 0 '+this.value+'%)'"><a href="ControlRigMech_B_v3_{view}.png">原生2K实际成品</a></article>''' for view,label in views)
diag=''.join(f'<article><h2>{label}</h2><img src="LineDiagnostics/ControlRigMech_B_v3_{name}.png" alt="{label}"></article>' for name,label in [('NoLines','实际关闭线条'),('InternalOnly','实际仅结构线'),('OutlineOnly','实际仅外轮廓壳'),('AllLines','实际全部线条')])
videos=''.join(f'<article><h2>{label}</h2><video controls loop preload="metadata" poster="AnimationPreviews/ControlRigMech_{clip}_Start.png" src="AnimationPreviews/ControlRigMech_B_v3_{clip}.mp4"></video></article>' for clip,label in [('Deploy','完整部署'),('Idle','完整待机'),('Walk','完整行走')])
lodcards=''.join(f'<article><h2>LOD{i} 实际模型</h2><img src="LODComparison/ControlRigMech_B_v3_LOD{i}_SameCamera.png" alt="LOD{i}"></article>' for i in range(4))
rows=''.join(f"<tr><td>LOD{x['lod']}</td><td>{x['body_triangles']:,}</td><td>{x['outline_triangles']:,}</td><td>{x['total_triangles']:,}</td><td>+{x['total_delta']:,}</td></tr>" for x in lods)
doc=f'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ControlRig B-v3 实际线稿与三档明暗</title><style>body{{margin:0;background:#f7f2e8;color:#2c3735;font:16px/1.65 system-ui,"Microsoft YaHei",sans-serif}}main{{max-width:1440px;margin:auto;padding:24px}}h1{{font-size:28px}}h2{{font-size:20px}}a{{color:#8e3a2a}}.notice{{padding:14px;border-left:5px solid #8e3a2a;background:#e8ddc7}}.grid{{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:20px}}article{{padding:12px;border:1px solid #d5c09c;background:white}}img,video{{display:block;width:100%}}.compare{{position:relative}}.after{{position:absolute;inset:0;clip-path:inset(0 0 0 50%)}}.tag{{position:absolute;top:8px;padding:4px 8px;background:#2c3735;color:white}}.a{{left:8px}}.b{{right:8px}}input{{width:100%;accent-color:#8e3a2a}}table{{width:100%;border-collapse:collapse;margin:20px 0}}td,th{{padding:10px;text-align:right;border-bottom:1px solid #d5c09c}}@media(max-width:800px){{.grid{{grid-template-columns:1fr}}}}</style><main><h1>ControlRig B-v3 · 三渲二、实际线稿、三档明暗</h1><p><a href="ControlRigMech_B_v3_Production.blend">Blender源文件</a> · <a href="Blender_Live_B_v3.png">当前Blender截图</a> · <a href="README.md">制作说明与检查范围</a> · <a href="production_manifest.json">版本哈希</a></p><p class="notice">全部成品图及动作使用实际生产材质与描边壳，关闭Freestyle。当前可旋转3D材质预览显示相同效果。B-v3待审核；四档预算及0–2描边超标，UE未导入。</p><div class="grid">{cards}</div><h2>实际线条开关对照</h2><div class="grid">{diag}</div><h2>实际完整动作</h2><div class="grid">{videos}</div><h2>本体与描边三角面</h2><table><tr><th>LOD</th><th>本体</th><th>描边</th><th>合计</th><th>合计超出</th></tr>{rows}</table><div class="grid">{lodcards}</div><p>Blender SHA256：<code>{blendhash}</code>。<a href="../review_decisions.json">审核状态</a>。GPU顶点拆分、UE等效材质和实战帧率未验证。</p></main></html>'''
(O/'Review_B_v3.html').write_text(doc,encoding='utf-8')
files=[blend,O/'README.md',O/'Review_B_v3.html',O/'Blender_Live_B_v3.png',O/'realtime_line_report.json',O/'realtime_readback_audit.json',R/'Approvals/Approval_A_v2_20261004.json']
files += [O/f'ControlRigMech_B_v3_{v}.png' for v,_ in views]
for folder in ('Textures','LineDiagnostics','LODComparison','AnimationSource','AnimationPreviews'):
 files += sorted(p for p in (O/folder).rglob('*') if p.is_file())
for script in ('build_realtime_lines_b_v3.py','audit_realtime_lines_b_v3.py','prepare_motion_scripts_b_v3.py','render_motion_b_v3.py','verify_movies_b_v3.py','audit_movie_fidelity_b_v3.py','finalize_production_b_v3.py'):
 files.append(R/'Scripts'/script)
entries=[]
for f in files:
 assert f.suffix.lower() not in ('.uasset','.umap','.uexp','.ubulk','.pak')
 e={'path':f.relative_to(R).as_posix(),'bytes':f.stat().st_size,'sha256':sha(f)}
 if f.suffix.lower()=='.png':
  with Image.open(f) as im:e['width'],e['height']=im.size;im.verify()
 entries.append(e)
assert all(next(e for e in entries if e['path']==f'Production_B_v3/ControlRigMech_B_v3_{view}.png')['width']==2048 for view,_ in views)
manifest={'version':'B-v3','created_utc':datetime.now(timezone.utc).isoformat(),'art_revision':'1.2','stage':'actual_Blender_candidate_submitted_for_B','A':'approved','approved_A_manifest_sha256':old['References_A_v2/reference_manifest.json'],'B':'pending','user_B_approval':None,'UE_formal_import':'not run','UE_export_readback':'pending B approval','formal_target_after_B':'/Game/GuLiStrike/Mechs/ControlRigMech','final_blender_sha256':blendhash,'main_shader':'actual structural distance + independent 2K mask + skinned outline; Freestyle false','three_tone_coefficients':[.42,.74,1.],'lod_budget_result':'over budget; actual differences and comparison submitted; no exception accepted','lods':lods,'readback':audit,'all_native_movie_frames_checked':283,'previous_frozen_files_validated':validated,'revises_version':'B-v2','revision_reason':'User requested actual model line art, toon shading and three discrete tones; previous main showcase Freestyle did not prove shader completion.','runtime_fps':'not measured','files':entries}
(O/'production_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8');digest=sha(O/'production_manifest.json')
class Links(HTMLParser):
 def handle_starttag(self,tag,attrs):
  for k,v in attrs:
   if k in ('href','src') and v and not v.startswith('#'):assert (O/v).is_file(),v
Links().feed(doc)
decisions=json.loads((R/'review_decisions.json').read_text(encoding='utf-8'))
prior=decisions['B'];assert prior['submitted_version']=='B-v2' and prior['status']=='pending'
prior.update(status='revision_requested',user_decision='实际模型线稿修订',user_message_evidence=['所以线稿没搞？','三渲二，线稿，三档明暗'],decision_date='2026-10-04')
decisions.setdefault('B_history',[]).append(prior)
decisions['B']={'status':'pending','submitted_version':'B-v3','manifest_path':'Production_B_v3/production_manifest.json','manifest_sha256':digest,'blender_sha256':blendhash,'user_decision':None,'user_message_evidence':None,'budget_result':'over_budget_delta_submitted','line_fidelity':'actual_model_shader_and_outline_submitted; UE equivalent pending'}
(R/'review_decisions.json').write_text(json.dumps(decisions,ensure_ascii=False,indent=2),encoding='utf-8')
(R/'CURRENT.md').write_text(f'''# 当前成品候选：ControlRig B-v3

[实际模型三渲二、结构线、轮廓与三档明暗](Production_B_v3/README.md)已放入当前 Blender 的可旋转材质预览。A-v2 已通过，B-v3 待用户审核，预算超标，UE 正式导入与导出回读尚未执行。

[Blender文件](Production_B_v3/ControlRigMech_B_v3_Production.blend) · [当前Blender截图](Production_B_v3/Blender_Live_B_v3.png) · [参考、实际线条开关与动作](Production_B_v3/Review_B_v3.html)。B清单SHA256 `{digest}`，BlenderSHA256 `{blendhash}`。

B-v2 主展示依赖 Freestyle、实际内线不足；按用户反馈新增 B-v3，旧冻结文件完整保留。具体决定见 [审核记录](review_decisions.json)。
''',encoding='utf-8')
print('B_V3_FROZEN',digest,'BLENDER',blendhash,'FILES',len(entries),'OLD_VALIDATED',validated)
