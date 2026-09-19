import json, hashlib, re, shutil
from pathlib import Path

ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919')
OUT=ROOT/'Production_v8_HeadRefine'
report=json.loads((OUT/'refinement_report.json').read_text())
readback=json.loads((OUT/'saved_readback.json').read_text())
assert report['success'] and readback['success']
feedback=OUT/'Feedback';feedback.mkdir(exist_ok=True)
for source,dest in [
    ('C:/Users/a/AppData/Local/Temp/codex-clipboard-01af9f5c-1af1-4785-944f-c39902136332.png','LightMech_Head_Color_Curve_Reference.png'),
    ('C:/Users/a/AppData/Local/Temp/codex-clipboard-cd91110c-9044-4df8-8229-bbb5832c0272.png','LightMech_Shoulder_Vent_UserFeedback.png')]:
    shutil.copy2(source,feedback/dest)

def figure(src,label):
    return f'<figure><button class="zoom" data-src="{src}" data-label="{label}"><img src="{src}" alt="{label}" loading="lazy"></button><figcaption>{label}<span>点击放大 ↗</span></figcaption></figure>'

details='<section id="refinement"><div class="section-title"><span class="number">↗</span><div><h2>头部与散热结构 · 三处细化</h2><p>真实网格与装配：弧面封舱甲、成对内凹排气孔、肩甲内嵌散热舱。点击近景放大检查。</p></div></div><div class="compare">'
for image,label in [('Head_Close','赭金色弧面头甲'),('Vent_Front','有厚度的框体与内凹叶片'),('Shoulder_Close','肩部散热舱与叶片收口')]:
    details+=figure('Previews/Mech_Lightest_'+image+'.png',label)
details+='</div><details><summary>侧视弧度与用户参考</summary><div class="compare">'
details+=figure('Previews/Mech_Lightest_Head_Profile.png','实际侧视 · 头甲弧线')
details+=figure('Feedback/LightMech_Head_Color_Curve_Reference.png','用户指定 · 头部配色与结构参考')
details+=figure('Feedback/LightMech_Shoulder_Vent_UserFeedback.png','用户反馈 · 旧版侧面竖条')
details+='</div></details></section>'

page=(ROOT/'Production_v7_Black/Review_B_v7.html').read_text(encoding='utf-8')
page=page.replace('REVISION 07','REVISION 08').replace('Revision 07','Revision 08').replace('配色 v7','头部细化 v8').replace('修订 v7','修订 v8')
page=page.replace('对称头部细化 v8','头部与散热结构细化 v8')
page=page.replace('整件分色<br>暗红装甲 · 黑色舱甲 · 蓝色腿甲','轻型机甲细化<br>弧面头甲 · 内凹排气 · 肩部散热舱')
page=page.replace('按最新反馈撤回条纹，沿完整装甲部件分色。SpiderMech 的四片上腿装甲盖改暗红；轻型机甲按最新要求将白色封舱甲改为炭黑，成对上腿装甲保持灰蓝。五项线稿与三渲二保留。','头部恢复参考图的赭金色，用四片连续弧面装甲封舱；前方两个排气孔重做厚框、内壁和倾斜叶片，肩侧重做完整散热舱。保留蓝色腿甲、原装配、线稿与三渲二。')
page=page.replace('封舱装甲整件炭黑，成对上腿装甲整组灰蓝；赭黄主色、琥珀灯与原侧挂武器保留。','赭金色弧面封舱甲、成对灰蓝上腿甲；头部双排气孔及肩侧散热结构已重做。')
page=page.replace('<a href="#paint">背部修正</a>','<a href="#refinement">三处细化</a>')
page=re.sub(r'<section id="paint">.*?</section>',details,page,flags=re.S)
page=page.replace('Mechs_ComponentColors_v7.blend','Mechs_HeadRefined_v8.blend')
page=page.replace('新增颜色直接赋给完整源表面部件，不使用条纹或空间切割遮罩；对称部件逐对核对。','轻型头甲和两组散热结构按最新参考重做；弧度来自实际网格，前排气孔左右镜像。蓝色腿甲及Spider整件暗红配色保留。')
page=page.replace('<td>轻型机甲</td><td>4,824</td><td>1,285</td>',f'<td>轻型机甲</td><td>{readback["light_body_evaluated_triangles"]:,}</td><td>{readback["light_outline_evaluated_triangles"]:,}</td>')
for src in set(re.findall(r'(?:src|data-src)="([^"]+)"',page)):
    assert (OUT/src).is_file(),src
(OUT/'Review_B_v8.html').write_text(page,encoding='utf-8')
(OUT/'README.md').write_text(f'''# v8 · 轻型机甲头部与散热结构细化

[实际效果、三视图与局部近景](Review_B_v8.html) / [可编辑 Blender](Mechs_HeadRefined_v8.blend)。

用户指定参考图要求头部恢复原赭金色并做出弧度，随后指出前部排气孔像梯子、肩侧竖条不合规。本版据此直接修改实际几何：

- 封舱装甲恢复 #B48A3F，以四片弧面装甲组成连续外形；纵向弧线、横向拱度与真实倒角保留，底层连续封板覆盖接缝，无驾驶员或开放舱。
- 头部左右排气孔使用镜像厚框、斜内壁、封闭底壁，每边六片倾斜叶片；框口到内壁约7.9cm深。
- 肩侧散热舱完整落在原肩甲侧面范围，金色外框、凹入内壁、七片收口叶片及四颗固定件；内凹层次约3.48cm。原有外挑竖条删除，源UV中破碎的侧甲边线改为新框体与轮廓表达。

左右上腿灰蓝、功能灯和原单侧武器关系保留。新件沿用原Mount_top及肩部装配约束；原腿、骨架、武器和Spider网格未改。几何变化仅为轻型头甲和指定散热细节，不声称本版仍为4,824面。

当前评估后轻型主体 **{readback['light_body_evaluated_triangles']:,} 三角面**，独立轮廓 **{readback['light_outline_evaluated_triangles']:,} 三角面**。Spider仍839,778面主体，另有839,778面轮廓。未进行游戏性能评估。

[建模记录](refinement_report.json)、[保存回读](saved_readback.json)、[哈希清单](production_manifest.json)。新增轻型效果、三视图及四张局部视图共8张；其他四项沿用当前有效渲染。反馈原图保存在Feedback目录。

当前为Blender细化候选，尚未同步UE，不把用户修改指令记录为成品视觉通过。上一版[黑色头甲v7](../Production_v7_Black/README.md)保留历史。
''',encoding='utf-8')
manifest={'version':'8','source':'../Production_v7_Black/Mechs_ComponentColors_v7.blend','change':'Reference ochre curved closed head armor, paired recessed exhausts, integrated shoulder cooling cassette','approval_B':'pending','ue_integration_this_revision':'not_run','files':{}}
for path in [OUT/'Mechs_HeadRefined_v8.blend',OUT/'refinement_report.json',OUT/'saved_readback.json',OUT/'Review_B_v8.html',OUT/'README.md',*sorted((OUT/'Previews').glob('*.png')),*sorted(feedback.glob('*.png'))]:
    manifest['files'][path.relative_to(OUT).as_posix()]={'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
(OUT/'production_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(OUT/'Review_B_v8.html')
