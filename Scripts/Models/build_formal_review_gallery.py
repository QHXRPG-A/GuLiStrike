"""Publish actual UE captures and a reproducible before/after comparison, no repainting."""
import html
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/LocalTeamColorUE_B_v2_20261008'
ART = ROOT / 'Artifacts/ModelRegistryRuntime20261008'
OUT.mkdir(parents=True, exist_ok=True)
models = {r['Id']: r for r in json.loads((ROOT / 'Data/Json/DT_GuLiStrikeModels_Models.json').read_text(encoding='utf8'))}
captures = json.loads((ART / 'formal-render-captures.json').read_text(encoding='utf8'))
groups = {}
for r in captures['renders']:
    groups.setdefault(r['model_id'], []).append(r)
views = {'hero': '效果图', 'front': '正视', 'left': '左视', 'back': '后视'}
content = []
missing = []
ore_images = ART / 'Images/OreRockReview'
if (ore_images / 'blue_after.png').is_file():
    content.append('<section><h2>矿石黑块 · 同机位提亮对照</h2><p>实际 UE 原网格。岩壳使用固定深灰色卡补光；晶体暗面使用原色卡深蓝 / 深红补光，原顶点底色、亮面与形状保留。</p>')
    for side, title in [('blue','蓝矿'),('red','红矿')]:
        content.append('<h3>'+title+'</h3><div class="compare">')
        for revision, label in [('before','调整前'),('after','调整后')]:
            p = ore_images / (side+'_'+revision+'.png')
            if not p.is_file(): missing.append(str(p))
            rel = '../../'+p.relative_to(ROOT).as_posix()
            content.append('<a href="'+rel+'" target="_blank"><img src="'+rel+'"><span>'+label+'</span></a>')
        content.append('</div>')
    content.append('</section>')
for mid, renders in groups.items():
    m = models[mid]
    content.append('<section><h2>' + html.escape(m['DisplayName']) + ' · ModelId ' + str(mid) + '</h2><p>' + html.escape(m['Description']) + '</p>')
    for palette, title in [('blue', '己方蓝色'), ('red', '敌方红色')]:
        content.append('<h3>' + title + '</h3><div class="grid">')
        for view in views:
            r = next(x for x in renders if x['palette'] == palette and x['view'] == view)
            p = Path(r['file'])
            if not p.is_file(): missing.append(str(p))
            rel = '../../' + p.relative_to(ROOT).as_posix()
            content.append('<a href="' + rel + '" target="_blank"><img loading="lazy" src="' + rel + '"><span>' + views[view] + '</span></a>')
        content.append('</div>')
    before = ART / 'Images/BeforeShadowAdjustment' / (m['Name'] + '_blue_hero.png')
    if before.is_file():
        content.append('<details><summary>同机位：调整前 / 调整后</summary><div class="compare">')
        for title, p in [('调整前', before), ('调整后', ART / 'Images/FormalModels' / (m['Name'] + '_blue_hero.png'))]:
            rel = '../../' + p.relative_to(ROOT).as_posix()
            content.append('<a href="' + rel + '" target="_blank"><img src="' + rel + '"><span>' + title + '</span></a>')
        content.append('</div></details>')
    content.append('</section>')
page = '''<!doctype html><html lang="zh-CN"><meta charset="UTF-8"><title>UE 正式资源 · 暗部提亮</title>
<style>body{margin:0;background:#f7f2e7;color:#2c3735;font:16px/1.6 system-ui}main{max-width:1500px;margin:auto;padding:36px}h1{font-size:30px}section{margin:32px 0;padding:24px;background:white;border-radius:12px}.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}.compare{display:grid;grid-template-columns:1fr 1fr;gap:16px}a{color:inherit;text-decoration:none}img{width:100%;display:block;border-radius:6px}span{display:block;text-align:center;padding:8px}details{margin-top:24px}@media(max-width:800px){.grid{grid-template-columns:1fr 1fr}}code{background:#e5e1d6;padding:3px 6px}</style>
<main><h1>UE 正式资源 · 暗部提亮 v1</h1><p>实际 UE 渲染，15 个模型入口（14 个模型及共用彼之矛施工体），共 120 张蓝红四视图。点击图片放大；每个模型可展开同机位对比。</p>
<p>原固定色卡与网格保持。阴影 / 中间色 / 亮部约为 0.62 / 0.82 / 1.0，深色表面增加少量固定补光；内部描线采用原色卡深灰并降低遮罩强度。外轮廓、三档明暗、动画与 LOD 保留。</p>
<p>本次修订依据用户“这些黑块太深了，再浅一些”。此前正式导入依据“记得将刚刚开发的美术资源导入至UE的正式资源中，并将旧资源删除”。助手运行检查与用户最终美术验收分别记录。</p>
'''+''.join(content)+'</main></html>'
(OUT / 'index.html').write_text(page, encoding='utf8')
report = {'models': len(groups), 'renders': len(captures['renders']), 'missing': missing,
          'ore_comparison_images': 4 if (ore_images / 'blue_after.png').is_file() else 0,
          'capture_source': captures['source'], 'browser_interaction': 'not verified; local file restrictions not bypassed',
          'formal_storage_authorized': True, 'lighter_revision_requested': True, 'user_visual_acceptance': 'pending',
          'source_images_modified': False, 'success': len(groups) == 15 and len(captures['renders']) == 120 and not missing}
(OUT / 'delivery-readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
print(json.dumps(report, ensure_ascii=False))
