"""Package the four remaining original-mesh material references for approval A."""
from pathlib import Path
import hashlib
import json
import re
import struct

PROJECT = Path(__file__).resolve().parents[2]
ROOT = PROJECT / 'ArtSource/Ships/ShipComponentStyle_20260917/Batch04'
ASSETS = [
    ('Bottom_Twin_Barrel_Turret', '底置双联炮', '赭黄', '#C9903E', '保留宽平上甲与双长炮管；上甲浅色、侧甲赭黄、炮管和安装结构深蓝灰。'),
    ('High_Rate_Fire_Cannon', '高射速炮', '钢蓝', '#4C7891', '保留双短炮管、顶部安装环与后部散热结构；侧罩钢蓝、上部浅色、凹入结构深色。'),
    ('Incendiary_Bomb_LaunchBay', '燃烧弹发射舱', '燃橙', '#C66B38', '保留五组原舱体，橙色舱盖配浅色框架；两端朝外装甲面加入清楚的白色火焰标志。'),
    ('Missile_Bay', '导弹舱', '砖红', '#B16352', '保留双列封闭长舱盖与中央连接件；砖红舱盖、浅色端板和边框、深蓝舱壁。'),
]
LABELS = {'hero': '效果角度', 'front': '正视', 'right': '右侧', 'rear': '背视', 'top': '俯视补充'}
VERSIONS = {'High_Rate_Fire_Cannon': 'v2'}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def artifact(path):
    path = Path(path)
    data = {'path': path.relative_to(ROOT).as_posix(), 'sha256': sha(path), 'bytes': path.stat().st_size}
    if path.suffix.lower() == '.png':
        with path.open('rb') as stream:
            header = stream.read(24)
        assert header[:8] == b'\x89PNG\r\n\x1a\n'
        data['resolution'] = list(struct.unpack('>II', header[16:24]))
    return data


