"""Package reference-A images and frozen originals; does not modify model assets."""
from pathlib import Path
import hashlib
import json
import struct

PROJECT = Path(__file__).resolve().parents[2]
ROOT = PROJECT / 'ArtSource/Ships/ShipComponentStyle_20260917/Batch02'
ASSETS = [
    ('Autocannon', '自动炮', 'v2', '赭黄', '三管呈三角排列，护罩与侧甲使用完整赭黄色块。'),
    ('Triple_Barrel_Turret', '三联炮', 'v1', '赭黄', '三门炮并列，赭黄集中在现有上盖与侧板。'),
    ('Single_Barrel_Turret', '单管炮', 'v2', '青蓝', '长方形炮口与长炮轨保留，青蓝标识炮身护罩和侧甲。'),
]
VIEW_LABELS = {'hero': '效果角度', 'front': '正视', 'right': '右侧', 'rear': '背视', 'top': '俯视补充'}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def artifact(path):
    path = Path(path)
    data = {'path': path.relative_to(ROOT).as_posix(), 'sha256': digest(path), 'bytes': path.stat().st_size}
    if path.suffix == '.png':
        with path.open('rb') as f:
            header = f.read(24)
        if header[:8] != b'\x89PNG\r\n\x1a\n':
            raise ValueError(f'Invalid PNG: {path}')
        data['resolution'] = list(struct.unpack('>II', header[16:24]))
    return data


def main():
    source_path = ROOT / 'Source/source_snapshot_v1.json'
    source = json.loads(source_path.read_text(encoding='utf-8'))
    prototype_path = ROOT / 'Source/prototype_render_manifest_v1.json'
    prototype = json.loads(prototype_path.read_text(encoding='utf-8'))
    assert source['success'] and source['source_packages_unchanged']
    manifest = {
        'schema': 'gulistrike-art-reference-review/v1',
        'work_id': 'WORK-20260917-004', 'date': '2026-09-17', 'art_revision': '1.0',
        'phase': 'reference_A', 'review_bundle': 'v1',
        'user_instruction': '根据模型和项目美术风格产出效果图+三视图审核，再根据图纸和原模型调整贴图和框线并审核。',
        'A': {'status': 'pending', 'user_decision': None},
        'B': {'status': 'not_started', 'user_decision': None},
        'production_textures_and_outlines': 'not_started',
        'geometry_policy': 'Original rigged mesh is authoritative; reference images authorize surface treatment only.',
        'image_generation_mode': 'built-in image_gen',
        'source_capture': artifact(source_path),
        'original_view_manifest': artifact(prototype_path),
        'source_packages_unchanged': True,
        'UE_formal_import_and_runtime_validation': 'not_run',
        'prompt_sets': [artifact(ROOT / 'References' / n) for n in ['prompts_v1.json', 'prompts_v2.json', 'prompts_v2_autocannon_retry.json']],
        'parts': {},
    }
    sections = []
    for index, (key, label, version, accent, intent) in enumerate(ASSETS, 1):
        frozen = prototype['parts'][key]
        authoring = ROOT / frozen['authoring_source']
        if digest(authoring) != frozen['authoring_sha256']:
            raise RuntimeError(f'Frozen original changed: {key}')
        sheet = artifact(ROOT / f'References/{key}_Reference_{version}.png')
        original_views = {}
        for view, ref in frozen['views'].items():
            actual = artifact(ROOT / ref['path'])
            if actual['sha256'] != ref['sha256']:
                raise RuntimeError(f'Original view changed: {key}/{view}')
            original_views[view] = actual
        part = source['parts'][key]
        manifest['parts'][key] = {
            'label': label, 'reference_version': version, 'A': 'pending', 'B': 'not_started',
            'reference_sheet': sheet, 'intent': intent, 'accent': accent,
            'original_authoring': artifact(authoring),
            'original_rig_fbx': artifact(ROOT / f'Source/ExistingAuthoring/SKM_SC_{key}.fbx'),
            'original_static_fbx': artifact(ROOT / f'Source/FBX_static_v1/SM_SC_{key}.fbx'),
            'original_geometry_snapshot': artifact(ROOT / f'Source/{key}_original_geometry.json'),
            'original_views': original_views,
            'original_mesh_dimensions_m': frozen['dimensions_m'],
            'original_vertices': frozen['vertices'], 'original_triangles': frozen['triangles'],
            'bones': frozen['bones'], 'blueprint': part['blueprint'], 'visual_mesh': part['visual_mesh'],
            'sockets': part['sockets'], 'part_relative_transform': part['part_relative_transform'],
            'assistant_visual_review': 'Counts, visible principal structures, major color regions and view coverage checked. Not user approval.',
        }
        figures = ''.join(
            f'<figure><a href="{r["path"]}" class="zoom" data-label="{label}原模型 · {VIEW_LABELS[v]}">'
            f'<img src="{r["path"]}" alt="{label}原模型{VIEW_LABELS[v]}" loading="lazy"></a>'
            f'<figcaption>{VIEW_LABELS[v]}</figcaption></figure>'
            for v, r in original_views.items())
        sections.append(f'''<section class="asset" id="{key}">
<header class="asset-head"><div><p class="eyebrow">COMPONENT {index:02}</p><h2>{label}<small>{version}</small></h2></div><span class="chip">{accent} · 待审核 A</span></header>
<p class="intent">{intent}</p>
<a class="sheet zoom" href="{sheet['path']}" data-label="{label}参考设计 {version}"><img src="{sheet['path']}" alt="{label}{version}效果图及正、右侧、背三视图"></a>
<p class="caption">效果图 + 正 / 右侧 / 背三视图 · 点击图片放大</p>
<details><summary>展开原模型对照</summary><div class="originals">{figures}</div><p class="muted">中性视图直接渲染自冻结的原骨骼模型。形体和机械接口以该源模型为准。</p></details>
</section>''')
    manifest_path = ROOT / 'References/reference_A_manifest_v1.json'
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    nav = ''.join(f'<a href="#{key}">{name} <span>{version}</span></a>' for key, name, version, _, _ in ASSETS)
    page = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Ship 第二批 · 参考审核 A</title><style>
