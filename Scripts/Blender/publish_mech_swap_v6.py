import json,hashlib,re,shutil
from pathlib import Path
ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919');OUT=ROOT/'Production_v6_Swap'
report=json.loads((OUT/'component_paint_report.json').read_text());assert report['success']
page=(ROOT/'Production_v5_Parts/Review_B_v5.html').read_text(encoding='utf-8')
page=page.replace('REVISION 05','REVISION 06').replace('Revision 05','Revision 06').replace('配色 v5','配色 v6').replace('修订 v5','修订 v6')
page=page.replace('暗红装甲 · 蓝色舱甲 · 白色腿甲','暗红装甲 · 白色舱甲 · 蓝色腿甲')
page=page.replace('轻型机甲的封舱甲改蓝、成对上腿装甲改白','轻型机甲按最新要求对调蓝白：封舱甲改白、成对上腿装甲改蓝')
page=page.replace('封舱装甲整件灰蓝，成对上腿装甲整组米白','封舱装甲整件米白，成对上腿装甲整组灰蓝')
page=page.replace('成对上腿装甲整件改白','成对上腿装甲整件灰蓝')
page=page.replace('Mechs_ComponentColors_v5.blend','Mechs_ComponentColors_v6.blend')
page=page.replace('Previews/SpiderMech','../Production_v5_Parts/Previews/SpiderMech')
for src in set(re.findall(r'(?:src|data-src)="([^"]+)"',page)):assert (OUT/src).is_file(),src
(OUT/'Review_B_v6.html').write_text(page,encoding='utf-8')
(OUT/'README.md').write_text('''# v6 · 轻型机甲蓝白对调

[最新实际效果与三视图](Review_B_v6.html) / [完整Blender文件](Mechs_ComponentColors_v6.blend)。

按用户最新“白色部分跟蓝色部分对调”，将v5轻型机甲的封舱装甲从灰蓝改为米白#E3E6E0，左右上腿装甲从米白改为灰蓝#587F9B。仍按完整部件分色；其余配色、线稿、三档明暗和几何保持。

SpiderMech继续使用v5的四片暗红上腿装甲盖，主体完整原网格839,778三角面；此前红色条纹已撤回。三套武器沿用v3线稿/三渲二。总览引用未改资产的实际旧版渲染，轻型五张视图本次重新渲染。

[部件与镜像记录](component_paint_report.json)、[保存回读](saved_readback.json)、[哈希清单](production_manifest.json)。整件分色依据见[v5制作说明](../Production_v5_Parts/README.md)，线稿/三渲二依据见[v3制作说明](../Production_v3_InkCel/README.md)。

当前Blender修订候选尚未同步UE，现有Ground资源仍为此前版本。用户配色修改指令不记为最终视觉审核通过。
''',encoding='utf-8')
feedback=OUT/'Feedback';feedback.mkdir(exist_ok=True)
shutil.copy2('C:/Users/a/AppData/Local/Temp/codex-clipboard-10406e49-da23-4447-89c2-b9598a3a897c.png',feedback/'LightMech_BlueWhite_SwapRequest.png')
manifest={'version':'6','source':'../Production_v5_Parts/Mechs_ComponentColors_v5.blend','change':'Swap light-mech whole-part blue and white','approval_B':'pending','ue_integration_this_revision':'not_run','files':{}}
for path in [OUT/'Mechs_ComponentColors_v6.blend',OUT/'component_paint_report.json',OUT/'saved_readback.json',OUT/'Review_B_v6.html',OUT/'README.md',*sorted((OUT/'Previews').glob('*.png')),*feedback.glob('*.png')]:
    manifest['files'][str(path.relative_to(OUT)).replace('\\','/')]={'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
(OUT/'production_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(str(OUT/'Review_B_v6.html'))
