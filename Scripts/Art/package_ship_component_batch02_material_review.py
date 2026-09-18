"""Collect actual Blender candidates, parameters and media for user review B."""
from pathlib import Path
import hashlib
import json
import struct

PROJECT=Path(__file__).resolve().parents[2]
ROOT=PROJECT/'ArtSource/Ships/ShipComponentStyle_20260917/Batch02'
VERSION='v2'
OUT=ROOT/'Production'/VERSION
PRE=ROOT/'Previews'/VERSION
PARTS=[('Autocannon','自动炮','赭黄'),('Triple_Barrel_Turret','三联炮','赭黄'),('Single_Barrel_Turret','单管炮','青蓝')]
VIEWS=[('hero','效果'),('front','正视'),('right','右侧'),('rear','背视')]
# Display crops only: original approved PNG files stay byte-for-byte intact.
CROPS={
 'Autocannon':[[24,80,735,483],[777,80,736,483],[24,583,735,407],[777,583,736,407]],
 'Triple_Barrel_Turret':[[6,87,738,550],[746,87,696,550],[6,640,738,418],[746,640,696,418]],
 'Single_Barrel_Turret':[[18,87,688,548],[713,87,718,548],[18,640,688,407],[713,640,718,407]],
}

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def artifact(path):
    path=Path(path)
    d={'path':path.relative_to(ROOT).as_posix(),'bytes':path.stat().st_size,'sha256':sha(path)}
    if path.suffix=='.png':
        with path.open('rb') as f:header=f.read(24)
        d['resolution']=list(struct.unpack('>II',header[16:24]))
    return d
def dump(path,data):path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def figure(path,label):
    return f'<figure><a class="zoom" href="{path}" data-label="{label}"><img src="{path}" alt="{label}" loading="lazy"></a><figcaption>{label}</figcaption></figure>'

