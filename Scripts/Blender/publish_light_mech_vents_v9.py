"""Publish actual Blender renders and the saved mesh contact evidence."""
import json, hashlib, re, shutil
from pathlib import Path

ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919')
OUT=ROOT/'Production_v9_VentMount'
report=json.loads((OUT/'mount_report.json').read_text())
readback=json.loads((OUT/'saved_readback.json').read_text())
assert report['success'] and readback['success']
feedback=OUT/'Feedback';feedback.mkdir(exist_ok=True)
for source in (ROOT/'Production_v8_HeadRefine/Feedback').glob('*.png'):
    shutil.copy2(source,feedback/source.name)
shutil.copy2('C:/Users/a/AppData/Local/Temp/codex-clipboard-bbd2169a-17b2-412d-af36-cce8a4fff76a.png',feedback/'LightMech_Floating_Exhaust_UserFeedback.png')

def figure(src,label):
    return f'<figure><button class="zoom" data-src="{src}" data-label="{label}"><img src="{src}" alt="{label}" loading="lazy"></button><figcaption>{label}<span>点击放大 ↗</span></figcaption></figure>'

details='<section id="vent_mount"><div class="section-title"><span class="number">↗</span><div><h2>排气口与机身接实</h2><p>背上方同角度对比。两侧框体向内收并降低，补齐沿原机身表面贴合的封闭底座。</p></div></div><div class="compare pair">'
details+=figure('Previews/Mech_Lightest_BackTop.png','修复后 · 连续底座接入原壳体')
details+=figure('Previews/Mech_Lightest_BackTop_Before.png','修复前 · 框体背部透空')
details+='</div><details><summary>放大检查两侧连接与用户反馈</summary><div class="compare">'
for src,label in [('Previews/Mech_Lightest_Mount_R_Before.png','同角度近景 · 修复前'),('Previews/Mech_Lightest_Mount_R.png','同角度近景 · 修复后'),('Previews/Mech_Lightest_Mount_L.png','另一侧 · 对称底座'),('Previews/Mech_Lightest_Vent_Front.png','正面 · 内凹叶片与对称框口'),('Feedback/LightMech_Floating_Exhaust_UserFeedback.png','用户标注 · 原浮空位置')]:
    details+=figure(src,label)
details+='</div></details></section>'
page=(ROOT/'Production_v8_HeadRefine/Review_B_v8.html').read_text(encoding='utf-8')
page=page.replace('REVISION 08','REVISION 09').replace('Revision 08','Revision 09').replace('头部与散热结构细化 v8','排气口贴合修复 v9')
page=page.replace('轻型机甲细化<br>弧面头甲 · 内凹排气 · 肩部散热舱','排气口贴合修复<br>对称内收 · 连续贴壳底座')
page=page.replace('头部恢复参考图的赭金色，用四片连续弧面装甲封舱；前方两个排气孔重做厚框、内壁和倾斜叶片，肩侧重做完整散热舱。保留蓝色腿甲、原装配、线稿与三渲二。','修正头部两侧排气口浮空：缩窄并内收框体，在背部补齐贴合机身的风道外壳。保留赭金弧面头甲、灰蓝腿甲、内凹叶片和三渲二。')
page=page.replace('<nav class="nav">','<nav class="nav"><a href="#vent_mount">浮空修复对比</a>')
page=page.replace('<section id="refinement">',details+'<section id="refinement">',1)
page=page.replace('</style>','.compare.pair{grid-template-columns:repeat(2,minmax(0,1fr))}.compare.pair img{height:auto}@media(max-width:850px){.compare.pair{grid-template-columns:1fr}}</style>')
page=page.replace('Mechs_HeadRefined_v8.blend','Mechs_VentMount_v9.blend')
page=page.replace('头部双排气孔及肩侧散热结构已重做。','双排气孔已补齐贴壳底座，保持对称；肩侧散热舱沿用。')
page=page.replace('轻型头甲和两组散热结构按最新参考重做；弧度来自实际网格，前排气孔左右镜像。','本版修复双排气孔与主体之间的悬空：框体内收、收窄并后移，新增闭合贴壳底座。保存后两侧周界各115处采样均接合，镜像误差0；原Mount_top装配保持。')
page=page.replace('<td>轻型机甲</td><td>17,286</td><td>8,573</td>',f'<td>轻型机甲</td><td>{readback["light_body_evaluated_triangles"]:,}</td><td>{readback["light_outline_evaluated_triangles"]:,}</td>')
for src in set(re.findall(r'(?:src|data-src)="([^"]+)"',page)):
    assert (OUT/src).is_file(),src
(OUT/'Review_B_v9.html').write_text(page,encoding='utf-8')
(OUT/'README.md').write_text(f'''# v9 · 轻型机甲双排气口贴合修复

[同角度前后对比与实际三视图](Review_B_v9.html#vent_mount) / [可编辑 Blender](Mechs_VentMount_v9.blend)。

用户指出头部两侧排气口浮空。检查确认v8框体背部没有连接原壳体，外缘部分超出安装面；上一版的闭合与镜像检查没有覆盖部件之间的接触。

本版将成对排气口中心由±53.5cm内收到±47cm，宽度缩为原来的75%，沿安装法线后移4cm。新增赭金色完整风道底座，后沿依据实际Cockpit_Jet表面投射并埋入约1.8cm，前沿嵌入原框体后壁；两侧完整镜像。六片内凹叶片、Mount_top装配、头甲弧度与配色保持。

保存后独立回读：两侧各115个后沿点均接合，最小嵌入1.794cm；框体、叶片和底座镜像误差0。16个调整/新增主体网格闭合，无非流形边或退化面，世界变换与原壳体一致。其他轻型网格和其余四项主体几何哈希一致。此次未改变Spider网格、原骨骼或UE资产。

轻型评估后主体 **{readback['light_body_evaluated_triangles']:,} 三角面**，独立轮廓 **{readback['light_outline_evaluated_triangles']:,} 三角面**。保留三档明暗、线稿和功能发光。

交付11张当前实际渲染、2张同角度修复前渲染；反馈原图在Feedback。原编辑会话保存在Source，未覆盖v8。

[建模记录](mount_report.json)、[保存回读](saved_readback.json)、[哈希清单](production_manifest.json)。Blender已打开本版，尚未同步UE，未进行本版动画、碰撞或游戏性能验证；用户视觉审核状态仍单独记录。上一版见[v8](../Production_v8_HeadRefine/README.md)。
''',encoding='utf-8')
manifest={'version':'9','source':'Source/SessionBeforeVentFix.blend','change':'Seat paired front exhaust frames; mirrored continuous housings conform to the original cockpit shell','approval_B':'pending','ue_integration_this_revision':'not_run','files':{}}
for path in [OUT/'Mechs_VentMount_v9.blend',OUT/'mount_report.json',OUT/'saved_readback.json',OUT/'Review_B_v9.html',OUT/'README.md',*sorted((OUT/'Previews').glob('*.png')),*sorted(feedback.glob('*.png'))]:
    manifest['files'][path.relative_to(OUT).as_posix()]={'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
(OUT/'production_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(OUT/'Review_B_v9.html')