:root{color-scheme:light;--ink:#213d50;--navy:#23384a;--muted:#667c89;--paper:#f3f6f6;--line:#d1dde2}
*{box-sizing:border-box}html{scroll-behavior:smooth;scroll-padding-top:82px}body{margin:0;background:var(--paper);color:var(--ink);font:16px/1.65 'Segoe UI','Microsoft YaHei',sans-serif}a{color:inherit}main{max-width:1260px;margin:auto;padding:0 28px 56px}header.intro{padding:42px 0 22px}.eyebrow{font-size:11px;letter-spacing:.17em;font-weight:750;margin:0 0 6px;color:var(--muted)}h1{font-size:36px;letter-spacing:-.03em;line-height:1.25;margin:0 0 12px}h2{font-size:27px;line-height:1.3;margin:0}h2 small{font-size:14px;color:var(--muted);margin-left:12px;font-weight:500}p{margin:8px 0}.lead{max-width:840px;color:#536c7b}.nav{position:sticky;top:0;z-index:2;border-block:1px solid var(--line);background:#f3f6f6f5;display:flex;gap:9px;padding:10px 0;backdrop-filter:blur(12px)}.nav a{text-decoration:none;border:1px solid var(--line);border-radius:6px;padding:6px 15px;font-size:14px;background:white}.nav a:hover{background:var(--navy);color:white}.nav span{opacity:.65;font-size:11px;margin-left:6px}.asset{padding-top:32px}.asset-head{display:flex;justify-content:space-between;align-items:center;gap:20px}.chip{font-size:12px;padding:5px 12px;border:1px solid #a4b9c4;border-radius:100px;white-space:nowrap}.intent{font-size:14px;color:#526c7c;margin:10px 0 18px}.sheet{display:block;border-radius:8px;overflow:hidden;border:1px solid #9daeb7;cursor:zoom-in;background:#b2c6d0}.sheet img{width:100%;display:block}.caption{font-size:12px;color:var(--muted);text-align:right}details{border-bottom:1px solid var(--line);margin:12px 0 4px;padding:12px 0 18px}summary{font-size:14px;cursor:pointer;font-weight:650}.originals{display:grid;grid-template-columns:repeat(3,1fr);gap:14px;margin-top:18px}figure{margin:0}figure img{width:100%;display:block;border-radius:5px}.zoom{cursor:zoom-in}figcaption{font-size:12px;color:var(--muted);margin:6px 0}.muted{font-size:12px;color:var(--muted)}.style{margin-top:38px;background:white;border:1px solid var(--line);border-radius:8px;padding:25px}.style h2{font-size:20px}.palette{display:flex;gap:18px;flex-wrap:wrap;margin:20px 0}.swatch{font-size:12px}.swatch b{display:block;width:56px;height:28px;border:1px solid #92a2ab;border-radius:3px;margin-bottom:5px}.baseline{display:grid;grid-template-columns:1fr 1fr;gap:18px;margin-top:18px}.baseline img{max-height:240px;object-fit:contain;background:#7b909a}.links{display:flex;flex-wrap:wrap;gap:20px;font-size:13px;margin-top:22px}footer{margin-top:25px;color:var(--muted);font-size:12px}dialog{width:min(98vw,1700px);max-width:98vw;max-height:96vh;padding:12px;border:1px solid #8196a1;border-radius:9px;background:#e5edef;color:var(--ink)}dialog::backdrop{background:#0c1e2bdf}dialog img{display:block;max-width:100%;max-height:84vh;object-fit:contain;margin:auto}dialog header{display:flex;align-items:center;justify-content:space-between;padding:0 0 10px;gap:20px}dialog button{font:inherit;background:white;border:1px solid #9aadb8;border-radius:4px;padding:4px 14px;cursor:pointer}@media(max-width:640px){main{padding:0 15px 35px}h1{font-size:28px}.nav{gap:5px}.nav a{padding:5px 10px}.originals{grid-template-columns:1fr 1fr}.asset-head{gap:8px}h2{font-size:23px}.baseline{grid-template-columns:1fr}.chip{font-size:10px;padding:4px 8px}}
</style><main><header class="intro"><p class="eyebrow">GULISTRIKE / SHIP / BATCH 02</p><h1>三组件表面风格设计</h1><p class="lead">先确认配色、框线和三渲二光影，再依据已审图纸与原模型制作贴图。每件设计板包含效果图和正、侧、背三视图。</p><p class="muted">参考审核 A · 候选集 01 · 2026.09.17</p></header>
<nav class="nav" aria-label="组件">__NAV__</nav>__SECTIONS__
<section class="style"><p class="eyebrow">SHARED STYLE</p><h2>共用首批已审风格</h2><p class="intent">浅色装甲、深蓝灰机械结构、功能色完整分区；外轮廓清楚，内线只强调真实接缝与结构，明暗分为三档。</p><div class="palette">__SWATCHES__</div>
<div class="baseline"><figure><a class="zoom" href="../Previews/v4/Twin_Barrel_Turret_hero.png" data-label="首批已审双联炮 v4"><img src="../Previews/v4/Twin_Barrel_Turret_hero.png" alt="首批已审双联炮v4" loading="lazy"></a><figcaption>赭黄配色与线稿基准 · 首批双联炮 v4</figcaption></figure><figure><a class="zoom" href="../Previews/v4/CIWS_hero.png" data-label="首批已审CIWS v4"><img src="../Previews/v4/CIWS_hero.png" alt="首批已审CIWSv4" loading="lazy"></a><figcaption>青蓝配色与光影基准 · 首批 CIWS v4</figcaption></figure></div>
<p class="muted">本页三张设计板由内置图像生成工具依据原模型视图制作。实际 Blender 材质、框线与运动效果将在审核 B 提供。审核决定请回复当前对话。</p>
<div class="links"><a href="References/reference_A_manifest_v1.json">版本与来源清单</a><a href="References/prompts_v1.json">首版提示词</a><a href="References/prompts_v2.json">修订提示词</a><a href="Source/source_snapshot_v1.json">原型接口快照</a></div></section>
<footer>规范 1.0 · 本批 A 待审核 / B 未开始 · 原模型是形体与尺寸依据</footer></main><dialog id="viewer"><header><span id="view-title"></span><button id="close" type="button">关闭</button></header><img id="view-image" alt=""></dialog><script>
const dlg=document.getElementById('viewer'),pic=document.getElementById('view-image');
document.querySelectorAll('a.zoom').forEach(a=>a.addEventListener('click',e=>{e.preventDefault();pic.src=a.getAttribute('href');pic.alt=a.dataset.label||'';document.getElementById('view-title').textContent=pic.alt;dlg.showModal()}));
document.getElementById('close').addEventListener('click',()=>dlg.close());dlg.addEventListener('click',e=>{if(e.target===dlg)dlg.close()});
</script></html>'''
    swatches = ''.join(f'<div class="swatch"><b style="background:{color}"></b>{name}</div>' for name, color in [
        ('浅色装甲', '#DDE6E6'), ('机械深色', '#23384A'), ('中间明度', '#4B6577'),
        ('钢件', '#8298A5'), ('对地赭黄', '#C9903E'), ('对空青蓝', '#348B9F'), ('框线', '#213D50')])
    page = page.replace('__NAV__', nav).replace('__SECTIONS__', '\n'.join(sections)).replace('__SWATCHES__', swatches)
    page_path = ROOT / 'Review_A_v1.html'
    page_path.write_text(page, encoding='utf-8')
    report = {
        'phase': 'reference_A', 'source_capture_success': True, 'frozen_source_authoring_hashes': 'passed',
        'all_15_original_view_hashes': 'passed', 'selected_reference_images': [p['reference_sheet'] for p in manifest['parts'].values()],
        'manifest': artifact(manifest_path), 'review_page': artifact(page_path),
        'user_A': 'pending', 'user_B': 'not_started', 'UE_formal_validation': 'not_run',
    }
    (ROOT / 'reference_package_validation_v1.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'review': str(page_path), 'assets': len(manifest['parts']), 'A': 'pending'}, ensure_ascii=False))


if __name__ == '__main__':
    main()