def dump(path, data):
    Path(path).write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def main():
    source_path = ROOT / 'Source/source_snapshot_v1.json'
    original_path = ROOT / 'Source/prototype_render_manifest_v1.json'
    source = json.loads(source_path.read_text(encoding='utf-8'))
    original = json.loads(original_path.read_text(encoding='utf-8'))
    assert source['success'] and source['source_packages_unchanged']
    manifest = {'schema': 'gulistrike-art-reference-review/v1', 'work_id': 'WORK-20260917-006',
        'date': '2026-09-17', 'art_revision': '1.0', 'phase': 'reference_A', 'review_bundle': 'v1',
        'user_instruction': '全都处理了，燃烧弹发射舱 需要有个火焰的标志',
        'scope': 'Four remaining independent component meshes. Batch02 B/export remains a separately recorded review.',
        'A': {'status': 'pending', 'user_decision': None}, 'B': {'status': 'not_started', 'user_decision': None},
        'production_textures_and_outlines': 'not_started',
        'geometry_policy': 'Frozen original meshes and current authoring rigs are authoritative. Reference sheets constrain surface treatment only; no geometry redesign.',
        'flame_marking': 'Flat cream flame pictogram on orange exterior short-end armor panels of Incendiary_Bomb_LaunchBay; no geometry or VFX.',
        'image_generation_mode': 'built-in image_gen', 'source_capture': artifact(source_path),
        'original_view_manifest': artifact(original_path), 'source_packages_unchanged': True,
        'style_baseline': {'path': '../Previews/v4/Twin_Barrel_Turret_hero.png', 'version': 'approved actual material v4', 'sha256': sha(ROOT.parent / 'Previews/v4/Twin_Barrel_Turret_hero.png')},
        'prompt_sets': [artifact(ROOT / 'References/prompts_v1.json'), artifact(ROOT / 'References/prompt_high_rate_retry_v1.json'), artifact(ROOT / 'References/prompt_high_rate_color_fix_v2.json')],
        'generated_output_provenance': artifact(ROOT / 'References/generated_outputs_v1.json'),
        'UE_formal_import_and_runtime_validation': 'not_run', 'parts': {}}
    sections = []
    for number, (key, label, accent, color, intent) in enumerate(ASSETS, 1):
        base, part = original['parts'][key], source['parts'][key]
        authoring = ROOT / base['original_source']
        inspection = ROOT / base['inspection_blend']
        assert sha(authoring) == base['original_source_sha256']
        assert sha(inspection) == base['inspection_blend_sha256']
        for record in part['exports']:
            assert sha(ROOT / record['path']) == record['sha256']
        views = {}
        for view, record in base['views'].items():
            actual = artifact(ROOT / record['path'])
            assert actual['sha256'] == record['sha256']
            views[view] = actual
        version = VERSIONS.get(key, 'v1')
        sheet = artifact(ROOT / f'References/{key}_Reference_{version}.png')
        skeletal = base['mechanical_type'] == 'skeletal'
        manifest['parts'][key] = {'label': label, 'reference_version': version, 'A': 'pending', 'B': 'not_started',
            'reference_sheet': sheet, 'intent': intent, 'accent': accent, 'proposed_accent_srgb': color,
            'original_authoring_source': artifact(authoring), 'original_inspection_blend': artifact(inspection),
            'original_static_exports': [artifact(ROOT / r['path']) for r in part['exports']],
            'original_geometry_snapshot': artifact(ROOT / f'Source/{key}_original_geometry.json'),
            'original_views': views, 'dimensions_m': base['dimensions_m'], 'vertices': base['vertices'],
            'authoring_triangles': base['triangles'], 'original_static_triangles': part['original_triangles'],
            'bones': part.get('bones', []), 'mechanical_type': base['mechanical_type'],
            'blueprint': part['blueprint'], 'visual_mesh': part['visual_mesh'], 'mesh_sockets': part['sockets'],
            'blueprint_compatible_sockets': part['compatible_sockets'], 'part_relative_transform': part['part_relative_transform'],
            'assistant_visual_review': 'Inspected visible silhouettes, counts, large color regions, structural outlines and flame placement against actual original views; not user approval.'}
        figures = ''.join(f'<figure><a class="zoom" href="{a["path"]}" data-label="{label}原型 · {LABELS[v]}"><img src="{a["path"]}" alt="{label}原模型{LABELS[v]}" loading="lazy"></a><figcaption>{LABELS[v]}</figcaption></figure>' for v, a in views.items())
        dims = ' × '.join(f'{x:.2f}' for x in base['dimensions_m'])
        rig = '骨骼：Root → BarrelPitch' if skeletal else '静态模型'
        note = '<p class="mark-note">火焰标志：现有两端外侧装甲面上的白色平面涂装。正/背视沿五组舱体的长轴观察，右侧视展示五组；完整结构以原型对照为准。</p>' if key == 'Incendiary_Bomb_LaunchBay' else ''
        sections.append(f'''<section class="asset" id="{key}"><header class="asset-head"><div><p class="eyebrow">REMAINING COMPONENT {number:02}</p><h2>{label}<small>{version}</small></h2></div><span class="chip">{accent} · 待审核 A</span></header><p class="intent">{intent}</p>{note}
<a class="sheet zoom" href="{sheet['path']}" data-label="{label}参考设计 {version}"><img src="{sheet['path']}" alt="{label}效果图及正、右侧、背三视图"></a><p class="caption">效果图 + 正 / 右侧 / 背三视图 · 点击放大</p>
<details><summary>展开原模型对照与尺寸</summary><div class="originals">{figures}</div><p class="muted">原型工作源 {base['triangles']} 三角面 · {dims} 米 · {rig} · 当前网格 Socket {len(part['sockets'])} 个。尺寸、轮廓和接口以冻结源为准。</p><div class="links"><a href="{base['inspection_blend']}">原型检查 Blender</a><a href="{base['original_source']}">冻结工作源</a></div></details></section>''')
    old = (ROOT.parent / 'Batch02/Review_A_v1.html').read_text(encoding='utf-8')
    css = re.search(r'<style>(.*?)</style>', old, re.S).group(1)
    js = re.search(r'<script>(.*?)</script>', old, re.S).group(1)
    css += '.mark-note{padding:12px 16px;border-left:3px solid #c66b38;background:#f0e4d8;font-size:14px}.nav{flex-wrap:wrap}'
    nav = ''.join(f'<a href="#{key}">{name} <span>{VERSIONS.get(key,"v1")}</span></a>' for key, name, _, _, _ in ASSETS)
    swatches = ''.join(f'<div class="swatch"><b style="background:{color}"></b>{name}</div>' for name, color in [(r[1],r[3]) for r in ASSETS]+[('浅色装甲','#DDE6E6'),('深蓝结构','#23384A'),('机械中间色','#4B6577'),('钢件','#8298A5'),('框线','#213D50')])
    page = f'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Ship 剩余四组件 · 参考审核 A</title><style>{css}</style><main>
