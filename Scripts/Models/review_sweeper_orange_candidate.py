"""Inspect saved B renders and publish their local review record, not a game test."""
import hashlib
import json
from html import escape
from pathlib import Path
import numpy as np
from PIL import Image, ImageFilter

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/SweeperTeamColor_v1_20261008'
build=json.loads((OUT/'build-report.json').read_text(encoding='utf8'))
readback=json.loads((OUT/'blender-saved-readback.json').read_text(encoding='utf8'))
approval_file=OUT/'formal-import-authorization.json'
approval=json.loads(approval_file.read_text(encoding='utf8')) if approval_file.exists() else {}
released=approval.get('blend_sha256')==build['blend_sha256']
formal_file=OUT/'formal-ue-import.json'
formal=json.loads(formal_file.read_text(encoding='utf8')) if formal_file.exists() else {}
runtime_file=OUT/'UE/runtime-local-team-readback.json'
runtime=json.loads(runtime_file.read_text(encoding='utf8')) if runtime_file.exists() else {}
qa={'version':build['version'],'checks':[],'errors':list(readback['errors']),
    'visual_scope':'Original, blue, red and UV-mask renders; identical cameras, poses and tone parameters',
    'A':'New drawings waived by user; original orange regions explicitly chosen',
    'B':'released_for_formal_import' if released else 'pending',
    'formal_ue_import':bool(formal.get('success')),'runtime_verified':bool(runtime.get('success'))}
for view in ['Hero','Front','Left','Back']:
    original=np.array(Image.open(OUT/'Previews'/f'Original_{view}.png').convert('RGB'),dtype=np.int16)
    blue=np.array(Image.open(OUT/'Previews'/f'Blue_{view}.png').convert('RGB'),dtype=np.int16)
    red=np.array(Image.open(OUT/'Previews'/f'Red_{view}.png').convert('RGB'),dtype=np.int16)
    mask=Image.open(OUT/'Previews'/f'Mask_{view}.png').convert('L')
    # Exclude the UV boundary plus normal image antialiasing from the fixed interior.
    enlarged=np.array(mask.filter(ImageFilter.MaxFilter(7)))>2
    fixed=~enlarged
    entry={'view':view,'fixed_interior_pixels':int(fixed.sum()),
        'blue_vs_original_fixed_max_rgb_error':int(np.abs(blue-original)[fixed].max()),
        'red_vs_original_fixed_max_rgb_error':int(np.abs(red-original)[fixed].max()),
        'blue_vs_red_fixed_max_rgb_error':int(np.abs(blue-red)[fixed].max()),
        'team_region_pixels':int(enlarged.sum()),
        'blue_red_changed_pixels':int((np.abs(blue-red).max(axis=2)>2).sum())}
    for key in ['blue_vs_original_fixed_max_rgb_error','red_vs_original_fixed_max_rgb_error','blue_vs_red_fixed_max_rgb_error']:
        if entry[key]>2:qa['errors'].append(view+': fixed interior differs: '+key+'='+str(entry[key]))
    qa['checks'].append(entry)
