"""Assemble the material-only original-model review from actual Blender output."""
from pathlib import Path
import datetime
import hashlib
import html
import json
import re
import shutil

ROOT=Path('D:/UE5.7/test1/ArtSource/Ships/ShipComponentStyle_20260917')
PROD=ROOT/'Production/v4'
PRE=ROOT/'Previews/v4'
PARTS={'Twin_Barrel_Turret':('双联炮','赭黄','C9903E'),'CIWS':('CIWS','青蓝','348B9F'),'Thor_MissilePod':('Thor 一级导弹舱','砖红','B16352')}
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def dump(path,data):path.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')

def main():
    reports={key:json.loads((PROD/(key+'_material_report.json')).read_text(encoding='utf-8')) for key in PARTS}
    for key,r in reports.items():
        assert r['body_geometry_unchanged'] and r['original_corner_normals_unchanged']
        assert r['stage']=='review_B_ready',key
        assert r['blend_sha256']==digest(PROD/(key+'_OriginalMesh_MaterialCandidate.blend'))
    now=datetime.datetime.now(datetime.timezone.utc).isoformat()
    scope={'schema':'guli-ship-material-scope/v1','work_id':'WORK-20260917-003','recorded_at':now,
       'current_version':'v4','geometry':'Original source body topology, coordinates, corner normals, pivots and weights retained',
       'allowed_work':['surface textures','three-tone scene lighting and shadows','internal line mask','optional render outline'],
       'user_decisions':[
          {'text':'原来的组件的模型就很好看了，你只需要改它的渲染。贴图还有框线。就好了，不需要把他们的模型给改了。','effect':'Supersedes geometry redesign; A references now color/light style only'},
          {'text':'还有三渲二的光照阴影啥的效果','effect':'Scene-driven three-tone lighting, self-shadow and cast shadow included'},
          {'text':'连接处异常','attachment':'Feedback/CIWS_connection_user_markup_20260917.png','effect':'v3 not accepted; fix material-region assignment at fixed/moving junction'}],
       'connection_diagnosis':'Some fixed faces were incorrectly colored as the moving cyan shroud by nearest-vertex classification. Uniform toon material and unchanged normals isolated the issue to paint regions.',
       'connection_fix':'Recover whole original connected parts using original rigid-bone ownership and unchanged source anchors; keep each coplanar region one paint color; rebake mask.',
       'body_geometry_modified':False,'user_B':'pending','UE_formal_validation':'not_run'}
    dump(ROOT/'material_scope_and_connection_fix_v4.json',scope)
    params={'schema':'guli-ship-material-parameters/v1','renderer':'Blender EEVEE',
       'lighting':'Diffuse BSDF -> Shader to RGB -> RGB to BW -> CONSTANT ColorRamp -> multiply BaseColor -> mix independent ink -> Emission',
       'thresholds':[0,.18,.46],'linear_tone_multipliers':[[.43,.50,.60],[.72,.79,.84],[1,1,1]],
       'palette_sRGB':{'Pearl':'DDE6E6','Slate':'4B6577','Navy':'23384A','Steel':'8298A5','Ochre':'C9903E','Cyan':'348B9F','Brick':'B16352','Ink':'213D50'},
       'texture_resolution':[2048,2048],'BaseColor':'sRGB; no baked lighting or shadows',
       'ORM':'Non-Color; R=1 AO placeholder (no baked occlusion), G=roughness, B=metallic',
       'LineMask':'Non-Color; white=ink; strength .85; separately switchable',
       'UV':'Original channels retained, SC_PaintUV and SC_LineUV added',
       'outline':'Separate OUTLINE_TOGGLE collection; mesh body untouched; CIWS .010m and large components .065m render width',
       'limitations':'Shader-to-RGB is a Blender render shader. Future UE material needs equivalent quantized light/shadow and outline logic. FBX alone cannot transfer it.',
       'parts':{key:{k:r[k] for k in ['body_triangles','outline_triangles','body_material_slots','dimensions_blender_m','uv_channels','sockets']} for key,r in reports.items()}}
    dump(PROD/'MaterialParameters_v4.json',params)
    text=['# Ship 原模型材质样板 v4','',
       '当前是实际 Blender 渲染候选，审核 B 待用户决定。主体几何、原法线和机械接口保留；没有新增装甲、护罩或倒角。',
       '', '使用 Blender EEVEE 打开各组件文件或总览文件。总览内三个 Scene 分别保持原尺寸和原点；从 Scene 选择器切换组件。',
       '', '两门炮第 0 帧为中立姿态；1–97 帧是 −15° 到 75° 再返回的演示。演示不代表已接入 UE 玩法。',
       '', '内部框线：材质节点 `Line_Strength`，0 为关闭。外轮廓：隐藏 `OUTLINE_TOGGLE` 集合。贴图均已打包，并在 `Textures/` 提供独立 2K PNG。',
       '', '原有部件区域保存在网格面属性 `OriginalComponentSeed`，便于原件选择和重涂；不通过拆面改变原模型。',
       '', '## 核对结果','', '| 组件 | 主体三角面 | 描边三角面 | 主体材质槽 | 原几何 / 原法线 |','|---|---:|---:|---:|---|']
    for key,r in reports.items():text.append(f"| {PARTS[key][0]} | {r['body_triangles']} | {r['outline_triangles']} | 1 | 一致 / 一致 |")
    text+=['','两门炮各运行 91 个角度采样，复用现有机械检查工具；详细误差和接口见各 `material_report.json`。',
       '', 'CIWS 连接处的交错三角色块来自错误分色，v4 已按原件和原骨骼归属重烘焙。诊断和用户标注保留在 v3 与 Feedback。',
       '', '审核 B 通过后才导出最终 FBX 并回读。UE 正式验证未运行。材质参数和后续 UE 重建说明见 `MaterialParameters_v4.json`。']
    (PROD/'README.md').write_text('\n'.join(text)+'\n',encoding='utf-8')
    cards=[];sections=[]
    for key,(label,color,hexvalue) in PARTS.items():
        r=reports[key]
        cards.append(f'<a class="card" href="#{key}" style="--accent:#{hexvalue}"><img src="Previews/v4/{key}_hero.png" alt="{label}实际渲染"><div><strong>{label}</strong><span>{color} · 原模型保留</span></div></a>')
        views=''.join(f'<button data-view="{view}">{name}</button>' for view,name in [('hero','效果'),('front','正视'),('right','侧视'),('rear','背视'),('top','俯视')])
        poses=''
        if key!='Thor_MissilePod':
            poses=f'<div class="motion"><video controls muted loop playsinline preload="metadata" poster="Previews/v4/{key}_pitch_30.png"><source src="Previews/v4/{key}_pitch_motion.mp4" type="video/mp4"></video><div><h3>连续俯仰</h3><p>−15° → 75° → −15°<br>固定底座、活动护罩与炮口跟随。</p><p class="dim">灰模与彩色姿态可点击查看原图。</p></div></div><div class="poses">'
            for angle in (-15,0,30,75):poses+=f'<figure><a href="Previews/v4/{key}_pitch_{angle}.png" target="_blank"><img loading="lazy" src="Previews/v4/{key}_pitch_{angle}.png" alt="{angle}度"></a><figcaption>{angle}° · <a href="Previews/v4/{key}_gray_pitch_{angle}.png" target="_blank">灰模</a></figcaption></figure>'
            poses+='</div>'
        else:poses='<p class="note">保留原型 5 组舱体 × 每组 3 孔，以及原有 5 个发射 Socket。</p>'
        sections.append(f'''<section id="{key}" data-key="{key}" style="--accent:#{hexvalue}">
          <div class="sectionhead"><div><span class="eyebrow">MATERIAL STUDY / V4</span><h2>{label}</h2></div><span class="badge">B 待审核</span></div>
          <div class="controls views">{views}</div>
          <div class="viewer"><a class="full" href="Previews/v4/{key}_hero.png" target="_blank"><img class="main" src="Previews/v4/{key}_hero.png" alt="{label}成品渲染"></a></div>
          <div class="controls modes"><button data-mode="hero" class="active">三渲二＋框线</button><button data-mode="lines_off">关闭框线</button><button data-mode="original_geometry_gray">原几何灰模</button><button data-mode="alternate_light">另一光向</button><button data-mode="small">缩小视图</button></div>
          <p class="dim">上述均为实际 Blender 渲染。更换光向时明暗与投影会随场景灯光变化。</p>
          {poses}
          <details><summary>源文件与核对数据</summary><p>主体 {r['body_triangles']} 三角面 · 独立描边 {r['outline_triangles']} 三角面 · 1 个主体材质槽 · 3 张 2K 贴图</p>
          <p>原几何与原法线一致；骨骼、权重及挂点保留。</p><a href="Production/v4/{key}_OriginalMesh_MaterialCandidate.blend">可编辑 Blender</a> · <a href="Production/v4/{key}_material_report.json">核对记录</a></details>
        </section>''')
    page='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Ship 原模型材质样板 · v4 审核</title>
    <style>
    :root{color-scheme:dark;font:16px/1.65 "Segoe UI","Microsoft YaHei",sans-serif;background:#10212c;color:#e1e9ed}*{box-sizing:border-box}body{margin:0}a{color:#8fd0df;text-decoration:none}a:hover{text-decoration:underline}header,main,footer{max-width:1240px;margin:auto;padding:40px 32px}header{padding-bottom:20px}.eyebrow{font-size:12px;letter-spacing:2px;color:#9fb2bd}h1{font-size:36px;line-height:1.3;margin:12px 0}h2{margin:4px 0;font-size:28px}h3{font-size:20px}p{max-width:850px}button{font:inherit;cursor:pointer;border:1px solid #415a6a;background:#18313f;color:#cbdde6;border-radius:6px;padding:7px 15px}button.active{background:var(--accent,#418596);color:#fff;border-color:transparent}button:hover{filter:brightness(1.15)}.badge{border:1px solid #718681;padding:4px 12px;border-radius:20px;font-size:13px;color:#b9d2c5;white-space:nowrap}.cards{display:grid;grid-template-columns:repeat(3,1fr);gap:18px}.card{background:#1e3442;color:#e0e9ed;border-top:3px solid var(--accent);border-radius:9px;overflow:hidden}.card img{display:block;width:100%;aspect-ratio:4/3;object-fit:contain}.card div{padding:16px}.card strong{font-size:20px;display:block}.card span{color:#aabec9;font-size:14px}section{margin-top:48px;padding:25px;background:#162c39;border:1px solid #314b5b;border-radius:12px;scroll-margin-top:16px}.sectionhead{display:flex;align-items:center;justify-content:space-between;gap:20px}.controls{display:flex;flex-wrap:wrap;gap:8px;margin:16px 0}.viewer{background:#758d9a;border-radius:8px;overflow:hidden}.viewer img{width:100%;display:block;aspect-ratio:4/3;object-fit:contain}.viewer img.small{width:360px;margin:auto;max-width:100%}.dim{font-size:14px;color:#a5bdc9}.note{padding:15px;border-left:3px solid #508c98;background:#193443}.motion{display:grid;grid-template-columns:2fr 1fr;gap:25px;align-items:center;margin:28px 0 15px}video{width:100%;border-radius:6px;background:#13212c}.poses{display:grid;grid-template-columns:repeat(4,1fr);gap:12px}figure{margin:0}figure img{width:100%;border-radius:5px}figcaption{font-size:14px;text-align:center}details{margin:22px 0 0;border-top:1px solid #355363;padding-top:18px}summary{cursor:pointer;color:#a8c8d8}.downloads{display:flex;gap:16px;flex-wrap:wrap}.alert{padding:16px 20px;background:#203a46;border-radius:8px}.footerline{border-top:1px solid #365162;padding-top:18px}footer{color:#a5bbc5;font-size:14px}@media(max-width:760px){header,main,footer{padding:22px 16px}h1{font-size:28px}.cards{grid-template-columns:1fr}.card{display:grid;grid-template-columns:1fr 1fr;align-items:center}.motion{grid-template-columns:1fr}section{padding:14px}.poses{grid-template-columns:repeat(2,1fr)}.badge{font-size:12px}}
    </style><header><span class="eyebrow">GULISTRIKE / SHIP COMPONENTS</span><h1>原模型保留，更新色块、光影与框线</h1><p>双联炮、CIWS、Thor 一级导弹舱 · 材质候选 v4</p><div class="alert">CIWS 连接处的三角色块已按原件归属修正；主体坐标、拓扑、原法线和机械接口保持不变。当前等待实际渲染审核 B。</div><p class="dim">已通过的参考 A 仅继续用于配色与三渲二风格。几何以用户指定的原模型为准。</p></header><main>
    <div class="cards">'''+''.join(cards)+'''</div><div class="downloads" style="margin-top:20px"><a href="Production/v4/ShipComponentStyle_OriginalMeshes_Overview.blend">总览 Blender（3 个 Scene）</a><a href="Production/v4/MaterialParameters_v4.json">材质参数</a><a href="Production/v4/README.md">使用说明</a><a href="InterfaceSpec_v1.md">原接口清单</a></div>'''+''.join(sections)+'''
    <section><h2>连接处修正</h2><p>原先把部分固定连接面归入了活动护罩的分色区，形成三角碎块和错误内线。v4 按原骨骼归属、原件连通区域和连续平面重新分色与烘焙。</p><div class="cards"><figure><img style="width:100%" src="Previews/v3/CIWS_hero.png" alt="修正前"><figcaption>修正前 v3</figcaption></figure><figure><img style="width:100%" src="Previews/v4/CIWS_hero.png" alt="修正后"><figcaption>修正后 v4</figcaption></figure><figure><img style="width:100%" src="Previews/v4/CIWS_original_geometry_gray.png" alt="原几何灰模"><figcaption>同一主体几何</figcaption></figure></div><p><a href="Feedback/CIWS_connection_user_markup_20260917.png">用户标注</a> · <a href="material_scope_and_connection_fix_v4.json">范围与修正记录</a></p></section>
    </main><footer><div class="footerline">B 通过后导出最终 FBX 并回读。UE 正式接入与验证尚未运行。Blender 的三渲二节点需在后续 UE 材质中重建，普通 FBX 不会自动携带完整光影效果。</div></footer>
    <script>
    document.querySelectorAll('section[data-key]').forEach(section=>{const key=section.dataset.key,img=section.querySelector('img.main'),link=section.querySelector('a.full');const show=(suffix)=>{const path=`Previews/v4/${key}_${suffix}.png`;img.src=path;link.href=path;img.classList.toggle('small',suffix==='small');};section.querySelectorAll('button[data-view]').forEach(button=>button.onclick=()=>{show(button.dataset.view);section.querySelectorAll('button').forEach(b=>b.classList.remove('active'));button.classList.add('active');});section.querySelectorAll('button[data-mode]').forEach(button=>button.onclick=()=>{show(button.dataset.mode);section.querySelectorAll('button').forEach(b=>b.classList.remove('active'));button.classList.add('active');});});
    </script></html>'''
    (ROOT/'Review_B_Materials_v4.html').write_text(page,encoding='utf-8')
    # Verify only this concrete review's local references, not the project tree.
    refs=set(re.findall(r'(?:src|href)="([^"#]+)"',page))
    missing=[ref for ref in refs if not ref.startswith(('http:', 'https:')) and not (ROOT/ref).exists()]
    assert not missing,missing
    for key in PARTS:
        for suffix in ('hero','front','right','rear','top','lines_off','original_geometry_gray','alternate_light','small'):
            assert (PRE/(key+'_'+suffix+'.png')).exists()
    manifest={'schema':'guli-material-review-B/v1','version':'v4','work_id':'WORK-20260917-003','assembled_at':now,'A':'approved_as_color_reference','B':'pending','stage':'review_B_ready',
          'geometry':'unchanged original sources','UE_formal_validation':'not_run','final_fbx_export':'not_run','parts':reports,'local_reference_count':len(refs)}
    files=[p for p in PROD.iterdir() if p.suffix in ('.blend','.json','.md')]+list((PROD/'Textures').glob('*.png'))+list(PRE.glob('*.png'))+list(PRE.glob('*.mp4'))
    manifest['files']=[{'path':str(p.relative_to(ROOT)).replace('\\','/'),'bytes':p.stat().st_size,'sha256':digest(p)} for p in sorted(files)]
    dump(ROOT/'review_B_material_manifest_v4.json',manifest)
    legacy_path=ROOT/'review_manifest_v1.json'
    legacy=json.loads(legacy_path.read_text(encoding='utf-8'))
    legacy['stage']='material_only_review_B';legacy['new_model_production']='cancelled_per_user_keep_original_geometry'
    legacy['material_production']='v4_ready_for_B';legacy['latest_review']='Review_B_Materials_v4.html'
    legacy['latest_scope_record']='material_scope_and_connection_fix_v4.json'
    for r in legacy['parts'].values():r['latest_material_candidate']='v4';r['B']='pending'
    dump(legacy_path,legacy)
    print(json.dumps({'review':str(ROOT/'Review_B_Materials_v4.html'),'files':len(files),'local_refs':len(refs),'geometry_unchanged':True},ensure_ascii=False))

if __name__=='__main__':main()