<header class="intro"><p class="eyebrow">GULISTRIKE / SHIP / BATCH 04</p><h1>剩余四件 · 表面风格参考</h1><p class="lead">燃烧弹发射舱加入火焰标志。四件继续保留原模型，通过配色、框线和三渲二光影统一到 Ship 风格。</p><p class="muted">参考审核 A · 候选集 01（高射速炮 v2，其余 v1）· 2026.09.17</p><div class="palette">{swatches}</div></header><nav class="nav">{nav}</nav>{''.join(sections)}
<section class="style"><p class="eyebrow">SHARED STYLE</p><h2>共用 Ship 风格</h2><p class="intent">浅色装甲、深蓝灰结构、完整功能色区、主要结构内线和三档光影。</p><div class="baseline"><figure><a class="zoom" href="../Previews/v4/Twin_Barrel_Turret_hero.png" data-label="首批已审双联炮 v4"><img src="../Previews/v4/Twin_Barrel_Turret_hero.png" alt="首批已审 Ship 材质风格" loading="lazy"></a><figcaption>已审首批实际 Blender 风格</figcaption></figure></div>
<p class="muted">这些设计板根据本批实际网格视图生成，用于配色、标识和线稿审核。通过 A 后在原模型上制作贴图与材质，再提交实际 Blender 成品审核 B。</p><div class="links"><a href="References/reference_A_manifest_v1.json">来源与版本清单</a><a href="Source/source_snapshot_v1.json">原型接口快照</a><a href="../Batch02/Review_B_v2.html">第二批三件成品审核 B</a></div></section>
<footer>规范 1.0 · 第四批 A 待审核 / B 未开始 · UE 正式验证未运行</footer></main><dialog id="viewer"><header><span id="view-title"></span><button id="close" type="button">关闭</button></header><img id="view-image" alt=""></dialog><script>{js}</script></html>'''
    mp, hp = ROOT/'References/reference_A_manifest_v1.json', ROOT/'Review_A_v1.html'
    dump(mp, manifest)
    hp.write_text(page, encoding='utf-8')
    dump(ROOT/'reference_package_validation_v1.json', {'phase':'reference_A','technical_status':'passed',
        'source_capture_success':True,'source_packages_unchanged':True,'frozen_authoring_and_static_exports':'passed',
        'original_inspection_blend_hashes':'passed','all_20_original_view_hashes':'passed',
        'selected_reference_images':[p['reference_sheet'] for p in manifest['parts'].values()],
        'manifest':artifact(mp),'review_page':artifact(hp),'user_A':'pending','user_B':'not_started','UE_formal_validation':'not_run'})
    print(json.dumps({'review':str(hp),'parts':len(ASSETS),'A':'pending'},ensure_ascii=False))


if __name__ == '__main__':
    main()