qa['files']=[{'file':str(p.relative_to(OUT)),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(OUT.rglob('*'))
    if p.is_file() and p.suffix.lower() in ['.blend','.png','.json'] and p.name not in ['visual-qa.json']]
(OUT/'visual-qa.json').write_text(json.dumps(qa,ensure_ascii=False,indent=2),encoding='utf8')
rows=[]
for view,label in [('Hero','效果'),('Front','正面'),('Left','左侧'),('Back','背面')]:
    cards=[]
    for key,title in [('Original','原版橙色'),('Blue','己方蓝色 #6AA4BE'),('Red','敌方红色 #A34053'),('Mask','白色为可变区')]:
        path=f'Previews/{key}_{view}.png'
        cards.append(f'<figure><a href="{path}" target="_blank"><img src="{path}" loading="lazy" alt="{escape(title+label)}"></a><figcaption>{escape(title)}</figcaption></figure>')
    rows.append(f'<h2>{label}</h2><div class="row">'+''.join(cards)+'</div>')
html='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>扫荡者橙区队色 B_v1</title><style>body{background:#eeeee9;color:#283844;font:16px system-ui;margin:30px;line-height:1.65}h1{font-size:28px}h2{font-size:21px}.row{display:grid;grid-template-columns:repeat(4,1fr);gap:14px}figure{margin:0;background:#fff;border:1px solid #ccc}img{width:100%;display:block}figcaption{padding:8px}a{color:#274e61}@media(max-width:900px){.row{grid-template-columns:repeat(2,1fr)}}</style>
<h1>扫荡者 · 橙区队色 B_v1 · 待用户审核</h1>
<p>依据用户“​​不用出图了，就这橙色的区域作为team colour就好了”。取消新参考图，仅将原橙色变为队色。其他原涂装、结构、三档明暗及无线稿例外保持。</p>
<p><a href="Sweeper_OrangeTeamColor_B_v1.blend">Blender 成品</a> · <a href="build-report.json">分区与来源</a> · <a href="blender-saved-readback.json">保存回读</a> · <a href="visual-qa.json">固定区检查</a></p>
<p>蓝红共用同一套原网格。近中远三档沿原 UV 使用精确橙区遮罩，防止远景跨色三角面串色。此候选未导入正式 UE，未接入或验证运行自动改色。</p>
<a href="Previews/Original_Blue_Red_Overview.png" target="_blank"><img src="Previews/Original_Blue_Red_Overview.png" alt="原版、蓝方、红方并排成品"></a>
'''+''.join(rows)+'''<h2>原三档 LOD</h2><a href="Previews/Blue_ThreeLOD.png" target="_blank"><img src="Previews/Blue_ThreeLOD.png" alt="原三档 LOD"></a>
<p>Blender LOD1 原源文件为1355三角，UE渲染LOD1为1363三角；属于既有FBX导出差异。本轮未重建或覆盖UE网格。克隆兵营占位2004、领地据点占位2007已按用户要求排除后续制作。</p></html>'''
if released:
    html=html.replace('B_v1 · 待用户审核','B_v1 · 用户已放行正式导入')
    html=html.replace('此候选未导入正式 UE，未接入或验证运行自动改色。',
        '用户2026-10-09明确要求“导入 UE、接入自动改色”。当前正式导入及双客户端Q召唤改色核对已完成；<a href="UE/index.html">查看实际UE效果与交付证据</a>。')
(OUT/'index.html').write_text(html,encoding='utf8')
review=f'''# 扫荡者橙区队色 B_v1

用户已取消新增参考图并指定原模型全部橙色为队色区。本候选只改色，不改网格。

- 原源：`{build['source']}`；SHA256 `{build['source_sha256']}`。
- 成品：[Sweeper_OrangeTeamColor_B_v1.blend](Sweeper_OrangeTeamColor_B_v1.blend)；SHA256 `{build['blend_sha256']}`。
- 蓝方 `#6AA4BE`，红方 `#A34053`，全部原橙色随 TeamPrimary 变化；其他纹理、轮胎、炮管、白板、黄灯保持原版。
- [成品画廊](index.html)提供原版、蓝红与实际遮罩同机位四视图；这些是 Blender 成品渲染，不是生成参考图。
- [保存回读](blender-saved-readback.json)检查三档源几何、UV、法线、原色数据、权重、层级与持久变换；蓝红实例共享一个网格。
- [固定区检查](visual-qa.json)仅检查同机位实际成品渲染，避开纹理边界及抗锯齿外延；不视为UE运行测试。
- 遮罩使用原UV `UVmap_0`：新角点Alpha编码角色3，必须与[精确橙区UV遮罩](Masks/T_Sweeper_OrangeTeamMask.png)共同判断。简化LOD的跨色面不能只按顶点Alpha整面换色。原 `Attribute` RGB/Alpha及全部动画UV保持原样。
- 保留扫荡者既有无附加线稿/描边例外与原三档明暗参数。
- 克隆兵营占位2004和领地据点占位2007不制作。它们的旧登记作为历史保留，本轮未更新UE模型表或正式资产。

A：用户明确取消出图，并直接确认现有橙色分区。B：待用户审核。审核后才导入正式UE并接入本地阵营CPD；本轮未编译、未运行游戏，尚不能声称扫荡者自动改色已在游戏中生效。
'''
if released:
    review=review.replace('本候选只改色，不改网格。','本版本只改色，不改网格。用户2026-10-09明确要求“导入 UE、接入自动改色。”，对应[正式导入放行记录](formal-import-authorization.json)。')
    review=review.replace('它们的旧登记作为历史保留，本轮未更新UE模型表或正式资产。','它们的模型ID和原资源保留，改色开关已关闭，八条未使用CPD绑定及两条待制作区域已从源表移除；历史归档不改写。')
    review=review[:review.index('A：用户明确取消出图')]+'''A：用户明确取消出图，并直接确认现有橙色分区。B：用户已明确放行此版本正式导入和接线；最终引擎显示反馈另记。没有重新制作或导入网格，正式材质在原稳定路径增加UV0精确遮罩与CPD8／16，原三档明暗、刚性WPO及固定底色不变。正式网格顶点Alpha保持原样，运行时使用精确UV遮罩，不依赖候选面Alpha。

[UE实际四视图、LOD与运行画廊](UE/index.html)记录25张实际静态图和4张实际召唤批次图。[正式入库](formal-ue-import.json)、[DataTable回读](datatable-saved-readback.json)、[双方客户端实际参数](UE/runtime-local-team-readback.json)、[未知身份与Tick恢复](UE/runtime-tick-recovery.json)、[地图保存回读](UE/acceptance-scene-readback.json)均已完成。原网格、UV、法线及原三档LOD完全一致；本次不涉及原生代码或编译，不新增自动化测试框架。
'''
(OUT/'REVIEW.md').write_text(review,encoding='utf8')
print(json.dumps({'views':len(qa['checks']),'errors':qa['errors'],'checks':qa['checks']},ensure_ascii=False))
if qa['errors']:raise RuntimeError(qa['errors'])