def main():
    approval=json.loads((ROOT/'approval_A_20260917.json').read_text(encoding='utf-8'))
    readback=json.loads((OUT/'saved_blender_readback.json').read_text(encoding='utf-8'))
    assert approval['decision']=='approved'
    config={}
    manifest={'schema':'gulistrike-art-material-review/v1','date':'2026-09-17','work_id':'WORK-20260917-004',
        'candidate_version':VERSION,'art_revision':'1.0','A':'approved','B':'pending',
        'scope':'Original mesh surface treatment; actual Blender EEVEE renders; FBX export after B approval',
        'approval_A':artifact(ROOT/'approval_A_20260917.json'),
        'overview_blend':artifact(OUT/'ShipComponentStyle_Batch02_Overview.blend'),
        'saved_blender_readback':artifact(OUT/'saved_blender_readback.json'),
        'motion_video_readback':artifact(OUT/'motion_video_readback.json'),
        'UE_formal_validation':'not_run','FBX_export':'not_run_await_B','parts':{}}
    sections=[]
    for n,(key,label,accent) in enumerate(PARTS,1):
        rp=OUT/f'{key}_material_report.json'
        report=json.loads(rp.read_text(encoding='utf-8'))
        blend=OUT/f'{key}_OriginalMesh_MaterialCandidate.blend'
        assert sha(blend)==report['blend_sha256']==readback[key]['blend_sha256']
        assert report['original_geometry_sha256']==report['finished_body_geometry_sha256']
        assert report['body_geometry_unchanged'] and report['original_corner_normals_unchanged']
        assert report['motion']['status']=='passed' and report['motion']['single_bone_weight_one']
        assert readback[key]['packed_2K_textures']==3
        ref=approval['parts'][key]['reference_sheet']
        assert sha(ROOT/ref['path'])==ref['sha256']
        textures=[artifact(OUT/'Textures'/f'T_SC_{key}_{suffix}_2K.png') for suffix in ('BaseColor','ORM','LineMask')]
        assert all(t['resolution']==[2048,2048] for t in textures)
        media=[artifact(PRE/f'{key}_{v}.png') for v in ('hero','front','right','rear','top','original_geometry_gray','lines_off','alternate_light','small')]
        media += [artifact(PRE/f'{key}_{kind}_{angle}.png') for kind in ('pitch','gray_pitch') for angle in (-15,0,30,75)]
        video=artifact(PRE/f'{key}_pitch_motion.mp4')
        assert video['bytes']>10000
        material={
            'palette_sRGB':{'Pearl':'DDE6E6','Slate':'4B6577','Navy':'23384A','Steel':'8298A5','Ochre':'C9903E','Cyan':'348B9F','Ink':'213D50'},
            'render_engine':'BLENDER_EEVEE','view_transform':'Standard','look':'None',
            'shader':'Diffuse -> ShaderToRGB -> luminance -> constant 3-step ramp; multiply BaseColor; independent unlit ink mix',
            'three_tone_thresholds':report['toon_thresholds'],'three_tone_multipliers_linear':report['toon_multipliers_linear'],
            'line_strength':report['line_strength'],'line_halfwidth_m':report['internal_line_halfwidth_m'],
            'outline_width_m':report['outline_width_m'],'outline_collection':'OUTLINE_TOGGLE','outline_export':False,
            'UV_channels':report['uv_channels'],'paint_UV':'SC_PaintUV','line_UV':'SC_LineUV',
            'editable_face_attribute':'SC_PaintPaletteIndex','editable_corner_color':'SC_EditablePaintColor',
            'BaseColor':'sRGB; unlit color only, no baked light or shadow','ORM':'Non-Color; R=1 AO, G=roughness, B=metallic',
            'LineMask':'Non-Color; structural boundaries only, coplanar triangle diagonals omitted',
            'lighting':'Studio_Key sun energy 2; scene lighting and self-shadow drive tone bands',
            'frame_0':'neutral pose','frames_1_97':'Review animation only; 24 fps; -15 to +75 to -15 degrees',
            'FBX_material_note':'EEVEE toon nodes and outline helpers require later shader reconstruction; retain portable textures and parameters.',
        }
        dump(OUT/f'{key}_material_parameters.json',material)
        manifest['parts'][key]={'label':label,'reference':ref,'reference_version':approval['parts'][key]['reference_version'],
            'candidate_version':VERSION,'blend':artifact(blend),'report':artifact(rp),'textures':textures,'previews':media,'motion_video':video,
            'parameters':artifact(OUT/f'{key}_material_parameters.json'),'paint_regions':artifact(OUT/f'{key}_paint_regions.json'),
            'body_triangles':report['body_triangles'],'body_material_slots':1,'source_geometry_unchanged':True,
            'source_normals_unchanged':True,'bones':['Root','BarrelPitch'],'motion':report['motion'],
            'socket_count':len(report['sockets']),'B':'pending'}
        config[key]={'label':label,'ref':ref['path'],'refSize':ref['resolution'],'crops':CROPS[key],
                     'views':{v:f'Previews/{VERSION}/{key}_{v}.png' for v,_ in VIEWS}}
        buttons=''.join(f'<button type="button" data-view="{v}" aria-pressed="{str(v=="hero").lower()}">{name}</button>' for v,name in VIEWS)
        poses=''.join(figure(f'Previews/{VERSION}/{key}_pitch_{angle}.png',f'{angle}°') for angle in (-15,0,30,75))
        gray=''.join(figure(f'Previews/{VERSION}/{key}_gray_pitch_{angle}.png',f'灰模 {angle}°') for angle in (-15,0,30,75))
        other=''.join(figure(f'Previews/{VERSION}/{key}_{v}.png',name) for v,name in [
            ('original_geometry_gray','原网格灰模'),('top','俯视补充'),('lines_off','关闭内线与外轮廓'),('alternate_light','更换光照方向'),('small','缩小视图')])
        sections.append(f'''<section id="{key}" class="asset" data-key="{key}">
<header class="asset-head"><div><p class="eyebrow">COMPONENT {n:02}</p><h2>{label}<small>材质 {VERSION}</small></h2></div><span class="chip">{accent} · B 待审核</span></header>
<div class="viewbar">{buttons}</div><div class="compare">
<figure><div class="imagebox"><a class="refcrop zoom" href="{ref['path']}" data-label="{label}已审参考完整图板"><img class="refimage" src="{ref['path']}" alt="{label}已审参考局部展示"></a></div><figcaption>已审参考 A · {approval['parts'][key]['reference_version']} <span class="view-name">效果</span></figcaption></figure>
<figure><a class="actual zoom" href="Previews/{VERSION}/{key}_hero.png" data-label="{label}实际Blender效果"><img src="Previews/{VERSION}/{key}_hero.png" alt="{label}实际Blender渲染"></a><figcaption>实际 Blender 渲染 · <span class="view-name">效果</span></figcaption></figure></div>
<details><summary>俯仰演示与四个检查角度</summary><video controls preload="metadata" poster="Previews/{VERSION}/{key}_pitch_30.png" src="Previews/{VERSION}/{key}_pitch_motion.mp4"></video><p class="muted">−15° → 75° → −15° 连续运动，24 fps。视频与姿态图均为实际 Blender 渲染。</p><div class="gallery poses">{poses}</div><div class="gallery poses">{gray}</div></details>
<details><summary>灰模、框线开关与光影对照</summary><div class="gallery">{other}</div></details>
<details><summary>可编辑源与材质资料</summary><p class="facts">主体 {report['body_triangles']} 三角面 · 1 个材质槽 · 3 张 2K 贴图 · {len(report['sockets'])} 个炮口 · Root → BarrelPitch</p><p class="muted">原网格和原法线哈希保持一致；逐度检查覆盖 −15° 至 75°，单骨骼权重为 1，固定部分与炮口跟随通过核对。</p><div class="links"><a href="Production/{VERSION}/{key}_OriginalMesh_MaterialCandidate.blend">单件 Blender</a><a href="Production/{VERSION}/{key}_material_parameters.json">材质参数</a><a href="Production/{VERSION}/{key}_paint_regions.json">可编辑分色记录</a><a href="Production/{VERSION}/{key}_material_report.json">技术核对</a></div></details>
</section>''')
    dump(OUT/'Review_B_Manifest.json',manifest)
    nav=''.join(f'<a href="#{key}">{name}</a>' for key,name,_ in PARTS)
    page='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Ship 第二批 · 实际成品审核 B</title><style>
