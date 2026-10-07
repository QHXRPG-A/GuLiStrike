"""Publish concrete native B review pages and explicit unapproved budget differences."""
import json,html,hashlib
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1'
d=json.loads((O/'construction_report.json').read_text(encoding='utf8'));v=json.loads((O/'native_validation.json').read_text(encoding='utf8'));motion=json.loads((O/'animation_deformation_validation.json').read_text(encoding='utf8'));approval=json.loads((R/'approval_A.json').read_text(encoding='utf8'))
labels={'AirBase':'空军基地','CloningCenter':'克隆中心','CommandCenter':'指挥中心','MilitaryFactory':'军工厂','Reactor':'反应堆','StrategyCenter':'战略中心','Floor':'平台','Lamp':'灯柱','Light':'灯片','Drone':'无人机'}
model_hash=hashlib.sha256((O/'SSF_Production_B_v1.blend').read_bytes()).hexdigest();assert model_hash==v['source_blend_sha256']==motion['source_blend_sha256']
exceptions=[];table=['| 资产 | 原本体 | LOD0 实际本体＋描边 | LOD1 实际本体＋描边 | LOD2 实际本体＋描边 | 本体差额 L0/L1/L2 |','|---|---:|---:|---:|---:|---:|']
for a in d['assets']:
 es=a['lods'];table.append('| '+labels[a['key']]+' | '+str(a['source_triangles'])+' | '+' | '.join(f"{e['body_triangles']}＋{e['outline_triangles']}" for e in es)+' | '+' / '.join('+'+str(e['body_delta']) for e in es)+' |')
 for e in es:
  if e['body_delta']:
   exceptions.append({'asset':a['key'],'LOD':e['LOD'],'actual_body_triangles':e['body_triangles'],'body_cap':e['body_cap'],'body_delta':e['body_delta'],'actual_outline_triangles':e['outline_triangles'],'outline_cap':e['outline_cap'],'outline_delta':e['outline_delta'],'total_triangles':e['total_triangles'],'total_cap':e['total_cap'],'difference_reason':'原分件、曲面、细长零件截面及动画/破坏可见内部结构全部保留；超出保护容差的减面候选已拒绝。','visual_comparison':'Sheets/'+a['key']+'_LOD_Comparison.png','reference_comparison':'Sheets/'+a['key']+'_Reference_Comparison.png','decision':'pending_user_exception_decision'})
