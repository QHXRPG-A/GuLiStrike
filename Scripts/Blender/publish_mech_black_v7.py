import json, hashlib, re
from pathlib import Path

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919')
OUT = ROOT / 'Production_v7_Black'
report = json.loads((OUT / 'component_paint_report.json').read_text())
assert report['success']
page = (ROOT / 'Production_v6_Swap/Review_B_v6.html').read_text(encoding='utf-8')
page = page.replace('REVISION 06', 'REVISION 07').replace('Revision 06', 'Revision 07').replace('配色 v6', '配色 v7').replace('修订 v6', '修订 v7')
page = page.replace('暗红装甲 · 白色舱甲 · 蓝色腿甲', '暗红装甲 · 黑色舱甲 · 蓝色腿甲')
page = page.replace('轻型机甲按最新要求对调蓝白：封舱甲改白、成对上腿装甲改蓝', '轻型机甲按最新要求将白色封舱甲改为炭黑，成对上腿装甲保持灰蓝')
page = page.replace('封舱装甲整件米白', '封舱装甲整件炭黑')
page = page.replace('Mechs_ComponentColors_v6.blend', 'Mechs_ComponentColors_v7.blend')
for src in set(re.findall(r'(?:src|data-src)="([^"]+)"', page)):
    assert (OUT / src).is_file(), src
(OUT / 'Review_B_v7.html').write_text(page, encoding='utf-8')
(OUT / 'README.md').write_text('''# v7 · 轻型机甲白色封舱甲改黑

[最新实际效果与三视图](Review_B_v7.html) / [完整 Blender 文件](Mechs_ComponentColors_v7.blend)。

用户评价 v6 配色不满意，并要求“白色改成黑色看看”。本次仅将完整封舱装甲从米白 #E3E6E0 改为炭黑 #24282C；左右上腿装甲保持灰蓝 #587F9B。保持整件分色、对称涂装、线稿和三档明暗。

轻型机甲五张实际 Blender 渲染已更新。SpiderMech 继续使用 v5 的四片暗红上腿装甲盖，保留 839,778 面完整源网格；三套武器沿用 v3 线稿/三渲二渲染。当前修订没有修改其他部件配色、模型几何或装配。

[部件记录](component_paint_report.json)、[保存回读](saved_readback.json)、[哈希清单](production_manifest.json)。历史：[蓝白对调 v6](../Production_v6_Swap/README.md)、[整件分色 v5](../Production_v5_Parts/README.md)、[线稿/三渲二 v3](../Production_v3_InkCel/README.md)。

当前为 Blender 配色候选，尚未同步 UE；本次修改指令不记为最终视觉通过。
''', encoding='utf-8')
manifest = {'version':'7', 'source':'../Production_v6_Swap/Mechs_ComponentColors_v6.blend', 'change':'Light mech whole sealed armor white to charcoal black; paired blue upper-leg armor retained', 'approval_B':'pending', 'ue_integration_this_revision':'not_run', 'files':{}}
paths = [OUT / 'Mechs_ComponentColors_v7.blend', OUT / 'component_paint_report.json', OUT / 'saved_readback.json', OUT / 'Review_B_v7.html', OUT / 'README.md', *sorted((OUT / 'Previews').glob('*.png'))]
for path in paths:
    manifest['files'][path.relative_to(OUT).as_posix()] = {'bytes':path.stat().st_size, 'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
(OUT / 'production_manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
print(OUT / 'Review_B_v7.html')
