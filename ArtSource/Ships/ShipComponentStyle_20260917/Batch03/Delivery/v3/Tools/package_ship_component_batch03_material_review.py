"""Package the third batch's actual static Blender surfaces for review B."""
from pathlib import Path
import hashlib
import json
import re
import struct
import shutil

PROJECT = Path(__file__).resolve().parents[2]
ROOT = PROJECT / 'ArtSource/Ships/ShipComponentStyle_20260917/Batch03'
VERSION = 'v3'
OUT = ROOT / 'Production' / VERSION
PRE = ROOT / 'Previews' / VERSION
PARTS = [('Drone_LaunchBay', '无人机发射舱', '赭黄 · 僚机飞出标识'),
         ('Electronic_JammingDevice', '电子干扰装置', '深蓝 / 白 / 黄'),
         ('Shield_Generator', '护盾发生器', '叶绿')]
VIEWS = [('hero', '效果'), ('front', '正视'), ('right', '右侧'), ('rear', '背视')]
CROPS = {
    'Drone_LaunchBay': [[10,48,790,586], [810,48,625,586], [10,646,859,408], [878,646,557,408]],
    'Electronic_JammingDevice': [[15,53,712,467], [736,53,784,467], [15,528,712,468], [736,528,784,468]],
    'Shield_Generator': [[18,42,704,510], [728,42,702,510], [18,558,704,504], [728,558,702,504]],
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def artifact(path):
    path = Path(path)
    result = {'path': path.relative_to(ROOT).as_posix(), 'bytes': path.stat().st_size, 'sha256': sha(path)}
    if path.suffix == '.png':
        with path.open('rb') as stream:
            header = stream.read(24)
        result['resolution'] = list(struct.unpack('>II', header[16:24]))
    return result


def dump(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def figure(path, label):
    return f'<figure><a class="zoom" href="{path}" data-label="{label}"><img src="{path}" alt="{label}" loading="lazy"></a><figcaption>{label}</figcaption></figure>'


def main():
    approval = json.loads((ROOT / 'approval_A_20260917.json').read_text(encoding='utf-8-sig'))
    readback = json.loads((OUT / 'saved_blender_readback.json').read_text(encoding='utf-8'))
    assert approval['decision'] == 'approved'
    manifest = {'schema': 'gulistrike-art-material-review/v1', 'date': '2026-09-17', 'work_id': 'WORK-20260917-005',
        'candidate_version': VERSION, 'art_revision': '1.0', 'A': 'approved', 'B': 'pending',
        'scope': 'Original static mesh surface treatment; actual EEVEE renders; final FBX after B',
        'approval_A': artifact(ROOT / 'approval_A_20260917.json'),
        'overview_blend': artifact(OUT / 'ShipComponentStyle_Batch03_Overview.blend'),
        'saved_blender_readback': artifact(OUT / 'saved_blender_readback.json'),
        'launch_markings': artifact(OUT / 'Drone_LaunchBay_launch_markings.json'),
        'editable_launch_vector': artifact(OUT / 'Drone_LaunchBay_launch_marking.svg'),
        'user_exterior_marking_instruction': artifact(ROOT / 'References/Drone_LaunchBay_User_ExteriorMarking_20260917.png'),
        'surface_revision': artifact(ROOT / 'surface_revision_20260917_exterior_marking.json'),
        'UE_formal_validation': 'not_run', 'FBX_export': 'not_run_await_B',
        'CIWS': 'unchanged; first batch v4 remains approved', 'parts': {}}
    sections, config = [], {}
    for number, (key, label, accent) in enumerate(PARTS, 1):
        rp = OUT / f'{key}_material_report.json'
        report = json.loads(rp.read_text(encoding='utf-8'))
        paint = json.loads((OUT / f'{key}_paint_regions.json').read_text(encoding='utf-8'))
        blend = OUT / f'{key}_OriginalMesh_MaterialCandidate.blend'
        assert sha(blend) == report['blend_sha256'] == readback[key]['blend_sha256']
        assert report['original_geometry_sha256'] == report['finished_body_geometry_sha256']
        assert report['body_geometry_unchanged'] and report['original_corner_normals_unchanged']
        assert report['original_object_matrix_unchanged'] and report['original_UV_channels_unchanged']
        assert report['motion']['status'] == 'not_applicable_static' and report['bones'] == []
        assert readback[key]['packed_2K_textures'] == 3
        ref = approval['parts'][key]['reference_sheet']
        assert sha(ROOT / ref['path']) == ref['sha256']
        textures = [artifact(OUT / 'Textures' / f'T_SC_{key}_{suffix}_2K.png') for suffix in ('BaseColor', 'ORM', 'LineMask')]
        assert all(t['resolution'] == [2048, 2048] for t in textures)
        media = [artifact(PRE / f'{key}_{v}.png') for v in ('hero', 'front', 'right', 'rear', 'top', 'original_geometry_gray', 'lines_off', 'alternate_light', 'small')]
        if key == 'Drone_LaunchBay':
            media += [artifact(PRE / f'{key}_{v}.png') for v in ('exterior_hero', 'left')]
        material = {
            'palette_sRGB': paint['palette_sRGB'], 'render_engine': 'BLENDER_EEVEE',
            'view_transform': 'Standard', 'look': 'None',
            'shader': 'Diffuse -> ShaderToRGB -> luminance -> constant three-tone ramp; multiply BaseColor; independent ink mix',
            'three_tone_thresholds': report['toon_thresholds'], 'three_tone_multipliers_linear': report['toon_multipliers_linear'],
            'line_strength': report['line_strength'], 'line_halfwidth_m': report['internal_line_halfwidth_m'],
            'outline_width_m': report['outline_width_m'], 'outline_collection': 'OUTLINE_TOGGLE', 'outline_export': False,
            'UV_channels': report['uv_channels'], 'paint_UV': 'SC_PaintUV', 'line_UV': 'SC_LineUV',
            'editable_face_attribute': 'SC_PaintPaletteIndex', 'editable_corner_color': 'SC_EditablePaintColor',
            'BaseColor': 'sRGB; paint and launch pictograms only; no baked light/shadows',
            'ORM': 'Non-Color; R=1, G=roughness, B=metallic; retained packed for future PBR data',
            'LineMask': 'Non-Color; structural edges and material boundaries; coplanar diagonals omitted',
            'lighting': 'Studio_Key sun energy 2; scene light and self-shadow drive tone bands',
            'mechanical_type': 'static; no added bones or animation',
            'original_object_matrix': report['source_object_matrix'],
            'FBX_material_note': 'EEVEE toon nodes and optional outline are Blender presentation; rebuild equivalent shader from these portable data in the target engine.'}
        dump(OUT / f'{key}_material_parameters.json', material)
        manifest['parts'][key] = {'label': label, 'reference': ref,
            'reference_version': approval['parts'][key]['reference_version'], 'candidate_version': VERSION,
            'blend': artifact(blend), 'report': artifact(rp), 'textures': textures, 'previews': media,
            'parameters': artifact(OUT / f'{key}_material_parameters.json'),
            'paint_regions': artifact(OUT / f'{key}_paint_regions.json'),
            'body_triangles': report['body_triangles'], 'body_material_slots': 1,
            'source_geometry_unchanged': True, 'source_normals_unchanged': True,
            'source_transform_unchanged': True, 'source_UV_unchanged': True,
            'bones': [], 'socket_count': len(report['sockets']), 'B': 'pending'}
        config[key] = {'label': label, 'ref': ref['path'], 'refSize': ref['resolution'], 'crops': CROPS[key],
                      'views': {v: f'Previews/{VERSION}/{key}_{v}.png' for v, _ in VIEWS}}
        buttons = ''.join(f'<button type="button" data-view="{v}" aria-pressed="{str(v == "hero").lower()}">{name}</button>' for v, name in VIEWS)
        extras = ''.join(figure(f'Previews/{VERSION}/{key}_{v}.png', name) for v, name in [
            ('original_geometry_gray', '原网格灰模'), ('top', '俯视补充'), ('lines_off', '关闭内线与外轮廓'),
            ('alternate_light', '更换光照方向'), ('small', '缩小视图')])
        marker = '<p class="intent">按你补充的标注，僚机剪影与箭头已移到薄侧壁朝外的一面；飞出方向保持。原顶面与厚侧壁的标识已移除。</p><div class="links"><a href="References/Drone_LaunchBay_User_ExteriorMarking_20260917.png">用户外侧位置标注</a></div>' if key == 'Drone_LaunchBay' else ''
        exterior = ('<div class="gallery">' + figure(f'Previews/{VERSION}/{key}_exterior_hero.png', '外侧效果 · 标识位置') + figure(f'Previews/{VERSION}/{key}_left.png', '外侧正视 · 飞出方向保持') + '</div>') if key == 'Drone_LaunchBay' else ''
        dimensions = ' × '.join(f'{v:.2f}' for v in report['dimensions_blender_m'])
        sections.append(f'''<section id="{key}" class="asset" data-key="{key}"><header class="asset-head"><div><p class="eyebrow">SUPPORT COMPONENT {number:02}</p><h2>{label}<small>材质 {VERSION}</small></h2></div><span class="chip">{accent} · B 待审核</span></header>{marker}
<div class="viewbar">{buttons}</div><div class="compare"><figure><div class="imagebox"><a class="refcrop zoom" href="{ref['path']}" data-label="{label}已审参考完整图板"><img class="refimage" src="{ref['path']}" alt="{label}已审参考局部展示"></a></div><figcaption>已审参考 A · {approval['parts'][key]['reference_version']} <span class="view-name">效果</span></figcaption></figure><figure><a class="actual zoom" href="Previews/{VERSION}/{key}_hero.png" data-label="{label}实际 Blender 效果"><img src="Previews/{VERSION}/{key}_hero.png" alt="{label}实际 Blender 渲染"></a><figcaption>实际 Blender 渲染 · <span class="view-name">效果</span></figcaption></figure></div>
{exterior}<details><summary>灰模、框线开关与光影对照</summary><div class="gallery">{extras}</div></details>
<details><summary>可编辑源与材质资料</summary><p class="facts">主体 {report['body_triangles']} 三角面 · 1 个材质槽 · 3 张 2K 贴图 · {dimensions} 米 · 静态组件</p><p class="muted">原始局部顶点、拓扑、法线、物体变换和原有 UV 回读一致。内部线稿与外轮廓分别可关闭。</p><div class="links"><a href="Production/{VERSION}/{key}_OriginalMesh_MaterialCandidate.blend">单件 Blender</a><a href="Production/{VERSION}/{key}_material_parameters.json">材质参数</a><a href="Production/{VERSION}/{key}_paint_regions.json">可编辑分色记录</a><a href="Production/{VERSION}/{key}_material_report.json">技术核对</a></div></details></section>''')
    # Preserve the proven review controls; only supply this batch's content.
    previous_page = (ROOT.parent / 'Batch02/Review_B_v2.html').read_text(encoding='utf-8')
    css = re.search(r'<style>(.*?)</style>', previous_page, re.S).group(1)
    prior_js = re.search(r'<script>(.*?)</script>', previous_page, re.S).group(1)
    js = 'const items=' + json.dumps(config, ensure_ascii=False) + prior_js[prior_js.index(';const names='):]
    nav = ''.join(f'<a href="#{key}">{label}</a>' for key, label, _ in PARTS)
    page = f'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Ship 第三批 · 实际材质审核 B</title><style>{css}.nav{{flex-wrap:wrap}}.intent{{font-size:14px;color:#536d7c}}</style><main>
<header class="intro"><p class="eyebrow">GULISTRIKE / SHIP / BATCH 03</p><h1>支援组件实际材质成品</h1><p class="lead">原模型上的贴图、框线和三渲二光影已完成。无人机飞出标识已按补充标注移到薄壁外侧，干扰装置采用深蓝白黄，护盾发生器采用绿色。</p><p class="muted">成品审核 B · 材质候选 {VERSION} · 2026.09.17 · 本页右侧为实际 Blender 渲染</p></header><nav class="nav">{nav}</nav>{''.join(sections)}
<section class="note"><p><strong>可编辑 Blender 源</strong></p><div class="links"><a href="Production/{VERSION}/ShipComponentStyle_Batch03_Overview.blend">三件总览 .blend</a><a href="Production/{VERSION}/Review_B_Manifest.json">成品与来源清单</a><a href="approval_A_20260917.json">审核 A 通过记录</a><a href="Review_A_v3.html">已审参考图板</a><a href="Production/{VERSION}/Drone_LaunchBay_launch_marking.svg">可编辑僚机标识</a></div><p class="muted">总览按三个 Scene 保存各件原始尺寸与原点。三张 2K 贴图均内嵌并另存；SC_PaintUV 与 SC_LineUV 分离，外轮廓放在 OUTLINE_TOGGLE。三件沿用静态接口，CIWS 保持原样。</p><p class="muted">本次参考 A 由“开始实施”放行。待你审核实际成品 B 后导出最终 FBX；UE 正式接入另行安排。参考仅在网页中裁切展示，原图文件保持。</p></section>
<footer>规范 1.0 · A 已通过 / B 待用户审核 · UE 正式验证未运行</footer></main><dialog id="viewer"><header><span id="view-title"></span><button id="close" type="button">关闭</button></header><img id="view-image" alt=""></dialog><script>{js}</script></html>'''
    path = ROOT / f'Review_B_{VERSION}.html'
    path.write_text(page, encoding='utf-8')
    tools_folder = OUT / 'Tools'
    tools_folder.mkdir(exist_ok=True)
    tool_paths = ['Scripts/Blender/style_ship_component_batch03_materials.py',
                  'Scripts/Blender/style_ship_component_original_materials.py',
                  'Scripts/Blender/bake_ship_lineart_mask.py',
                  'Scripts/Blender/finish_ship_component_batch03_review.py',
                  'Scripts/Blender/finish_ship_component_material_review.py',
                  'Scripts/Art/package_ship_component_batch03_material_review.py']
    manifest['production_tools'] = []
    for source in tool_paths:
        dest = tools_folder / Path(source).name
        shutil.copy2(PROJECT / source, dest)
        manifest['production_tools'].append(artifact(dest))
    dump(OUT / 'Review_B_Manifest.json', manifest)
    dump(ROOT / f'review_B_package_validation_{VERSION}.json', {'date': '2026-09-17', 'technical_status': 'passed',
        'A': 'approved', 'B': 'pending', 'review': artifact(path), 'manifest': artifact(OUT / 'Review_B_Manifest.json'),
        'verified': '3 saved original geometry/normal/object-transform/original-UV readbacks; 9 packed/external 2K textures; reference hashes and actual preview paths',
        'motion': 'not_applicable_static', 'UE_formal_validation': 'not_run', 'FBX_export': 'await_B'})
    print(json.dumps({'review': str(path), 'candidate': VERSION, 'parts': 3, 'A': 'approved', 'B': 'pending'}, ensure_ascii=False))


if __name__ == '__main__':
    main()
