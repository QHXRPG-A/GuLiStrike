import json,hashlib,shutil,re
from pathlib import Path
ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919');OUT=ROOT/'Production_v5_Parts'
report=json.loads((OUT/'component_paint_report.json').read_text());assert report['success']
page=(ROOT/'Production_v4_Accents/Review_B_v4.html').read_text(encoding='utf-8')
page=page.replace('REVISION 04','REVISION 05').replace('Revision 04','Revision 05').replace('配色 v4','配色 v5').replace('修订 v4','修订 v5')
page=page.replace('<h1>对称配色修订<br>砖红 · 灰蓝 · 米白</h1>','<h1>整件分色<br>暗红装甲 · 蓝色舱甲 · 白色腿甲</h1>')
page=page.replace('SpiderMech 增加对称砖红色块；轻型机甲增加对称灰蓝、米白。保留五项已经补齐的线稿与三档明暗，以下全部为实际 Blender 模型渲染。','按最新反馈撤回条纹，沿完整装甲部件分色。SpiderMech 的四片上腿装甲盖改暗红；轻型机甲的封舱甲改蓝、成对上腿装甲改白。五项线稿与三渲二保留。')
page=page.replace('胸甲双条、背部双面板和腿甲加入对称蓝白；保留赭黄主色、封闭装甲与原侧挂武器。','封舱装甲整件灰蓝，成对上腿装甲整组米白；赭黄主色、琥珀灯与原侧挂武器保留。')
page=page.replace('完整原网格；成对腿甲、前甲和中线尾甲加入低饱和砖红色块。','四片上腿装甲盖整件暗红，内嵌小面板和机械结构保持原色；原网格不减面。')
page=page.replace('背部取色已对齐，并追加蓝白涂装。纯色视图去除光照分档，便于检查左右配色。','背部取色已对齐，成对上腿装甲整件改白。纯色视图便于检查部件边界和对称性。')
page=page.replace('Mechs_SymmetricAccents_v4.blend','Mechs_ComponentColors_v5.blend')
page=page.replace('新增色块沿模型中线对称，保存为随蒙皮运动的静置坐标遮罩。','新增颜色直接赋给完整源表面部件，不使用条纹或空间切割遮罩；对称部件逐对核对。')
for src in set(re.findall(r'(?:src|data-src)="([^"]+)"',page)):assert (OUT/src).is_file(),src
(OUT/'Review_B_v5.html').write_text(page,encoding='utf-8')
feedback=OUT/'Feedback';feedback.mkdir(exist_ok=True)
shutil.copy2('C:/Users/a/AppData/Local/Temp/codex-clipboard-b8f9ac33-6bc4-43b0-863e-1ae90bc7d397.png',feedback/'Spider_Stripes_Rejected.png')
readme='''# v5 · 完整部件分色

[实际效果与三视图](Review_B_v5.html) / [Blender 源文件](Mechs_ComponentColors_v5.blend)。

用户否定 v4 红色条纹，要求把某几个部件整体染红；并明确轻型机甲的蓝白也要整件分色。因此从保留线稿/三渲二的 v3 源文件重新配色，撤回两台机甲的全部 v4 条纹、识别带和空间切割遮罩。

- **SpiderMech**：四片成对上腿装甲盖整体暗红（#A6534C），内嵌小面板、下部机械结构、主体和尾部保留原色。按完整原始表面连通部件取色，左右对应装甲使用同色。
- **轻型机甲**：封闭驾驶区的装甲盖整件灰蓝（#587F9B）；左右上腿装甲外壳整组米白（#E3E6E0）。保留其余赭黄/深青绿主色、灯和原侧挂武器。
- **其他三套武器**：沿用 v3 结构内线、独立轮廓和三档明暗，预览页引用对应十二张真实渲染。

颜色在完整连通部件间切换；未改变顶点、拓扑、UV、原权重和法线。Spider主体仍为839,778三角面，轮廓壳另计。封舱甲为中线对称部件；四片Spider装甲的源表面以及轻型腿甲均逐对核对镜像包围盒，记录于[部件报告](component_paint_report.json)。

[保存回读](saved_readback.json)、[哈希清单](production_manifest.json)。8张效果和三视图、2张背视纯色证据全部来自真实Blender渲染，位于Previews。原v4保留为被用户要求修改的历史候选，不能记录为通过。

当前新版尚未同步UE；现有Ground仍是此前已接入版本。保持既有骨架与挂点，并不等于本轮新增动画、碰撞或性能验收。制作脚本：项目Scripts/Blender/paint_mech_components_v5.py。
'''
(OUT/'README.md').write_text(readme,encoding='utf-8')
manifest={'version':'5','source':'../Production_v3_InkCel/Mechs_InkCel_v3.blend','replaces':'v4 stripe/accent palette rejected by user','approval_B':'pending','ue_integration_this_revision':'not_run','files':{}}
for path in [OUT/'Mechs_ComponentColors_v5.blend',OUT/'component_paint_report.json',OUT/'saved_readback.json',OUT/'Review_B_v5.html',OUT/'README.md',*sorted((OUT/'Previews').glob('*.png')),*feedback.glob('*.png')]:
    manifest['files'][str(path.relative_to(OUT)).replace('\\','/')]={'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
(OUT/'production_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(str(OUT/'Review_B_v5.html'))
