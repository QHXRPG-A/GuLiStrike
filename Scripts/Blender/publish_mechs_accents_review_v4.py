"""Point the five-asset review at the final symmetric palette revision."""
import json,hashlib,re
from pathlib import Path
ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919');OUT=ROOT/'Production_v4_Accents';V3=ROOT/'Production_v3_InkCel'
report=json.loads((OUT/'accent_report.json').read_text());assert report['success']
page=(V3/'Review_B_v3.html').read_text(encoding='utf-8')
page=page.replace('REVISION 03','REVISION 04').replace('Revision 03','Revision 04').replace('三渲二 v3','三渲二 / 对称配色 v4').replace('修订 v3','修订 v4')
page=page.replace('<h1>机甲与武器<br>线稿 · 三渲二</h1>','<h1>对称配色修订<br>砖红 · 灰蓝 · 米白</h1>')
page=page.replace('修正轻型机甲的背部涂装；为指定五项补齐深色轮廓、主要结构线与亮／中／暗三档明暗。以下全部为实际 Blender 模型渲染。','SpiderMech 增加对称砖红色块；轻型机甲增加对称灰蓝、米白。保留五项已经补齐的线稿与三档明暗，以下全部为实际 Blender 模型渲染。')
page=page.replace('对齐背部双侧面板、腿部条带；封闭装甲和原侧挂武器保留。','胸甲双条、背部双面板和腿甲加入对称蓝白；保留赭黄主色、封闭装甲与原侧挂武器。')
page=page.replace('完整原网格；蓝灰装甲、赭黄机构与暗色骨架。','完整原网格；成对腿甲、前甲和中线尾甲加入低饱和砖红色块。')
page=page.replace('背部通风口底色与腿部条带统一。纯色视图去除光照分档，便于区分涂装和受光差异。','背部取色已对齐，并追加蓝白涂装。纯色视图去除光照分档，便于检查左右配色。')
page=page.replace('Mechs_InkCel_v3.blend','Mechs_SymmetricAccents_v4.blend')
page=page.replace('Feedback/LightMech_Rear_UserFeedback.png','../Production_v3_InkCel/Feedback/LightMech_Rear_UserFeedback.png')
for key in ('FireWeapon_01','MissileWeapon_01','Machinegun_lvl1'):
    page=page.replace('Previews/'+key,'../Production_v3_InkCel/Previews/'+key)
page=page.replace('Previews/SpiderMech_Close.png','Previews/SpiderMech_Rear_Paint.png').replace('检查原网格近景','检查对称配色').replace('SpiderMech · 原网格近景','SpiderMech · 背视纯色核对')
page=page.replace('内部线稿使用独立遮罩，外轮廓为无投影的独立壳。','新增色块沿模型中线对称，保存为随蒙皮运动的静置坐标遮罩。内部线稿使用独立遮罩，外轮廓为无投影的独立壳。')
for src in set(re.findall(r'(?:src|data-src)="([^"]+)"',page)):
    assert (OUT/src).is_file(),src
(OUT/'Review_B_v4.html').write_text(page,encoding='utf-8')
readme='''# 机甲配色 v4：Spider 砖红 / 轻型蓝白

[最新实际效果与三视图](Review_B_v4.html)、[Blender 源文件](Mechs_SymmetricAccents_v4.blend)。

用户在五项线稿/三渲二修订过程中追加：“SpiderMech 加点红色块，注意对称；轻型机甲 加点蓝白，注意对称”。此文件包含完整批次模型，仅这两项新增配色；三套武器继续使用 v3 线稿/三档材质。

- SpiderMech：四片成对腿甲加入横向砖红条块，前甲两侧加入对称识别条，尾甲沿中线补充红色。蓝灰和赭黄仍为主体，红色 sRGB #B5544D。
- 轻型机甲：胸甲双条、背部双面板和左右腿甲加入灰蓝 #587F9B 与米白 #E3E6E0。保留赭黄/深青绿主色和原单侧武器装配，功能灯区域保持琥珀发光。
- 新增图案用静置局部坐标的 |X| 构造，左右使用同一组边界参数；不存在左右分别手绘而错位的问题。静置坐标保存为顶点属性 StyleRestPosition，遮罩随蒙皮移动，不使用世界投影。
- 色块在材质内连续切分，不按三角面分色；原几何、UV、权重、法线、骨架和线稿均保留。Spider 主体仍 839,778 三角面，不减面。
- [v3 线稿和三档制作说明](../Production_v3_InkCel/README.md)及其独立轮廓壳成本继续适用。当前材质是 Blender 固定艺术光方向候选，尚未同步 UE；未来导出需烘焙新增配色并重建等效材质，不能直接把普通 FBX 导入当作风格交付。

八张更新的效果/正/侧/背渲染和两张背视纯色证据位于 Previews；三套武器的十二张 v3 图由审核页直接引用。[色块制作记录](accent_report.json)、[保存回读](saved_readback.json)、[哈希清单](production_manifest.json)。原 v1/v2/v3 保留，未将最新修改请求记录为视觉审核通过。
'''
(OUT/'README.md').write_text(readme,encoding='utf-8')
manifest={'version':'4','source_version':'../Production_v3_InkCel/Mechs_InkCel_v3.blend','approval_B':'pending','ue_integration_this_revision':'not_run','files':{}}
for path in [OUT/'Mechs_SymmetricAccents_v4.blend',OUT/'accent_report.json',OUT/'saved_readback.json',OUT/'Review_B_v4.html',OUT/'README.md',*sorted((OUT/'Previews').glob('*.png'))]:
    manifest['files'][str(path.relative_to(OUT)).replace('\\','/')]={'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
(OUT/'production_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(str(OUT/'Review_B_v4.html'))