unique={rec['file'].replace('\\','/'):rec for a in d['assets'] for rec in a['textures'].values() if isinstance(rec,dict) and 'file' in rec}
pixels=sum(rec['resolution'][0]*rec['resolution'][1] for rec in unique.values());texture_bytes=sum((O/file).stat().st_size for file in unique)
budget={'version':d['version'],'source_blend_sha256':model_hash,'all_caps_passed':not exceptions,'body_exception_count':len(exceptions),'all_outline_caps_passed':all(e['outline_delta']==0 for a in d['assets'] for e in a['lods']),'exceptions_approved':False,'exceptions':exceptions,'unique_texture_files':len(unique),'unique_texture_pixels':pixels,'stored_PNG_bytes':texture_bytes,'RGBA8_base_level_equivalent_bytes':pixels*4,'RGBA8_full_mip_equivalent_bytes':round(pixels*4*4/3),'UE_compression_memory_not_measured':True,'performance_not_measured':True}
(O/'budget_exception_record.json').write_text(json.dumps(budget,ensure_ascii=False,indent=2),encoding='utf8')
(O/'Budget_Exceptions.md').write_text('# SSF B_v1 本体预算差额与待决定例外\n\n原定预算保持。以下实际面数并未达标，也尚无例外通过决定；不以削除主要结构强行达标。描边全部在各自预算内，灯柱48面、灯片2面保持。\n\n'+'\n'.join(table)+'\n\n每个资产的 [LOD 对比](Sheets) 和审核页提供同机位成品图。逐档差额、预算及理由见 [结构化记录](budget_exception_record.json)。B 与例外决定分别记录，不能仅凭建模指令自动通过。\n',encoding='utf8')
md=['# SSF 建筑实际成品审核 B_v1','',
'六座建筑及平台、灯柱、灯片、无人机已完成 Blender 制作，按已审 A_v7 配色、原模型尺寸/装配与原动画制作。当前提交具体成品 **SSF_Production_B_v1**；用户视觉审核 B 与24项本体预算例外均待决定。',
'',f'用户“{approval["user_quote"]}”作为 A_v7 制作放行依据；[A 决定](approval_A.json)固定其版本与清单哈希。所有参考 A_v1–A_v7 保留。',
'','- [完整图板与22个视频审核页](review_B_v1.html)','- [实际可编辑 Blender 成品](Production_B_v1/SSF_Production_B_v1.blend)','- [原始逐档面数与制作记录](Production_B_v1/construction_report.json)','- [本体预算差额与待决定例外](Production_B_v1/Budget_Exceptions.md)','- [网格、权重和材质核对](Production_B_v1/native_validation.json)','- [实际已审色值与参数保持](Production_B_v1/palette_preservation_validation.json)','- [22动作 × 3LOD 实际变形核对](Production_B_v1/animation_deformation_validation.json)',
'','![实际建筑成品总览](Production_B_v1/Overview_Buildings_B_v1.png)','',
'## 一比一依据与真实成品','',
'42张2048×2048效果/正交图、十套三视图图板、十套参考同机位对照、十套三档LOD对比和两张4096×4096组合均由实际成品渲染。相机、姿态和正交尺度读取已审 A_v7；没有用二维生成图代替 Blender 成品。',
'',f'保留{sum(len(a["parts"]) for a in d["assets"])}个制作分件、7套骨架共{sum(a["rig_bone_count"] for a in d["assets"])}根骨骼、原骨骼名称/层级/Root/轴心和全部22个原名称动作。金属分件权重为1；克隆中心软管保留源柔性权重。编辑集合含镜像和减面修改器，三档合并副本用于渲染/后续导出。',
'','近档与中档使用独立内部线稿遮罩和实际绑定描边壳；远档内部线稿为0、描边为0。源半透明标识和灯片保留，Light为原2面单面贴片，因此侧/背图不可见属原结构。',
'','![真实材质拆分](Production_B_v1/Sheets/Actual_Style_Breakdown.png)','',
'固定艺术光向 `(0.35,-0.55,0.76)`；明暗阈值 `0.38/0.68`、因子 `0.40/0.72/1.00`，沿用 A_v7。内部线稿 R=源面板线、G=规则结构折线，强度随LOD为 `1.00/0.70/0`；`Base Color`、`Team Color` 参数保留。成品外轮廓来自真实壳网格，Freestyle关闭。壳在预算内覆盖主要不透明构件，部分外轮廓比参考Freestyle细；源线稿重投影也有局部差异，对照中如实呈现，严格视觉一致性仍待B决定。',
'','## 实际面数与预算例外','',
'本体优先清理冗余面、重合点与可安全减少的分段；保留开门、运转和破坏时暴露的内部结构。为维持曲面、细长天线截面和所有运动零件，六座建筑、平台和无人机共24档本体未达到原上限；当前方案是保留结构并申请例外，尚未擅自调整预算。描边各档已达预算，实际材质近/中档最多3区段、远档最多2区段。','',*table,'',
'默认屏幕尺寸 `1.0 / 0.10 / 0.035` 已写入三档对象元数据；此阶段尚未验证 UE 自动切换和项目镜头。',
'',f'建筑四张2K图集/资产，平台四张4K，无人机和灯柱四张1K；共享源Logo及灯片各1K，共{len(unique)}张唯一新贴图，PNG共{texture_bytes/1024/1024:.2f}MiB。未压缩RGBA8基准约{pixels*4/1024/1024:.0f}MiB（完整mip约{pixels*4*4/3/1024/1024:.1f}MiB）；这是纹理预算记录，UE压缩/流送/驻留和实战帧率未测量。',
'','## 每个资产的审核图板','']
for a in d['assets']:
 k=a['key'];md.extend([f'### {k} · {labels[k]}','',f'[实际效果与三视图](Production_B_v1/Sheets/{k}_Actual_Views.png) · [A_v7同机位对照](Production_B_v1/Sheets/{k}_Reference_Comparison.png) · [三档LOD](Production_B_v1/Sheets/{k}_LOD_Comparison.png)',''])
md.extend(['## 全部动画与技术检查','',
'22个完整视频均在一个固定镜头并排播放三个实际LOD，15fps预览包含准确开始/结束及最后1/15秒停帧；动作来自UE逐帧30fps采集的6003个姿态样本，不裁剪破坏碎片运动。原 `Destoy`、`Edle` 名称按源包保留。',''])
for a in d['assets']:
 if a.get('animations'):
  md.extend([f'**{labels[a["key"]]}**','']+[f'- [{clip}](Production_B_v1/AnimationPreviews/{clip}.mp4)' for clip in a['animations']]+[''])
