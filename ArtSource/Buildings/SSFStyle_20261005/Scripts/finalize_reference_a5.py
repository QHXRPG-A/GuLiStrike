"""Lay out and validate a Stage A candidate. Freeze separately after actual visual inspection."""
from pathlib import Path
import hashlib
import html
import json
from PIL import Image, ImageDraw, ImageFont

ROOT = Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
OUT = ROOT / 'References_A_v5'
if (OUT / 'reference_manifest.json').exists():
    raise RuntimeError('Published A-v5 is frozen; create a new reference version.')
render = json.loads((OUT / 'render_manifest.json').read_text(encoding='utf-8'))
source = json.loads((ROOT / 'Source/source_manifest.json').read_text(encoding='utf-8'))
baseline = json.loads((ROOT / 'Baseline/blender_source_manifest.json').read_text(encoding='utf-8'))
src = {x['key']:x for x in source['meshes']}
base = {x['key']:x for x in baseline['assets']}
ROWS = {
 'AirBase': ('空军基地', '圆形库体、侧置控制塔、通信天线、分瓣门与基座。',
             '优化冗余分段与隐藏叠面；保留规则圆周、门体和破坏可见内构。'),
 'CloningCenter': ('克隆中心', '主舱、五个培养舱、五路软管、入口门和显示器。',
                   '整理舱壳与基座冗余；保留五舱、软管柔性及运动中可见内侧。'),
 'CommandCenter': ('指挥中心', '中心塔、环形层板、天线、支架与后部管路。',
                   '优化层板和重复小件；保留主塔、支架、天线及全部破坏分件。'),
 'MilitaryFactory': ('军工厂', '四段卷帘门、三路弯管、四角支腿与通信设备。',
                     '整理壁板和支腿冗余；保留门行程、舱内结构与配套飞行无人机。'),
 'Reactor': ('反应堆', '三向支撑、分瓣壳体、散热栅、顶盖和连杆。',
             '优化壳体与支座分段；保留三重径向结构、顶盖、连杆和活动内部件。'),
 'StrategyCenter': ('战略中心', '旋转座、定向阵列、天线、支架和基座。',
                    '整理层板和重复件；保留阵列朝向、旋转座与天线。'),
 'Floor': ('共用平台', '原外轮廓、双层台面、坡道、边界件与出入口；6m规整板缝。',
           '优化边界重复件与隐藏重叠面；保留平台尺寸、坡度和出入口。'),
 'Lamp': ('灯柱', '原支柱、弯折灯臂和灯头，保持原尺寸。', '原网格48面；三档保留必要几何，不强行削减。'),
 'Light': ('发光贴片', '原单面半透明贴片；1.97m方形、零厚度。',
           '原网格2面；正视沿贴片法线、左视为零厚度边缘、背面无正面图案，不补体积。'),
 'Drone': ('配套无人机', '原机身、导流件和两侧结构；沿用军工厂配色。',
           '仅优化隐藏与冗余面；保留飞行动画、翼形及1m级原尺寸。'),
}
BUILDINGS = ['AirBase','CloningCenter','CommandCenter','MilitaryFactory','Reactor','StrategyCenter']
ACCESSORIES = ['Floor','Lamp','Light','Drone']
BUDGET = {'AirBase':[[4300,700],[2300,400],[1050,0]], 'CloningCenter':[[2200,300],[1100,200],[500,0]],
 'CommandCenter':[[3800,600],[1900,300],[850,0]], 'MilitaryFactory':[[1800,250],[900,150],[400,0]],
 'Reactor':[[3000,500],[1500,250],[700,0]], 'StrategyCenter':[[3600,600],[1800,300],[800,0]],
 'Floor':[[6500,1000],[3500,500],[1500,0]], 'Drone':[[210,40],[110,20],[50,0]]}
FONT = Path('C:/Windows/Fonts/msyh.ttc')
BOLD = Path('C:/Windows/Fonts/msyhbd.ttc')
INK, BG, MUTED = '#1A182F', '#F3EEE5', '#62596B'

def font(size, bold=False): return ImageFont.truetype(str(BOLD if bold else FONT),size)
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def text(draw, pos, value, size=30, fill=INK, bold=False):
    draw.text(pos,value,font=font(size,bold),fill=fill)