:root{color-scheme:light;--ink:#213d50;--muted:#647b89;--line:#d0dce2;--navy:#23384a}*{box-sizing:border-box}html{scroll-behavior:smooth;scroll-padding-top:78px}body{margin:0;background:#f3f6f6;color:var(--ink);font:16px/1.65 'Segoe UI','Microsoft YaHei',sans-serif}main{max-width:1450px;margin:auto;padding:0 28px 45px}a{color:inherit}header.intro{padding:36px 0 20px}.eyebrow{font-size:11px;letter-spacing:.16em;color:var(--muted);font-weight:750;margin:0 0 7px}h1{font-size:34px;line-height:1.25;letter-spacing:-.03em;margin:0 0 13px}h2{font-size:26px;margin:0;line-height:1.3}h2 small{font-size:13px;font-weight:500;color:var(--muted);margin-left:12px}p{margin:9px 0}.lead{font-size:15px;max-width:900px;color:#536d7c}.nav{display:flex;gap:10px;position:sticky;top:0;z-index:2;background:#f3f6f6f5;backdrop-filter:blur(10px);border-block:1px solid var(--line);padding:10px 0}.nav a{border:1px solid var(--line);border-radius:5px;padding:5px 16px;font-size:14px;background:white;text-decoration:none}.nav a:hover{background:var(--navy);color:white}.asset{padding-top:32px}.asset-head{display:flex;align-items:center;justify-content:space-between;gap:14px}.chip{border:1px solid #9dafbb;border-radius:30px;font-size:12px;white-space:nowrap;padding:4px 12px}.viewbar{display:flex;gap:6px;margin:20px 0 12px}.viewbar button{font:inherit;font-size:13px;color:var(--ink);border:1px solid var(--line);background:white;border-radius:4px;padding:5px 16px;cursor:pointer}.viewbar button[aria-pressed=true]{background:var(--navy);color:white;border-color:var(--navy)}.compare{display:grid;grid-template-columns:1fr 1fr;gap:16px}figure{margin:0;min-width:0}.imagebox,.actual{aspect-ratio:4/3;background:#a8bdc7;border:1px solid #94a9b4;border-radius:6px;overflow:hidden;display:flex;align-items:center;justify-content:center}.actual img{width:100%;display:block}.refcrop{position:relative;display:block;width:100%;overflow:hidden}.refimage{position:absolute;max-width:none;height:auto;display:block}.zoom{cursor:zoom-in}figcaption{font-size:12px;color:var(--muted);margin:7px 0}.gallery{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:14px;margin:18px 0}.gallery.poses{grid-template-columns:repeat(4,minmax(0,1fr))}.gallery img{width:100%;display:block;border-radius:5px}.muted{font-size:12px;color:var(--muted)}.facts{font-size:13px}details{border-bottom:1px solid var(--line);padding:15px 0}summary{cursor:pointer;font-weight:650;font-size:14px}.links{display:flex;gap:18px;flex-wrap:wrap;font-size:13px;margin-top:14px}video{display:block;width:min(100%,960px);margin:18px auto;background:#24394a;border-radius:6px}.note{border:1px solid var(--line);background:white;padding:20px 24px;margin-top:32px;border-radius:7px}footer{font-size:12px;color:var(--muted);margin-top:22px}dialog{width:min(98vw,1700px);max-width:98vw;max-height:97vh;padding:12px;border:1px solid #8196a1;border-radius:8px;background:#e5edef;color:var(--ink)}dialog::backdrop{background:#0c1e2bdf}dialog img{max-width:100%;max-height:84vh;display:block;margin:auto;object-fit:contain}dialog header{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;gap:12px}dialog button{font:inherit;cursor:pointer;background:white;border:1px solid #9dafbb;border-radius:4px;padding:3px 13px}@media(max-width:700px){main{padding:0 14px 30px}h1{font-size:27px}h2{font-size:23px}.compare{grid-template-columns:1fr}.gallery,.gallery.poses{grid-template-columns:1fr 1fr}.nav{gap:5px}.nav a{padding:5px 12px}.viewbar button{padding:5px 14px}.chip{font-size:10px;padding:4px 8px}}
</style><main><header class="intro"><p class="eyebrow">GULISTRIKE / SHIP / BATCH 02</p><h1>三组件实际材质成品</h1><p class="lead">原模型上的贴图、框线与三渲二光影已完成。切换视角对照已审参考，并查看灰模、换光效果和连续俯仰演示。</p><p class="muted">审核 B · 材质候选 v2 · 2026.09.17</p></header><nav class="nav">__NAV__</nav>__SECTIONS__
<section class="note"><p><strong>可编辑 Blender 源</strong></p><div class="links"><a href="Production/v2/ShipComponentStyle_Batch02_Overview.blend">三件总览 .blend</a><a href="Production/v2/Review_B_Manifest.json">成品与来源清单</a><a href="approval_A_20260917.json">审核 A 通过记录</a><a href="Review_A_v1.html">完整参考图板</a></div><p class="muted">每件保留原几何、法线、安装原点和 Root → BarrelPitch 接口。色块与内线使用独立 UV；外轮廓位于 OUTLINE_TOGGLE，可单独关闭。单件文件和总览均内嵌贴图。</p><p class="muted">本页是实际 Blender 成品审核。审核 B 通过后导出最终 FBX；UE 正式接入另行安排。参考栏仅在网页中局部展示原设计板，原图文件未修改。</p></section><footer>规范 1.0 · A 已通过 / B 待用户审核 · UE 正式验证未运行</footer></main>
<dialog id="viewer"><header><span id="view-title"></span><button id="close" type="button">关闭</button></header><img id="view-image" alt=""></dialog><script>const items=__CONFIG__;const names={hero:'效果',front:'正视',right:'右侧',rear:'背视'},order=['hero','front','right','rear'];
function show(section,view){const d=items[section.dataset.key],box=d.crops[order.indexOf(view)],[x,y,w,h]=box,crop=section.querySelector('.refcrop'),im=section.querySelector('.refimage');crop.style.aspectRatio=w+'/'+h;im.style.width=(d.refSize[0]/w*100)+'%';im.style.left=(-x/w*100)+'%';im.style.top=(-y/h*100)+'%';const a=section.querySelector('.actual');a.href=d.views[view];a.dataset.label=d.label+'实际Blender · '+names[view];a.querySelector('img').src=d.views[view];a.querySelector('img').alt=a.dataset.label;section.querySelectorAll('.view-name').forEach(e=>e.textContent=names[view]);section.querySelectorAll('[data-view]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.view===view)))}
document.querySelectorAll('.asset').forEach(s=>{show(s,'hero');s.querySelectorAll('[data-view]').forEach(b=>b.addEventListener('click',()=>show(s,b.dataset.view)))});const dlg=document.getElementById('viewer'),pic=document.getElementById('view-image');document.querySelectorAll('a.zoom').forEach(a=>a.addEventListener('click',e=>{e.preventDefault();pic.src=a.getAttribute('href');pic.alt=a.dataset.label||'';document.getElementById('view-title').textContent=pic.alt;dlg.showModal()}));document.getElementById('close').addEventListener('click',()=>dlg.close());dlg.addEventListener('click',e=>{if(e.target===dlg)dlg.close()});</script></html>'''
    page=page.replace('__NAV__',nav).replace('__SECTIONS__','\n'.join(sections)).replace('__CONFIG__',json.dumps(config,ensure_ascii=False))
    page=page.replace('</style>','@media(max-width:700px){.compare>figure:first-child{order:2}.compare>figure:last-child{order:1}}</style>')
    page=page.replace('<a href="approval_A_20260917.json">','<a href="Production/v2/motion_video_readback.json">俯仰视频回读</a><a href="approval_A_20260917.json">')
    review_path=ROOT/f'Review_B_{VERSION}.html'
    review_path.write_text(page,encoding='utf-8')
    dump(ROOT/f'review_B_package_validation_{VERSION}.json',{'date':'2026-09-17','technical_status':'passed',
        'A':'approved','B':'pending','review':artifact(review_path),'manifest':artifact(OUT/'Review_B_Manifest.json'),
        'verified':'3 saved body geometry hashes and normal checks; 9 packed/external 2K textures; 3 existing 91-angle motion audits; 3 decoded 97-frame videos; media paths and hashes',
        'UE_formal_validation':'not_run','FBX_export':'await_B'})
    print(json.dumps({'review':str(review_path),'candidate':VERSION,'parts':3,'A':'approved','B':'pending'},ensure_ascii=False))

if __name__=='__main__':main()