md.extend([f'实际骨骼抽样矩阵最大差 `{v["actual_native_action_sample_matrix_max_error"]:.9f}`，22动作每个5个时间点、各3LOD全部本体顶点对照源骨骼变换，最大位置差 `{motion["maximum_deformed_vertex_error_m"]:.9f} m`。这些检查证明 Blender 制作与源运动契约，不能代替用户视觉审核或正式UE导入回读。',
'','- [实际MP4回读：帧数、尺寸、帧率和110个检查帧](Production_B_v1/AnimationPreviews/movie_readback_report.json)','- [视频检查帧与原始渲染像素误差](Production_B_v1/AnimationPreviews/movie_pixel_validation.json)','- [助手视觉检查](Production_B_v1/visual_qa.json)','- [冻结交付清单](Production_B_v1/production_manifest.json)',
'','## 决定与交付边界','',
f'本成品 Blender SHA256：`{model_hash}`。A_v7清单 SHA256：`{approval["manifest_sha256"]}`。','',
'需要对 **SSF_Production_B_v1** 的外观/运动作出 B 决定，并对记录中的24项本体差额作出明确例外或继续调整决定。当前 B 与例外均未通过。依据用户原定流程及[模型制作技能](../../../.agents/skills/guli-model-production/SKILL.md)“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”，提交可审成品后停在B。正式导出回读、UE材质/LOD/22动画/物理资产/无人机蓝图副本和项目镜头验收均在B放行后继续；本轮未导入正式目录，未覆盖商城源包。'])
(R/'Review_B_v1.md').write_text('\n'.join(md)+'\n',encoding='utf8')
css='''body{margin:0;background:#f3eee5;color:#1a182f;font:18px/1.65 system-ui,"Microsoft YaHei",sans-serif}main{max-width:1400px;margin:auto;padding:36px}h1{font-size:38px}h2{margin:55px 0 18px}p{max-width:1150px}a{color:#0a6f73}img{display:block;width:100%;height:auto}figure{margin:24px 0;background:#f3eee5}figcaption{padding:8px 12px;color:#69637c}nav{position:sticky;top:0;background:#f3eee5e8;padding:12px;backdrop-filter:blur(7px);border-bottom:1px solid #ccc3bb;display:flex;flex-wrap:wrap;gap:15px;z-index:1}.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:24px}.note{border-left:5px solid #ee9d58;padding:20px;background:#fee4d9}details{margin:18px 0}summary{cursor:pointer;font-weight:700}video{width:100%;background:#f3eee5}table{border-collapse:collapse;width:100%;font-size:16px}td,th{border-bottom:1px solid #ccc3bb;padding:12px;text-align:left}code{overflow-wrap:anywhere;font-size:13px}@media(max-width:900px){.grid{grid-template-columns:1fr}main{padding:20px}table{font-size:12px}}'''
parts=['<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>SSF 实际成品 B_v1</title><style>'+css+'</style><main><h1>SSF 建筑实际成品 · B_v1</h1>',
'<p>按已审 A_v7 与原模型制作。六座建筑、四件配套、三档LOD与22个完整动作均来自实际 Blender 成品。</p>',
'<div class="note"><strong>B 与预算例外待审核。</strong>原结构和运动零件全部保留；24档本体超出原定预算，差额如下。描边在预算内覆盖主要构件，部分外轮廓比参考图细，线稿重投影有局部差异，须结合对照审核。当前尚未导出回读或正式导入UE。</div>',
'<p><a href="Production_B_v1/SSF_Production_B_v1.blend">打开可编辑 Blender 成品</a> · <a href="Review_B_v1.md">完整文字与技术记录</a> · <a href="Production_B_v1/Budget_Exceptions.md">预算例外记录</a></p>',
'<nav>'+''.join(f'<a href="#{a["key"]}">{labels[a["key"]]}</a>' for a in d['assets'])+'<a href="#budget">预算</a><a href="#motion">动画</a></nav>',
'<figure><img src="Production_B_v1/Overview_Buildings_B_v1.png" alt="六座实际成品"><figcaption>实际成品三分之四效果；六座采用各自独立色系。</figcaption></figure>',
'<details><summary>查看线稿与三档明暗真实拆分</summary><img loading="lazy" src="Production_B_v1/Sheets/Actual_Style_Breakdown.png" alt="线稿与三档真实拆分"></details>']
for a in d['assets']:
 k=a['key'];parts.extend([f'<section id="{k}"><h2>{html.escape(k)} · {labels[k]}</h2>',f'<figure><img loading="lazy" src="Production_B_v1/Sheets/{k}_Actual_Views.png" alt="实际效果与三视图"><figcaption>同姿态、同尺度的真实模型。</figcaption></figure>',f'<details><summary>A_v7 / 实际成品同机位对照</summary><img loading="lazy" src="Production_B_v1/Sheets/{k}_Reference_Comparison.png" alt="参考对照"></details>',f'<figure><img loading="lazy" src="Production_B_v1/Sheets/{k}_LOD_Comparison.png" alt="三档实际LOD"><figcaption>本体/描边分别计数；三档相同镜头。内部细线在远档关闭。</figcaption></figure>','</section>'])