def swatches(draw,x,y,colors,size=40):
    for i,c in enumerate(colors):
        draw.rounded_rectangle((x+i*size*1.3,y,x+i*size*1.3+size,y+size),radius=7,fill=c)
def record(path, **extra):
    row={'file':str(path.relative_to(ROOT)).replace('\\','/'),'bytes':path.stat().st_size,'sha256':sha(path),**extra}
    if path.suffix=='.png':
        im=Image.open(path); row.update({'pixels':list(im.size),'mode':im.mode})
    return row

(OUT/'Images').mkdir(exist_ok=True); (OUT/'Sheets').mkdir(exist_ok=True)
images=[]; assets=[]
view_names={'Hero':'三分之四效果图','Front':'正面正交图','Left':'左侧正交图','Back':'背面正交图','Top':'俯视补充图'}
for a in [next(x for x in render['assets'] if x['key']==key) for key in BUILDINGS+ACCESSORIES]:
    key=a['key']; label,keep,reduction=ROWS[key]
    dims=base[key]['dimensions_m']; tri=src[key]['lod0_triangles']
    theme=a['theme']
    colors=[theme[r] for r in ('Primary','Equipment','Accent','Frame')]
    if key=='Floor': colors=[render['palette_revision']['platform'],theme['Primary'],theme['Accent'],theme['Frame']]
    bones=len(src[key].get('bones',[])); clips=[x for x in source['animations'] if bones and x['skeleton']==src[key]['skeleton']]
    for v,camera in a['views'].items():
        original=OUT/camera['file']; im=Image.open(original).convert('RGB')
        assert im.size==(2048,2048),original
        d=ImageDraw.Draw(im)
        text(d,(96,54),'SSF / '+key+' · '+label,43,bold=True)
        text(d,(98,116),'参考 A v5 · '+view_names[v]+' · 待审核',29,fill=MUTED)
        swatches(d,1635,62,colors,48)
        d.line((96,164,1952,164),fill='#C8BFC5',width=2)
        text(d,(96,1748),'配色：'+theme['name']+'  |  '+ ' / '.join(colors),25,fill=MUTED)
        text(d,(96,1810),'保留：'+keep,29)
        text(d,(96,1854),'减面方向：'+reduction,27,fill=MUTED)
        dimensions=' × '.join(f'{x:.2f}' for x in dims)
        text(d,(96,1910),f'尺寸 XYZ：{dimensions} m    原本体：{tri:,} 三角面    骨骼：{bones}    动画：{len(clips)}',27)
        text(d,(96,1960),'源几何参考设计 · 本资产各视图同尺度同姿态 · 成品建模与 LOD 尚未开始',25,fill=MUTED)
        target=OUT/'Images'/f'{key}_{v}.png'; im.save(target)
        images.append(record(target,asset=key,view=v,role='canonical_reference',**{'camera':camera}))
    sheet=Image.new('RGB',(4200,4640),BG); d=ImageDraw.Draw(sheet)
    text(d,(76,42),key+' / '+label+' — 参考审核 A v5',60,bold=True)
    text(d,(80,126),theme['name']+' · 同一源模型、中立姿态与正交比例；原尺寸和结构保留。',34,fill=MUTED)
    for j,v in enumerate(['Hero','Front','Left','Back']):
        sheet.paste(Image.open(OUT/'Images'/f'{key}_{v}.png').convert('RGB'),(38+(j%2)*2076,206+(j//2)*2076))
    text(d,(78,4392),'本图板为参考设计；A 未通过，B 未开始。',39,bold=True)
    text(d,(78,4460),'灯片侧视为零厚度边缘；平台和无人机另附俯视图。' if key in ('Light','Floor','Drone') else
         '审核重点：轮廓、活动结构、配色边界、分档明暗和线条密度。',34,fill=MUTED)
    target=OUT/'Sheets'/f'{key}_Sheet_A_v5.png'; sheet.save(target)
    assets.append({'key':key,'label':label,'theme':theme,'retain':keep,'reduction_intent':reduction,
                   'source_triangles':tri,'source_bones':bones,'source_animation_count':len(clips),
                   'dimensions_m':dims,'bone_root_representation':
                   'UE Root is represented by the FBX armature object; reconstruct/verify at production export' if bones else None,
                   'sheet':record(target,asset=key,role='review_sheet'), 'lod_budgets_after_A':BUDGET.get(key),
                   'reference_scene':a})

def overview(keys,name,cols):
    rows=(len(keys)+cols-1)//cols; w=1000; h=1190
    im=Image.new('RGB',(cols*w,270+rows*h+200),BG); d=ImageDraw.Draw(im)
    text(d,(65,43),'SSF 建筑风格参考' if keys==BUILDINGS else 'SSF 配套风格参考',62,bold=True)
    text(d,(68,128),'参考 A v5 · 仅深色提亮 · 原浅色与明暗保留' if keys==BUILDINGS else
         '参考 A v5 · 深色提亮 · 原浅色保留 · 无人机随军工厂',32,fill=MUTED)
    for i,key in enumerate(keys):
        x=(i%cols)*w; y=240+(i//cols)*h
        im.paste(Image.open(OUT/'Renders'/f'{key}_Hero.png').convert('RGB').resize((w,w),Image.Resampling.LANCZOS),(x,y))
        label=ROWS[key][0]
        text(d,(x+55,y+1002),key+' · '+label,32,bold=True)
        theme=render['themes'][key]
        text(d,(x+55,y+1055),theme['name'],29,fill=MUTED)
        card_colors=[theme[r] for r in ('Primary','Equipment','Accent','Frame')]
        if key=='Floor': card_colors=[render['palette_revision']['platform'],theme['Primary'],theme['Accent'],theme['Frame']]
        swatches(d,x+690,y+1054,card_colors,37)
        text(d,(x+55,y+1110),'原尺寸：'+' × '.join(f'{v:.2f}' for v in base[key]['dimensions_m'])+' m',25,fill=MUTED)
    text(d,(65,270+rows*h+44),'各自视图保持一致比例；组合图按实际尺寸展示。参考审核通过后才开始成品建模。',26,fill=MUTED)
    p=OUT/name; im.save(p); return record(p,role='overview')
overviews=[overview(BUILDINGS,'Overview_Buildings.png',3),overview(ACCESSORIES,'Overview_Accessories.png',2)]
assembly=[]
for v in ('Hero','Top'):
    p=OUT/'Renders'/f'Assembly_{v}.png'; im=Image.open(p).convert('RGB'); d=ImageDraw.Draw(im)
    text(d,(140,82),'SSF / 六座建筑与平台、灯具、无人机组合参考',71,bold=True)
    text(d,(143,190),'参考 A v5 · 原尺寸组合 · 配色与体量关系展示',47,fill=MUTED)
    text(d,(142,3900),'此组合仅用于美术参考，不代表已批准的关卡布局。',43,fill=MUTED)
    target=OUT/f'Assembly_{v}_A_v5.png'; im.save(target); assembly.append(record(target,role='assembly_reference'))

checks=[]
for a in render['assets']:
    key=a['key']; s=src[key]; b=base[key]
    dims_error=max(abs(x/100-y) for x,y in zip(s['dimensions_cm'],b['dimensions_m']))
    tri=sum(m['triangles'] for m in b['meshes'])
    ue_names={x['name'] for x in s.get('bones',[])}
    imported_names={x['name'] for r in b['rigs'] for x in r['bones']}
    missing=sorted(ue_names-imported_names); extra=sorted(imported_names-ue_names)
    same_scale=len({round(v['ortho_scale_m'],8) for v in a['views'].values()})==1
    core={x['view'] for x in images if x['asset']==key}
    check={'asset':key,'size_max_error_m':dims_error,'source_triangles':s['lod0_triangles'],
           'blender_source_triangles':tri,'geometry_and_weights_digest_equal':a['geometry_sha256_before']==a['geometry_sha256_after'],
           'one_scale_per_asset':same_scale,'four_views':all(v in core for v in ('Hero','Front','Left','Back')),
           'ue_only_bone_identifiers':missing,'blender_only_bones':extra,
           'root_object_mapping_valid':(missing==['Root'] and not extra and all(r['name'].startswith('Root') for r in b['rigs'])) if ue_names else True}
    assert dims_error<.005 and tri==s['lod0_triangles'],check
    assert check['geometry_and_weights_digest_equal'] and same_scale and check['four_views'] and check['root_object_mapping_valid'],check
    checks.append(check)
assert len(images)==42 and sum(x['source_animation_count'] for x in assets)==22
assert len({render['themes'][key]['Primary'] for key in BUILDINGS})==6
assert all(c['source_hex'] in render['user_swatch_library'] for c in render['palette_revision']['changes'])
assert all(c['output_hex']==c['source_hex'] for c in render['palette_revision']['changes'] if not c['changed'])
assert all(c['L_star_after']>=61.99 for c in render['palette_revision']['changes'] if c['changed'])
assert render['tone_factors']==[.4,.72,1.0]
assert not source['new_dirty_content'] and not source['new_dirty_maps']
input_records=[record(p,role='user_palette_source') for p in sorted((ROOT/'Inputs').glob('*.jpg'))]
assert len(input_records)==3
validation={'success':True,'canonical_reference_images':len(images),'minimum_reference_pixels':[2048,2048],
            'building_hero_three_view_sets':6,'accessory_hero_three_view_sets':4,'extra_top_views':2,
            'source_meshes':10,'source_animations':22,'assets':checks,
            'approvals':{'A':'pending','B':'not_started'},'production_geometry_created':False,
            'new_lods_created':False,'ue_assets_saved':False,'ue_formal_import_performed':False,
            'six_distinct_primary_colors':True,'all_color_origins_from_user_cards':True,
            'lightened_derived_HEX_are_explicit':True,'only_original_dark_colors_lifted':True,'light_and_middle_HEX_preserved':True,'original_three_tone_factors_preserved':True,
            'tone_factors':render['tone_factors'],'line_ink':render['shared_ink'],
            'visual_review':'See visual_qa.json; technical image checks do not constitute user A/B approval'}
(OUT/'validation.json').write_text(json.dumps(validation,ensure_ascii=False,indent=2),encoding='utf-8')

md=['# SSF 建筑参考审核 A v5','',
    '**当前版本：SSF_Reference_A_v5。A 待审核，B 未开始。** 本套是基于原模型的参考设计，成品重制、减面、三档 LOD 和正式 UE 导入在相应审核后执行。','',
    f'![六座建筑总览]({(OUT/"Overview_Buildings.png").as_posix()})','',
    '按本轮“深色调浅，浅色别动”反馈，以A_v3原配色为基准，仅提亮深紫、深蓝、深青、深莓色及原深色框架、描线。橙色、奶油白、浅粉、浅蓝和蓝绿中间色保持原HEX；撤销A_v4全局明暗提亮，原三档着色恢复。无人机跟随军工厂，平台原深蓝灰提亮。','',
    '下表是当前实际采用的HEX。原深色派生为浅色，其余原色与着色因子锁定不改；完整原色/新色逐项对照见配色记录。','',
    '| 建筑 | 色系 | 主体 | 设备 | 点缀 | 框架 |',
    '|---|---|---|---|---|---|']
for key in BUILDINGS:
    t=render['themes'][key]
    md.append('| '+ROWS[key][0]+' | '+t['name']+' | '+' | '.join('`'+t[r]+'`' for r in ('Primary','Equipment','Accent','Frame'))+' |')
md.extend(['',
    f'本轮描线色：`{render["shared_ink"]}`；三档明暗因子恢复并保留 A_v3 的 `0.40 / 0.72 / 1.0`，不再全局抬亮浅色材质。平台主体为 `{render["palette_revision"]["platform"]}`。6m规整板缝继续作为参考设计。','',
    '三档艺术明暗共用固定光向；结构内线较细，外轮廓稍重。单图原生 2048×2048；四图板可放大查看。各资产内保持同一正交尺度与中立姿态，组合图保持真实资源尺寸。','',
    '## 六座建筑与配套图板',''])
for a in assets:
    key=a['key']; md.extend([f'### {key} · {a["label"]}','',
       f'![{a["label"]}四视图]({(ROOT/a["sheet"]["file"]).as_posix()})','',
       f'- 保留：{a["retain"]}',f'- 减面方向：{a["reduction_intent"]}',
       '- 原生单图：'+' · '.join(f'[{view_names[v]}]({(OUT/"Images"/(key+"_"+v+".png")).as_posix()})' for v in ['Hero','Front','Left','Back']),''])
    if key in ('Floor','Drone'):
        md.extend([f'[俯视补充图]({(OUT/"Images"/(key+"_Top.png")).as_posix()})',''])
md.extend(['## 原尺寸组合参考','',f'![组合效果]({(OUT/"Assembly_Hero_A_v5.png").as_posix()})','',
    f'[组合俯视]({(OUT/"Assembly_Top_A_v5.png").as_posix()})','',
    '组合仅展示体量与配色，不作为关卡布局方案。','',
    '## 结构与活动件基线','',
    '七套骨架及22个源动画已归档。每个动作采集了起点、四分之一、中点、四分之三和终点的局部骨骼姿态，用于识别活动件；这不是全部运动和动画兼容性的验收。','',
    '源FBX把每套UE的Root表示为Blender骨架对象，其余骨骼标识一致。后续成品导出必须还原并核对Root命名、层级、参考姿态和动画单位。平台源材质槽当前为WorldGridMaterial，因此本轮仅以其几何为基线，新配色在参考场景独立设计。','',
    '灯片保持源单面半透明用途，侧边为零厚度；六座的原半透明标识及其活动骨骼保留。源几何、拓扑和权重摘要在配色前后相等。','',
    '## 审核与后续','',
    '请针对 **SSF_Reference_A_v5 整套** 给出通过或需要修改的具体建筑、部件与配色。A通过后按本套设计重制可编辑分件、三档LOD和绑定；B针对实际Blender成品版本审核，B通过后再导入正式目录。','',
    '[模型制作技能](../../../.agents/skills/guli-model-production/SKILL.md)和[项目美术规范](../../../Progress/RequirementDocument/GuLiStrike美术规范.md)规定“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”。本轮依照用户指定流程停在A。','',
    f'[技术清单]({(OUT/"validation.json").as_posix()}) · [参考场景]({(OUT/"SSF_ReferenceDesign_v5.blend").as_posix()})'])
(ROOT/'Review_A_v5.md').write_text('\n'.join(md)+'\n',encoding='utf-8')
# Lightweight local gallery; no server or cloud upload is required.
cards=''.join(f'<article><h2>{a["key"]} · {a["label"]}</h2><a href="{a["sheet"]["file"]}"><img loading="lazy" src="{a["sheet"]["file"]}"></a><p>{html.escape(a["retain"])}</p><p>{html.escape(a["reduction_intent"])}</p></article>' for a in assets)
page='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>SSF 参考 A v5</title><style>body{margin:auto;max-width:1440px;padding:30px;background:#f3eee5;color:#1a182f;font:17px/1.6 system-ui,"Microsoft YaHei"}header{border-bottom:2px solid #0a6f73;padding-bottom:20px}img{width:100%;height:auto}article{margin:40px 0;padding:25px;background:#fffaf4;border-radius:12px}p{max-width:1000px}a{color:#0a6f73}nav{position:sticky;top:0;background:#f3eee5;padding:12px;z-index:2}</style><header><h1>SSF 建筑风格参考 A v5</h1><p>整套待审核。基于实际源模型的参考设计；A通过后开始成品建模，B通过后正式导入。</p><p>保留六套独立色系，仅将原深色提亮；原浅色、中间色及三档着色因子不变。点击图板查看原尺寸，单图为2048px。</p></header><nav><a href="Review_A_v5.md">完整说明</a> · <a href="References_A_v5/validation.json">技术清单</a></nav><img src="References_A_v5/Overview_Buildings.png">'''+cards+'''<h2>原尺寸组合</h2><img src="References_A_v5/Assembly_Hero_A_v5.png"><p>组合仅用于美术参考，不代表已批准的关卡布局。</p></html>'''
(ROOT/'review_A_v5.html').write_text(page,encoding='utf-8')
manifest={'version':'SSF_Reference_A_v5','date':'2026-10-05','art_revision':'1.3',
          'stage':'A_reference_submission','approvals':{'A':'pending','B':'not_started'},
          'assets':assets,'canonical_images':images,'overviews':overviews,'assembly':assembly,
          'user_palette_inputs':input_records,
          'files':[record(OUT/'SSF_ReferenceDesign_v5.blend',role='reference_scene'),
                   record(OUT/'render_manifest.json',role='camera_palette_and_part_assignments'),
                   record(OUT/'validation.json',role='technical_validation'),
                   record(ROOT/'Review_A_v5.md',role='review_entry'),record(ROOT/'review_A_v5.html',role='local_gallery')],
          'production_modeling_started':False,'ue_formal_import_performed':False}
(OUT/'candidate_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'success':True,'version':manifest['version'],'images':len(images),'sheets':len(assets),
                  'review':str(ROOT/'Review_A_v5.md')},ensure_ascii=False))
