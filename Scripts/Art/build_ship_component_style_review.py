"""Assemble reference review A from frozen inputs and actual generated sheets.

No proposed mesh production, UE package mutation, or image editing occurs here.
"""
import hashlib
import html
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path

PROJECT = Path('D:/UE5.7/test1')
ROOT = PROJECT / 'ArtSource/Ships/ShipComponentStyle_20260917'
KEYS = ('Twin_Barrel_Turret', 'CIWS', 'Thor_MissilePod')
DESIGN_REVISIONS = {'Twin_Barrel_Turret': 'v1', 'CIWS': 'v1', 'Thor_MissilePod': 'v3'}
INFO = {
    'Twin_Barrel_Turret': ('双联炮', '重型对地火力', '#B98535', '赭黄护罩与侧甲；双管、分层装甲和清楚的承重炮架。'),
    'CIWS': ('CIWS', '快速对空火力', '#2F91A4', '青蓝炮身护罩；紧凑轮廓、三管簇和明确的俯仰支座。'),
    'Thor_MissilePod': ('Thor 一级导弹舱', '五组导弹发射单元', '#A45447', '砖红舱盖；保留交错的 5 组 × 3 孔，共 15 个可见孔与 5 个 Socket。'),
}


def write_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def preserve(original, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        if sha(original) != sha(target):
            raise RuntimeError('Existing frozen copy differs: ' + str(target))
    else:
        shutil.copy2(original, target)
    return {'source': str(original.relative_to(PROJECT)).replace('\\', '/'),
            'path': str(target.relative_to(ROOT)).replace('\\', '/'), 'sha256': sha(target)}


def main():
    existing_review = ROOT / 'review_manifest_v1.json'
    if existing_review.exists():
        prior_review = json.loads(existing_review.read_text(encoding='utf-8'))
        if any(p.get(gate, 'pending') != 'pending' for p in prior_review.get('parts', {}).values() for gate in ('A', 'B')):
            raise RuntimeError('A recorded user decision exists; preserve it and create a new review version.')
    snapshot = json.loads((ROOT / 'Source/source_snapshot_v1.json').read_text(encoding='utf-8'))
    previous = json.loads((PROJECT / 'outputs/ship-component-rigs/source-baseline.json').read_text(encoding='utf-8'))
    assert snapshot['success'] and snapshot['source_packages_unchanged']
    now = datetime.now(timezone.utc).isoformat()
    registry = {'schema': 'guli-ship-style-review/v1', 'work_id': 'WORK-20260917-003',
                'reference_revision': 'v1', 'art_revision': '1.0', 'assembled_at': now,
                'stage': 'reference_review_A', 'user_A': 'pending', 'user_B': 'pending',
                'new_model_production': 'not_started', 'final_fbx_delivery': 'not_run',
                'ue_formal_validation': 'not_run', 'source_capture': 'passed',
                'thor_layout_decision': {'value': '5 cells x 3 visible bores; preserve 5 existing sockets',
                                        'user_reply': '保留原型的 5 组 × 3 孔（推荐）', 'date': '2026-09-17'},
                'parts': {}, 'preserved_evidence': [],
                'limits': ['Reference sheets are visual proposals, not measured CAD or finished Blender renders.',
                           'All installation dimensions and interfaces are authoritative in source_snapshot_v1.json.',
                           'Fresh UE skeletal FBX export is unsupported under NullRHI; prior authoring FBX and blend are preserved.',
                           'Source skeletal bounds include the motion envelope; use original_bounds_cm for reference-rest dimensions.']}
    interface_md = ['# Ship 三组件冻结接口 v1', '',
                    '规范 1.0；坐标以 UE 原网格空间、厘米为准。安装变换均为单位变换。',
                    '原点 (0,0,0) 保留；Blender 参考转换为 `(x/100, -y/100, z/100)` 米。',
                    '以下面数是原型数据，不能作为新成品的 LOD0 面数。', '',
                    '两门炮采用 `Root → BarrelPitch`。Root 为单位变换；BarrelPitch 的参考旋转四元数 (x,y,z,w) 为 `(0,0,0.7071067812,0.7071067812)`。',
                    '炮口局部 +X 随参考骨骼变换指向原网格 +Y；保持既有旋转，不擅自改轴。',
                    'Thor 的 Socket 原始旋转均为零；保留该接口，不把孔的视觉朝向自动写回为 Socket 旋转。', '']
    for key in KEYS:
        part = snapshot['parts'][key]
        old = previous['assets']['SM_SC_' + key]
        if old['geometry_sha256'] != part['original_geometry_sha256']:
            raise RuntimeError('Source baseline changed; inspect before review: ' + key)
        dims = [(part['original_bounds_cm'][1][i] - part['original_bounds_cm'][0][i]) / 100 for i in range(3)]
        version = DESIGN_REVISIONS[key]
        sheet = ROOT / 'References' / (key + '_Design_' + version + '.png')
        prompt = ROOT / 'References' / (key + '_Design_' + version + '_prompt.txt')
        if not sheet.is_file() or not prompt.is_file():
            raise RuntimeError('Reference sheet or prompt missing: ' + key)
        row = {'label': INFO[key][0], 'design_version': version, 'A': 'pending', 'B': 'pending',
               'reference_sheet': str(sheet.relative_to(ROOT)).replace('\\', '/'),
               'reference_sha256': sha(sheet), 'prompt_sha256': sha(prompt),
               'source_geometry_sha256': part['original_geometry_sha256'],
               'source_matches_previous_geometry_baseline': True,
               'dimensions_m_xyz': dims, 'original_triangles': part['original_triangles'],
               'sockets': len(part['sockets']), 'visual_bores': 15 if key == 'Thor_MissilePod' else len(part['sockets']),
               'feature_count_visual_review': 'see References/visual_review_v1.json; not user approval', 'mount_origin': [0, 0, 0],
               'prototype_previews': ['Source/Previews/' + key + '_original_' + view + '_v1.png'
                                      for view in ('hero', 'front', 'right', 'rear', 'top')]}
        registry['parts'][key] = row
        if key == 'Thor_MissilePod':
            row['previous_references'] = [
                {'version': 'v1', 'status': 'superseded_before_A', 'reason': 'End-on projections incorrectly showed three cells side by side.'},
                {'version': 'v2', 'status': 'superseded_before_A', 'reason': 'Side projection read as four cells; revised to five distinguishable cells in v3.'}]
        if key != 'Thor_MissilePod':
            for suffix in ('-authoring.json', '-ue-import.json'):
                original = PROJECT / 'outputs/ship-component-rigs' / (key + suffix)
                registry['preserved_evidence'].append(preserve(original, ROOT / 'Source/ExistingAuthoring' / original.name))
            original = PROJECT / 'outputs/ship-component-rigs' / (key + '_+00.png')
            registry['preserved_evidence'].append(preserve(original, ROOT / 'Source/Previews' / (key + '_prior_rig_0deg.png')))
        interface_md += ['## ' + INFO[key][0], '',
                         f"- 原型外包络 X/Y/Z：{' / '.join(f'{d:.6f}' for d in dims)} 米。",
                         f"- 原型：{part['original_triangles']} 三角面，{len(part['original_materials'])} 个材质槽。",
                         f"- 蓝图：`{part['blueprint']}`。", f"- 当前显示网格：`{part['visual_mesh']}`。",
                         '- 兼容安装槽：' + ', '.join('`' + s + '`' for s in part['compatible_sockets']) + '。']
        if 'bones' in part:
            pitch = next(b for b in part['bones'] if b['name'] == 'BarrelPitch')
            interface_md.append('- BarrelPitch 平移（cm）：`' + str(pitch['local']['location']) + '`。')
        else:
            interface_md.append('- 静态模型：5 组舱体 × 每组 3 个可见圆孔，共 15 孔；只有 5 个逻辑发射 Socket。用户已确认保留。')
        interface_md += ['', '| 名称 | 所属骨骼 | 网格空间位置 cm | 骨骼相对位置 cm |', '|---|---|---|---|']
        for s in part['sockets']:
            location = s.get('mesh_space', {}).get('location', s['location'])
            fmt = lambda values: '(' + ', '.join(f'{v:.6f}' for v in values) + ')'
            interface_md.append(f"| {s['name']} | {s.get('bone', '静态网格')} | {fmt(location)} | {fmt(s['location']) if 'bone' in s else '—'} |")
        interface_md += ['', '- 完整旋转、比例与来源见 [冻结快照](Source/source_snapshot_v1.json)。', '']
    (ROOT / 'InterfaceSpec_v1.md').write_text('\n'.join(interface_md), encoding='utf-8')
    for relative in ('Scripts/Art/freeze_ship_component_style_sources.py',
                     'Scripts/Blender/render_ship_component_style_prototypes.py',
                     'Scripts/Art/build_ship_component_style_review.py',
                     'Scripts/author_ship_component_rigs.py'):
        original = PROJECT / relative
        registry['preserved_evidence'].append(preserve(original, ROOT / 'Source/Tools' / original.name))
    for relative, filename in [('Progress/RequirementDocument/GuLiStrike美术规范.md', 'GuLiStrike美术规范_1.0.md'),
                                ('.agents/skills/guli-model-production/SKILL.md', 'guli-model-production_SKILL.md')]:
        registry['preserved_evidence'].append(preserve(PROJECT / relative, ROOT / 'Source/Standards' / filename))
    source_manifest = {'captured_at': snapshot['captured_at'], 'art_revision': '1.0',
                       'protected_ue_packages_unchanged': True, 'files': []}
    for folder in ('Source', 'References'):
        # Only this small, task-owned source directory is traversed. Never Content/Assets or Saved.
        for path in sorted((ROOT / folder).rglob('*')):
            if path.is_file() and path.suffix.lower() in ('.json', '.fbx', '.blend', '.png', '.txt', '.py', '.md'):
                source_manifest['files'].append({'path': str(path.relative_to(ROOT)).replace('\\', '/'),
                                                 'size': path.stat().st_size, 'sha256': sha(path)})
    write_json(ROOT / 'source_file_manifest_v1.json', source_manifest)
    write_json(ROOT / 'review_manifest_v1.json', registry)
    rows = []
    mini = []
    for index, key in enumerate(KEYS, 1):
        label, role, color, description = INFO[key]
        row = registry['parts'][key]
        dims = ' × '.join(f'{v:.2f}' for v in row['dimensions_m_xyz'])
        sheet = row['reference_sheet']
        thumbnails = ''.join(f'<a href="Source/Previews/{key}_original_{v}_v1.png"><img loading="lazy" src="Source/Previews/{key}_original_{v}_v1.png" alt="{label}原型{n}"><span>{n}</span></a>'
                             for v, n in [('front', '正面'), ('right', '侧面'), ('rear', '背面'), ('top', '俯视')])
        mini.append(f'<a class="family" href="#{key}" style="--accent:{color}"><span class="index">0{index}</span><h3>{label}</h3><p>{role}</p><img style="width:100%;display:block;margin-top:14px;border-radius:3px" src="{sheet}" alt="{label}共同风格总览"><span class="swatch" style="background:{color}"></span></a>')
        rows.append(f'''<section class="component" id="{key}" style="--accent:{color}">
<div class="section-heading"><div><span class="eyebrow">0{index} / {role}</span><h2>{label}</h2><p>{description}</p></div><span class="status">{row['design_version']} · 待审核 A</span></div>
<a class="board" href="{sheet}" target="_blank"><img src="{sheet}" alt="{label}设计{row['design_version']}：效果图与正侧背三视图" loading="lazy"></a>
<p class="image-note">点击设计图查看原尺寸。参考外观以本图为准；安装尺寸、轴心与 Socket 以冻结接口清单为准。</p>
<details><summary>查看原型对照与冻结尺寸</summary><div class="comparison"><figure><img src="Source/Previews/{key}_original_hero_v1.png" alt="{label}当前原型灰模"><figcaption>原型灰模 · 由本次 UE 读取的原始网格生成</figcaption></figure><div class="facts"><h3>保留的原型依据</h3><dl><dt>原型尺寸 X × Y × Z</dt><dd>{dims} m</dd><dt>发射接口</dt><dd>{row['sockets']} 个 Socket · {row['visual_bores']} 个可见孔</dd><dt>机械类型</dt><dd>{'静态模型' if key == 'Thor_MissilePod' else 'Root → BarrelPitch'}</dd><dt>原点与安装变换</dt><dd>原点保留 · 安装变换为单位变换</dd></dl><a href="InterfaceSpec_v1.md">完整接口清单 ↗</a><p class="small">原型几何哈希与既有基线一致。这里的尺寸是原型测量值，设计图尚不是测量图纸。</p></div></div><div class="source-views">{thumbnails}</div></details>
</section>''')
    document = '''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Ship 三组件样板 · 参考审核 A</title><style>
:root{color-scheme:light;--ink:#223949;--muted:#617583;--paper:#f4f6f5;--line:#d8e0e2}*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:var(--paper);color:var(--ink);font-family:"Segoe UI","Microsoft YaHei",sans-serif;line-height:1.65}a{color:inherit}header,main,footer{max-width:1320px;margin:auto;padding:0 40px}header{padding-top:48px}.eyebrow{font-size:12px;font-weight:700;letter-spacing:.16em;color:var(--muted)}.masthead{display:flex;align-items:center;justify-content:space-between;gap:20px;border-bottom:1px solid var(--line);padding-bottom:24px}h1{font-size:clamp(26px,3vw,44px);font-weight:650;letter-spacing:-.035em;line-height:1.25;margin:9px 0}h2{font-size:32px;line-height:1.2;margin:8px 0 16px}h3{margin:0;font-weight:650;font-size:18px}p{margin:10px 0;color:var(--muted)}.status{display:inline-block;border:1px solid #bccad0;border-radius:20px;padding:6px 14px;font-size:12px;white-space:nowrap;background:#fff}.intro{display:grid;grid-template-columns:1fr 1.1fr;gap:40px;align-items:center;margin:36px 0}.intro img{width:100%;display:block;border-radius:6px}.intro h2{font-size:26px}.palette{display:flex;gap:14px;flex-wrap:wrap;margin-top:25px}.palette div{font-size:11px;color:var(--muted)}.swatch{display:block;height:20px;width:46px;border-radius:2px;margin-bottom:5px;border:1px solid #0001}.family-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:16px;margin:28px 0 55px}.family{text-decoration:none;background:white;border:1px solid var(--line);border-top:4px solid var(--accent);border-radius:5px;padding:20px;position:relative;transition:transform .15s}.family:hover{transform:translateY(-3px)}.family .index{font-size:12px;letter-spacing:.15em;display:block;color:var(--muted);margin-bottom:9px}.family .swatch{position:absolute;right:18px;top:23px;width:26px;height:10px}.family p{font-size:13px;margin:7px 0 0}.section-heading{display:flex;align-items:center;justify-content:space-between;gap:24px;margin-bottom:22px}.section-heading h2{border-left:5px solid var(--accent);padding-left:15px}.component{scroll-margin-top:25px;padding-bottom:52px;margin-bottom:40px;border-bottom:1px solid var(--line)}.board{display:block;background:#e8eef2;border-radius:7px;overflow:hidden;border:1px solid var(--line)}.board img{width:100%;display:block}.image-note{font-size:12px;margin:12px 0 20px}details{background:#fff;border:1px solid var(--line);border-radius:5px;padding:16px 20px}summary{cursor:pointer;font-size:14px;font-weight:600}details[open] summary{margin-bottom:20px}.comparison{display:grid;grid-template-columns:1.2fr 1fr;gap:28px;align-items:center}figure{margin:0}figure img{width:100%;display:block;border-radius:3px}figcaption{font-size:12px;color:var(--muted);margin:8px 0}.facts dt{font-size:12px;color:var(--muted);margin-top:12px}.facts dd{margin:2px 0;font-weight:600;font-size:15px}.facts a{font-size:13px}.small{font-size:12px}.source-views{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-top:20px}.source-views a{text-decoration:none}.source-views img{width:100%;display:block}.source-views span{font-size:11px;color:var(--muted)}footer{padding-bottom:40px;font-size:12px;color:var(--muted)}footer a{margin-right:20px}.note{padding:18px 22px;border-left:3px solid #90aab4;background:#e9eef0;margin:22px 0 32px;font-size:14px}.note strong{font-weight:600}@media(max-width:760px){header,main,footer{padding-left:20px;padding-right:20px}.masthead,.section-heading{align-items:flex-start;flex-direction:column;gap:8px}.intro,.comparison{grid-template-columns:1fr;gap:18px}.family-grid{gap:8px}.family{padding:14px}.family .swatch{display:none}.family h3{font-size:16px}.source-views{grid-template-columns:repeat(2,1fr)}h2{font-size:26px}}@media(prefers-reduced-motion:reduce){html{scroll-behavior:auto}.family{transition:none}}
</style></head><body><header><div class="masthead"><div><span class="eyebrow">GULISTRIKE / SHIP COMPONENTS</span><h1>Ship 三组件风格样板</h1><p>参考设计 v1 · 2026.09.17 · 美术规范 1.0</p></div><span class="status">当前阶段：参考审核 A</span></div></header><main>
<section class="intro"><div><span class="eyebrow">SHARED DESIGN LANGUAGE</span><h2>同一套装甲语言，<br>三种明确的武器功能。</h2><p>以既有 Ship 为基准：清楚的剪影、层叠装甲、规则曲面与窄倒角。浅色装甲覆盖深蓝灰机械结构，功能色集中在护罩、侧甲和舱盖上。</p><div class="palette"><div><span class="swatch" style="background:#DDE6E6"></span>浅色装甲</div><div><span class="swatch" style="background:#253B4B"></span>深蓝灰结构</div><div><span class="swatch" style="background:#B98535"></span>赭黄 · 对地</div><div><span class="swatch" style="background:#2F91A4"></span>青蓝 · 对空</div><div><span class="swatch" style="background:#A45447"></span>砖红 · 导弹</div></div></div><figure><img src="References/Ship_Style_Anchor_20260916.png" alt="既有Ship风格基准"><figcaption>既有 Ship 实际渲染 · 共同风格基准</figcaption></figure></section>
<div class="note"><strong>本页审核颜色、轮廓、装甲分区与结构语言。</strong> 三件可以分别通过或修改。通过 A 的组件进入 Blender 规则几何重建；成品与运动演示将在审核 B 提交。</div><nav class="family-grid">''' + ''.join(mini) + '</nav>' + ''.join(rows) + '''</main><footer><p>当前为二维设计参考；新模型、最终 FBX 和贴图尚未交付。UE 正式导入与游戏验证未运行。</p><a href="InterfaceSpec_v1.md">冻结接口清单</a><a href="review_manifest_v1.json">参考版本与审核记录</a><a href="source_file_manifest_v1.json">源文件哈希</a></footer></body></html>'''
    (ROOT / 'Review_A_v1.html').write_text(document, encoding='utf-8')
    readme = '''# Ship 三组件风格样板制作

当前阶段：**参考审核 A，双联炮 v1、CIWS v1、Thor v3 均待用户决定。** 新成品建模、审核 B 和最终 FBX 交付尚未进行。

- [参考总览、设计图与原型对照](Review_A_v1.html)
- [冻结接口清单](InterfaceSpec_v1.md)
- [当前源快照](Source/source_snapshot_v1.json)
- [源文件与参考图哈希](source_file_manifest_v1.json)
- [审核版本清单](review_manifest_v1.json)

## 版本与范围

规范 1.0；双联炮赭黄、CIWS 青蓝、Thor 砖红。2026-09-17 用户确认 Thor 保留 5 组 × 每组 3 孔，与 5 个 Socket 分开记录。参考图由图像生成工具制作，原型灰模则来自实际 UE 网格；两者不能混记成新 Blender 成品。

`Source/FBX_static_v1/` 是本次导出的三个原型静态参考；`Source/ExistingAuthoring/` 是保留的旧炮台骨骼 FBX / Blender 源与历史检查。`Source/OriginalPrototypeInspection_v1.blend` 只含原型检查网格和摄影机。上述文件都不是本次重制成品。

原型静态网格哈希与既有基线一致；当前两套骨骼、Socket、安装变换已经直接读取。NullRHI 模式下 UE 5.7 骨骼 FBX 导出会在内部材质网格提取处断言，因此不采用该路径重复导出；旧骨骼源逐字节保留。两次失败日志和第三次成功日志均保留，未修改或保存 UE 资产。

## 后续制作

1. 按具体图片版本记录用户 A 决定；需修改的单件另存 v2。
2. 只对已通过 A 的组件制作可编辑分件、规则曲面、窄倒角和活动机构，先检查灰模与尺寸。
3. 制作默认 2K 纹理、独立内线遮罩、可关闭外轮廓与三档明暗；记录实际 LOD0 面数与材质槽。
4. 双联炮和 CIWS 保留 Root → BarrelPitch；单骨骼权重 1。展示 −15° / 0° / 30° / 75° 和连续运动，再提交 B。
5. B 通过后交付总览 Blender、可编辑分件、两套骨骼 FBX、一套静态 FBX、贴图和材质参数，使用现有回读工具验证。UE 正式接入留待后续。

## 可复现工具

- `Scripts/Art/freeze_ship_component_style_sources.py`：只读源提取，已成功快照不覆盖。
- `Scripts/Blender/render_ship_component_style_prototypes.py`：从冻结几何重现原型视图，不生产新模型。
- `Scripts/Art/build_ship_component_style_review.py`：整理实际图片、接口、哈希和总览页面。
'''
    (ROOT / 'README.md').write_text(readme, encoding='utf-8')
    print(json.dumps({'review': str(ROOT / 'Review_A_v1.html'), 'parts': list(registry['parts']),
                      'source_files': len(source_manifest['files']), 'A': 'pending', 'B': 'pending'}, ensure_ascii=False))


if __name__ == '__main__':
    main()
