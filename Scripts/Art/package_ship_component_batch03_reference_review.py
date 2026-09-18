"""Package immutable support-component references for user approval A."""
from pathlib import Path
import hashlib
import json
import re
import struct

PROJECT = Path(__file__).resolve().parents[2]
ROOT = PROJECT / 'ArtSource/Ships/ShipComponentStyle_20260917/Batch03'
BUNDLE = 'v3'
ASSETS = [
    ('Drone_LaunchBay', '无人机发射舱', 'v2', '赭黄', '保留长条导轨与非对称舱体，增加僚机剪影及沿导轨指向指定出口的箭头。'),
    ('Electronic_JammingDevice', '电子干扰装置', 'v3', '深蓝 / 白 / 黄', '按指定三色重配：深蓝阵面、白色外框和支座，黄色用于前部盖板及两侧连接护罩。'),
    ('Shield_Generator', '护盾发生器', 'v3', '叶绿', '中央头罩与两个前部小模块改为绿色；双立柱、中央壳体斜面及基座上层保持浅色。'),
]
ACCENTS = {'Drone_LaunchBay': '#C9903E', 'Electronic_JammingDevice': '#E0BC49', 'Shield_Generator': '#65955D'}
LABELS = {'hero': '效果角度', 'front': '正视', 'right': '右侧', 'rear': '背视', 'top': '俯视补充'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def artifact(path):
    path = Path(path)
    data = {'path': path.relative_to(ROOT).as_posix(), 'sha256': sha(path), 'bytes': path.stat().st_size}
    if path.suffix == '.png':
        with path.open('rb') as stream:
            header = stream.read(24)
        assert header[:8] == b'\x89PNG\r\n\x1a\n'
        data['resolution'] = list(struct.unpack('>II', header[16:24]))
    return data


def dump(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def main():
    source_path = ROOT / 'Source/source_snapshot_v1.json'
    source = json.loads(source_path.read_text(encoding='utf-8'))
    original_path = ROOT / 'Source/prototype_render_manifest_v1.json'
    original = json.loads(original_path.read_text(encoding='utf-8'))
    assert source['success'] and source['source_packages_unchanged']
    marker = artifact(ROOT / 'References/Drone_LaunchBay_User_ExitDirection_20260917.png')
    manifest = {'schema': 'gulistrike-art-reference-review/v1', 'work_id': 'WORK-20260917-005',
        'date': '2026-09-17', 'art_revision': '1.0', 'phase': 'reference_A', 'review_bundle': BUNDLE,
        'previous_review_manifest': artifact(ROOT / 'References/reference_A_manifest_v2.json'),
        'user_color_instruction': '干扰装置  改成深蓝、白、黄',
        'earlier_color_instruction': '点子干扰器和混沌发生器和CIWS  色系做一下区分，这三个色系太一致了，CIWS 不改',
        'color_interpretation': 'Jammer navy/white/yellow palette is user-directed; concrete cover placement awaits A. Shield green proposal and approved CIWS cyan remain unchanged.',
        'ciws_baseline': {'path': '../Previews/v4/CIWS_hero.png', 'accent_srgb': '#348B9F', 'sha256': sha(ROOT.parent / 'Previews/v4/CIWS_hero.png'), 'modification': 'none'},
        'user_selection_instruction': '继续挑选一批组件进行制作',
        'user_marking_instruction': '无人机发射舱需要有僚机飞出标识；出口方向如用户箭头',
        'user_direction_image': marker,
        'launch_marker_rule': 'Flat aircraft pictogram and arrow painted on existing armor, parallel to rail, towards the far upper-right end of the original hero view. Same physical direction in all views; no new geometry or gameplay socket.',
        'A': {'status': 'pending', 'user_decision': None}, 'B': {'status': 'not_started', 'user_decision': None},
        'production_textures_and_outlines': 'not_started',
        'geometry_policy': 'Frozen original static meshes are authoritative; reference images constrain surface treatment only.',
        'image_generation_mode': 'built-in image_gen', 'source_capture': artifact(source_path),
        'source_capture_method': artifact(ROOT / 'Source/capture_method_v1.json'),
        'original_view_manifest': artifact(original_path), 'source_packages_unchanged': True,
        'UE_formal_import_and_runtime_validation': 'not_run',
        'prompt_sets': [artifact(ROOT / 'References' / n) for n in ['prompts_v1.json', 'prompts_v1_launch_marker.json', 'prompts_round2.json', 'prompts_round3.json', 'prompts_drone_v2.json', 'prompts_color_separation_v1.json', 'prompts_jammer_navy_white_yellow_v1.json']],
        'color_revision_outputs': artifact(ROOT / 'References/generated_outputs_jammer_navy_white_yellow_v1.json'),
        'prior_color_revision_outputs': artifact(ROOT / 'References/generated_outputs_color_separation_v1.json'),
        'parts': {}}
    sections = []
    for number, (key, label, version, accent, intent) in enumerate(ASSETS, 1):
        base = original['parts'][key]
        part = source['parts'][key]
        fbx = ROOT / base['source_fbx']
        inspection = ROOT / base['inspection_blend']
        assert sha(fbx) == base['source_fbx_sha256'] == part['exports'][0]['sha256']
        assert sha(inspection) == base['inspection_blend_sha256']
        sheet = artifact(ROOT / f'References/{key}_Reference_{version}.png')
        views = {}
        for view, record in base['views'].items():
            actual = artifact(ROOT / record['path'])
            assert actual['sha256'] == record['sha256']
            views[view] = actual
        manifest['parts'][key] = {'label': label, 'reference_version': version, 'A': 'pending', 'B': 'not_started',
            'reference_sheet': sheet, 'intent': intent, 'accent': accent, 'proposed_accent_srgb': ACCENTS[key], 'original_static_fbx': artifact(fbx),
            'original_inspection_blend': artifact(inspection),
            'original_geometry_snapshot': artifact(ROOT / f'Source/{key}_original_geometry.json'),
            'original_views': views, 'original_mesh_dimensions_m': base['dimensions_m'],
            'original_vertices': base['vertices'], 'original_triangles': base['triangles'],
            'bones': [], 'mechanical_type': 'static', 'blueprint': part['blueprint'], 'visual_mesh': part['visual_mesh'],
            'mesh_sockets': part['sockets'], 'blueprint_compatible_sockets': part['compatible_sockets'],
            'part_relative_transform': part['part_relative_transform'],
            'assistant_visual_review': 'Reference views, original silhouette, navy-white-yellow jammer and green shield color consistency, and unchanged launch-direction marking inspected; not user approval.'}
        figures = ''.join(f'<figure><a class="zoom" href="{a["path"]}" data-label="{label}原型 · {LABELS[v]}"><img src="{a["path"]}" alt="{label}原模型{LABELS[v]}" loading="lazy"></a><figcaption>{LABELS[v]}</figcaption></figure>' for v, a in views.items())
        dimensions = ' × '.join(f'{x:.2f}' for x in base['dimensions_m'])
        mark_note = '<p class="mark-note">飞出方向：沿导轨指向效果图右上方远端。僚机剪影和箭头为装甲表面的平面标识。</p>' if key == 'Drone_LaunchBay' else ''
        sections.append(f'''<section class="asset" id="{key}"><header class="asset-head"><div><p class="eyebrow">SUPPORT COMPONENT {number:02}</p><h2>{label}<small>{version}</small></h2></div><span class="chip">{accent} · 待审核 A</span></header><p class="intent">{intent}</p>{mark_note}
<a class="sheet zoom" href="{sheet['path']}" data-label="{label}参考设计 {version}"><img src="{sheet['path']}" alt="{label}{version}效果图及正、右侧、背三视图"></a><p class="caption">二维表面参考 · 效果图 + 正 / 右侧 / 背三视图 · 点击放大</p>
<details><summary>展开原模型对照与尺寸</summary><div class="originals">{figures}</div><p class="muted">原网格 {base['triangles']} 三角面 · {dimensions} 米 · 静态模型 · 网格自带 Socket {len(part['sockets'])} 个。尺寸、轮廓和接口以冻结网格为准。</p><div class="links"><a href="{base['inspection_blend']}">原型检查 Blender</a><a href="{base['source_fbx']}">原型参考 FBX</a></div></details></section>''')
    # Reuse the established local review-page layout, without modifying the prior batch.
    old_page = (ROOT.parent / 'Batch02/Review_A_v1.html').read_text(encoding='utf-8')
    css = re.search(r'<style>(.*?)</style>', old_page, re.S).group(1)
    js = re.search(r'<script>(.*?)</script>', old_page, re.S).group(1)
    css += '.mark-note{padding:10px 14px;border-left:3px solid #c9903e;background:#ece9db;font-size:13px}.nav{flex-wrap:wrap}'
    nav = ''.join(f'<a href="#{key}">{name} <span>{version}</span></a>' for key, name, version, _, _ in ASSETS)
    swatches = ''.join(f'<div class="swatch"><b style="background:{color}"></b>{name}</div>' for name, color in [('CIWS · 青蓝（保持）','#348B9F'),('干扰装置 · 识别黄','#E0BC49'),('护盾发生器 · 叶绿','#65955D'),('机库 · 赭黄','#C9903E'),('白色装甲','#DDE6E6'),('深蓝阵面与机械结构','#23384A'),('中间明度','#4B6577'),('钢件','#8298A5'),('框线','#213D50')])
    page = f'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Ship 第三批 · 支援组件参考审核 A</title><style>{css}</style><main>
<header class="intro"><p class="eyebrow">GULISTRIKE / SHIP / BATCH 03</p><h1>机库与支援组件</h1><p class="lead">干扰装置使用你指定的深蓝、白、黄。护盾保留绿色候选，CIWS 保持已审青蓝，无人机舱保持赭黄与同向飞出标识。</p><p class="muted">参考审核 A · 候选集 03 · 2026.09.17 · 本轮调整配色，保留原模型依据和已设计的框线、光影。</p><div class="palette">{swatches}</div></header><nav class="nav">{nav}</nav>{''.join(sections)}
<section class="style"><p class="eyebrow">SHARED STYLE</p><h2>共用 Ship 风格</h2><p class="intent">浅色装甲与深蓝灰结构、完整功能色区、主要结构内线和三档光影。原有曲面、薄片、天线和安装结构保留。</p><div class="palette">{swatches}</div>
<div class="baseline"><figure><a class="zoom" href="../Previews/v4/Twin_Barrel_Turret_hero.png" data-label="首批已审双联炮 v4"><img src="../Previews/v4/Twin_Barrel_Turret_hero.png" alt="首批赭黄风格" loading="lazy"></a><figcaption>首批已审双联炮 v4 · 赭黄配色</figcaption></figure><figure><a class="zoom" href="../Previews/v4/CIWS_hero.png" data-label="首批已审CIWS v4 · 本轮保持原样"><img src="../Previews/v4/CIWS_hero.png" alt="CIWS 青蓝保持原样" loading="lazy"></a><figcaption>首批已审 CIWS v4 · 青蓝配色保持原样</figcaption></figure></div>
<p class="muted">设计板由内置图像生成工具根据本批实际网格视图制作，仅用于参考审核。三件具体图纸仍待 A，通过后制作可编辑贴图、框线与实际 Blender 成品，再提交 B。</p><div class="links"><a href="References/reference_A_manifest_{BUNDLE}.json">来源与版本清单</a><a href="References/prompts_jammer_navy_white_yellow_v1.json">本轮改色提示词</a><a href="Review_A_v2.html">上一版参考</a><a href="{marker['path']}">用户出口方向标注</a><a href="Source/source_snapshot_v1.json">原型接口快照</a></div></section>
<footer>规范 1.0 · 第三批 A 待审核 / B 未开始 · UE 正式验证未运行</footer></main><dialog id="viewer"><header><span id="view-title"></span><button id="close" type="button">关闭</button></header><img id="view-image" alt=""></dialog><script>{js}</script></html>'''
    manifest_path = ROOT / f'References/reference_A_manifest_{BUNDLE}.json'
    page_path = ROOT / f'Review_A_{BUNDLE}.html'
    dump(manifest_path, manifest)
    page_path.write_text(page, encoding='utf-8')
    dump(ROOT / f'reference_package_validation_{BUNDLE}.json', {'phase': 'reference_A', 'technical_status': 'passed',
        'source_capture_success': True, 'original_static_fbx_hashes': 'passed', 'original_inspection_blend_hashes': 'passed',
        'all_15_original_view_hashes': 'passed', 'user_marking_image': marker,
        'selected_reference_images': [p['reference_sheet'] for p in manifest['parts'].values()],
        'manifest': artifact(manifest_path), 'review_page': artifact(page_path),
        'user_A': 'pending', 'user_B': 'not_started', 'UE_formal_validation': 'not_run'})
    print(json.dumps({'review': str(page_path), 'parts': len(ASSETS), 'A': 'pending'}, ensure_ascii=False))


if __name__ == '__main__':
    main()