parts.extend(['<h2 id="budget">原定预算与实际差额</h2><p>预算例外尚待决定。本体各档均保留完整分件及动画中暴露的内部结构，拒绝造成曲面塌陷、截面畸变或部件丢失的减面。</p><div style="overflow:auto"><table><tr><th>资产</th><th>原本体</th><th>LOD0 本体＋描边</th><th>LOD1 本体＋描边</th><th>LOD2 本体＋描边</th><th>本体差额 L0/L1/L2</th></tr>'])
for a in d['assets']:
 parts.append('<tr><td>'+labels[a['key']]+'</td><td>'+str(a['source_triangles'])+'</td>'+''.join(f'<td>{e["body_triangles"]}＋{e["outline_triangles"]}</td>' for e in a['lods'])+'<td>'+' / '.join('+'+str(e['body_delta']) for e in a['lods'])+'</td></tr>')
parts.extend(['</table></div><h2 id="motion">22个完整原动画 · 三档同时播放</h2><p>15fps预览完整源动作，固定机位容纳全部运动范围。视频已通过原文件回读与检查帧对照；实战帧率未测试。</p>'])
for a in d['assets']:
 if not a.get('animations'):continue
 parts.append('<h3>'+labels[a['key']]+'</h3><div class="grid">')
 for clip in a['animations']:parts.append(f'<figure><video controls loop preload="none" poster="Production_B_v1/AnimationPreviews/{clip}_Checkpoint_0.png" src="Production_B_v1/AnimationPreviews/{clip}.mp4"></video><figcaption>{html.escape(clip)}</figcaption></figure>')
 parts.append('</div>')
parts.extend(['<h2>固定版本与后续</h2><p>当前版本：SSF_Production_B_v1。外观/运动B与预算例外未通过；审核放行后执行导出回读及UE正式副本交付。</p><p>Blender SHA256：<code>'+model_hash+'</code></p><p>A_v7清单SHA256：<code>'+approval['manifest_sha256']+'</code></p></main></html>'])
(R/'review_B_v1.html').write_text('\n'.join(parts),encoding='utf8')
(O/'README.md').write_text('# SSF_Production_B_v1\n\n实际 Blender 5.2.2 LTS 成品，提交审核B；24项本体预算例外待决定。\n\n[完整审核](../Review_B_v1.md) · [图板与全部视频](../review_B_v1.html) · [预算差额](Budget_Exceptions.md)。\n\n打开 `SSF_Production_B_v1.blend`：初始场景 `Production_Assembly` 为原尺寸组合。每资产有 `Production_<Key>` 场景；`<Key>_EDITABLE_MECHANICAL_PARTS` 保留分件/镜像/减面修改器，默认隐藏供编辑；`<Key>_LOD0/1/2_BAKED` 是三档渲染/后续导出副本。使用 `Rig_<Key>` 的22个同名Action查看原动作，30fps时间轴；未经审批不得直接替换正式UE资产。\n\n半透明Logo和灯片使用源UV，内部线稿使用 `SSF_AtlasUV` 的独立RG遮罩；`SSF_PaletteLinear` 控制已审色块，保留Base Color和Team Color节点参数。原Root与骨骼层级恢复；金属刚性，克隆中心软管保留源柔性。\n\n所有预览来自实际成品；无Freestyle代替真实壳。文件打包贴图，外部38张新纹理另交付。`Renders/`为原始2K/4K像素；`Sheets/`和`QA/`仅布局这些真实图；`AnimationPreviews/`为全部原动作三档同播视频及回读证据。\n\n原单位/轴心/骨骼运动在Blender检查通过；FBX导出回读、UE压缩/引用/物理资产/无人机蓝图/自动LOD及项目镜头验收尚未执行，实战帧率未测量。\n\n`production_manifest.json` 发布后冻结本版；后续修改创建新版本，工作中间文件与Logs不属于清单交付。\n',encoding='utf8')
print('SSF_REVIEW_PAGES_WRITTEN',model_hash,len(exceptions),flush=True)
