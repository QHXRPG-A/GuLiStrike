"""Package existing concept PNGs and source evidence into a local review index."""
from pathlib import Path
import hashlib, html, json, struct

ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919')
PRODUCTION_RELEASED=(ROOT/'Production_v1/production_authorization.json').exists()
PRODUCTION_VALIDATED=(ROOT/'Production_v1/model_validation.json').exists()
REVIEW_FILENAME='Review_A_v2.html'
ITEMS=[
    ('SpiderMech','蜘蛛机甲 · 低模方向','深蓝灰 · 赭黄 · 黑色骨架','SpiderMech_LowPoly_Concept_ThreeViews_v2.png',['Rendered/SpiderMech_Hero.png'],'Prompts/SpiderMech_v2.txt',
     ['保留六条附肢、前方长尖足与翘尾','合并碎面、简化关节与装甲层级','原 LOD0 为 839,778 三角面；实际制作版为 19,656，见成品 B']),
    ('Mecha_01','蓝色步行机甲 · 降饱和','灰蓝 · 黑灰 · 少量原浅色饰边','Mecha_01_Concept_ThreeViews_v2.png',['Rendered/Mecha_01_Hero.png'],'Prompts/Mecha_01_v2.txt',
     ['前上风挡、侧舱盖、背面竖格栅','反关节双腿和双趾足','按用户反馈降低蓝色饱和度；效果图与三视图统一灰蓝']),
    ('Mecha_02','黄色步行机甲','金黄 · 黑灰 · 银灰窗沿','Mecha_02_Concept_ThreeViews_v1.png',['Rendered/Mecha_02_Hero.png'],'Prompts/Mecha_02_v1.txt',
     ['八边形大前窗','背面四喷口，2×2 排列','保留圆鼓舱体和原腿部比例']),
    ('Mech_Lightest','轻型机甲 · 封闭装甲舱','赭黄 · 深青绿 · 黑色 · 琥珀灯','Mech_Lightest_Concept_ThreeViews_v4.png',['Rendered/Mech_Lightest_Hero.png'],'Prompts/Mech_Lightest_v4.txt',
     ['机体右侧单枪，左侧肩甲','双背鳍、原反关节腿','驾驶区由连续实心装甲封闭；各视图不显示驾驶员、座椅、护栏或开放驾驶舱']),
    ('Weapons','三套武器模块 · 红蓝降饱和','FireWeapon 灰蓝 / 导弹武器灰红 / 机枪赭黄深青绿','Weapons_Concept_ThreeViews_v4.png',['Accessories/FireWeapon_01_Hero.png','Accessories/MissileWeapon_01_Hero.png','Accessories/Machinegun_lvl1_Hero.png'],'Prompts/Weapons_v4.txt',
     ['蓝色与红色降低饱和度，保留原色系','结构勘误：参考少画一根炮管，实际制作以三管源网格为准','保持单侧接口及底排机枪配色']),
    ('Missile','独立导弹','灰白弹体 · 深灰分界','Missile_Concept_ThreeViews_v2.png',['Accessories/Missile_01_Hero.png'],'Prompts/Missile_v2.txt',
     ['保留原弹体轮廓与十字尾翼','独立效果参考及正、侧、背视图','驾驶舱附页已按用户要求退出当前展示']),
]

def record(path):
    data=path.read_bytes()
    item={'path':path.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)}
    if path.suffix=='.png':
        assert data[:8]==b'\x89PNG\r\n\x1a\n',str(path)
        item['width'],item['height']=struct.unpack('>II',data[16:24])
    return item

inventory=json.loads((ROOT/'Source/source_inventory.json').read_text(encoding='utf-8'))
manifest={'title':'两组机甲资源风格统一参考','date':'2026-09-19','art_revision':'1.2','review_version':2,'review_page':REVIEW_FILENAME,'generation':'built-in image_gen',
    'user_instruction':'不改变色系和着色逻辑，统一项目美术风格，先出效果参考图+三视图；不显示驾驶员；SpiderMech 减面成低模；不显示驾驶舱，该区域封起来；最新反馈：红蓝饱和度过高',
    'latest_color_feedback':{'quote':'红蓝饱和度过高','affected_assets':['Mecha_01','FireWeapon_01','MissileWeapon_01'],'action':'Reduce blue/red saturation in concepts while retaining color families and existing lighting; no UE material edits','user_approval':'pending'},
    'source_roots':list(inventory['roots']),'reference_review_A':'pending','blender_review_B':'not_started','ue_style_implementation':'not_started',
    'shader_contract':'Retain original material graph logic and shading models. Default Lit body surfaces. No shader edits were performed; hidden original cockpit screens are not part of current presentation. Concept art does not verify shader behavior.',
    'spider_mesh_reduction':'not_started; low-poly 2D direction only; proposed LOD0 15000-25000 triangles from measured 839778',
    'source_capture_projection':'4 degree narrow perspective for source front/side/back; generated orthographic design drawings are conceptual.',
    'items':[], 'source_evidence':[], 'superseded_candidates':[]}
if PRODUCTION_RELEASED:
    manifest['reference_review_A']='released_for_production_by_user'
    manifest['latest_color_feedback']['user_approval']='released_for_production_by_user'
    manifest['production_authorization']='Production_v1/production_authorization.json'
    manifest['blender_review_B']='candidate_delivered_pending_review' if PRODUCTION_VALIDATED else 'in_progress'
    manifest['spider_mesh_reduction']='source-based candidate: 19656 triangles; 97.66% reduction; see Production_v1/model_validation.json'
    manifest['actual_model_review']='Production_v1/Review_B_v1.html'
sections=[]
nav=[]
for key,title,palette,filename,sources,prompt,notes in ITEMS:
    file=ROOT/'Concepts'/filename
    entry={'key':key,'title':title,'palette':palette,'design':record(file),'prompt':record(ROOT/prompt),
           'source_images':[record(ROOT/'Source'/p) for p in sources], 'notes':notes,
           'assistant_visual_review':'reviewed as 2D concept; not user approval','user_review_A':'released_for_production_by_user' if PRODUCTION_RELEASED else 'pending'}
    manifest['items'].append(entry)
    nav.append(f'<a href="#{key}">{html.escape(title)}</a>')
    orig=''.join(f'<a href="Source/{s}"><img loading="lazy" src="Source/{s}" alt="{html.escape(title)} 原资源截图"></a>' for s in sources)
    desc=' / '.join(html.escape(n) for n in notes)
    sections.append(f'''<section id="{key}"><header><div><span class="code">{key}</span><h2>{html.escape(title)}</h2></div><span class="state">参考 A · 待审核</span></header>
    <p class="palette">{html.escape(palette)}</p><p>{desc}</p>
    <div class="original"><details><summary>展开原资源实际 UE 截图</summary><div class="sources">{orig}</div></details></div>
    <a class="board" href="Concepts/{filename}"><img loading="lazy" src="Concepts/{filename}" alt="{html.escape(title)} 效果参考与正侧背三视图"></a>
    <footer><a href="Concepts/{filename}">打开原尺寸设计板 ↗</a><a href="{prompt}">完整出图提示词 ↗</a></footer></section>''')

for p in [ROOT/'Source/source_inventory.json',ROOT/'Source/Rendered/capture_report.json',ROOT/'Source/Accessories/capture_report.json',ROOT/'Source/live_cleanup_readback.json',ROOT/'Source/spider_mesh_budget.json',ROOT/'Source/Feedback_RedBlue/Mecha_01_UserFeedback.png',ROOT/'Source/Feedback_RedBlue/Weapons_UserFeedback.png']:
    manifest['source_evidence'].append(record(p))
for filename,reason in [
    ('SpiderMech_Concept_ThreeViews_v1.png','User requested a low-poly SpiderMech; v2 simplifies armor and joints'),
    ('Mecha_01_Concept_ThreeViews_v1.png','User stated blue/red saturation is too high; v2 reduces blue chroma'),
    ('Mech_Lightest_Concept_ThreeViews_v1.png','v2 removes irregular mottled paint and standardizes three-view layout'),
    ('Mech_Lightest_Concept_ThreeViews_v2.png','User requested no driver; v3 shows an empty cockpit in all views'),
    ('Mech_Lightest_Concept_ThreeViews_v3.png','User requested no cockpit either; v4 encloses the former open cockpit with solid armor'),
    ('Cockpit_Missile_Concept_ThreeViews_v1.png','Cockpit withdrawn from current presentation; Missile v2 is a standalone sheet'),
    ('Weapons_Concept_ThreeViews_v1.png','v2 restores the asymmetrical exterior mount'),
    ('Weapons_Concept_ThreeViews_v2.png','v3 corrects the decorative exterior rack to two columns and four rows'),
    ('Weapons_Concept_ThreeViews_v3.png','User stated blue/red saturation is too high; v4 reduces chroma in the upper two rows')]:
    old=ROOT/'Concepts'/filename
    if old.exists(): manifest['superseded_candidates'].append({'reason':reason,'artifact':record(old)})
manifest['source_scripts']=[{'path':p.as_posix(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in [
    Path('D:/UE5.7/test1/Scripts/inspect_mech_style_sources.py'),
    Path('D:/UE5.7/test1/Scripts/capture_mech_style_sources_worker.py'),
    Path('D:/UE5.7/test1/Scripts/capture_mech_accessories_worker.py')]]
(ROOT/'delivery_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
page='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>GuLiStrike · 机甲风格参考</title>
<style>*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:#eeece6;color:#253137;font-family:"Segoe UI","Microsoft YaHei",sans-serif;line-height:1.7}main{max-width:1440px;margin:auto;padding:44px 32px 80px}.eyebrow{color:#827458;font-size:12px;letter-spacing:3px}h1{font-size:clamp(28px,4vw,48px);line-height:1.2;margin:16px 0 20px}h2{font-size:26px;margin:0}.intro{max-width:940px;color:#566168}.note{background:#e4e7df;padding:18px 22px;border-left:3px solid #627b72;margin:26px 0}nav{display:flex;flex-wrap:wrap;gap:10px;margin:28px 0 40px}a{color:#315e71}nav a{background:#fff9;padding:8px 17px;text-decoration:none;border-radius:30px}section{padding:28px;margin:0 0 34px;background:#faf9f5;border:1px solid #d8dbd5;border-radius:12px;scroll-margin-top:12px}header{display:flex;align-items:center;justify-content:space-between;gap:20px}.code{font-size:12px;color:#788487;letter-spacing:1px}.state{font-size:12px;white-space:nowrap;background:#ede3cd;padding:4px 12px;border-radius:30px}.palette{color:#876c41;margin:12px 0 4px}section p{margin:6px 0 18px;color:#607078}.board{display:block;background:#f7f5f0;margin-top:18px}.board img{display:block;width:100%;height:auto;border:1px solid #e8e5dc}.sources{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:16px;margin:16px 0}.sources img{display:block;width:100%;height:auto}summary{cursor:pointer;color:#506469}footer{display:flex;gap:24px;flex-wrap:wrap;margin-top:14px;font-size:13px}.end{color:#6d7778;font-size:13px}@media(max-width:620px){main{padding:24px 12px}section{padding:16px}h2{font-size:21px}header{align-items:flex-start}.state{font-size:10px}.sources{grid-template-columns:1fr}}</style>
<main><div class="eyebrow">GULISTRIKE / ART DIRECTION / 2026.09.19</div><h1>机甲风格统一 · 参考设计</h1><p class="intro">四种机体、三套武器模块与独立导弹，共六张设计板。每张包含效果参考与正、侧、背三视图，可展开对应原模型截图进行核对。</p>
<div class="note"><strong>保留各自色系与原着色逻辑。</strong> 按最新反馈，蓝色机甲与武器改为低饱和灰蓝，红色武器改为低饱和灰红。SpiderMech 保留低模方向，轻型机甲保持封闭装甲舱。当前为二维参考 A，尚未实际减面、重建模型或替换 UE 资源。</div>
<nav>'''+''.join(nav)+'</nav>'+''.join(sections)+'''<p class="end">内置 ImageGen 生成；原型证据来自实际 UE 资源。概念图用于外观审核，不等于引擎材质验证。设计与成品分别审核。<br><a href="README.md">范围与制作说明</a> · <a href="delivery_manifest.json">来源、版本和 SHA256 清单</a></p></main></html>'''
if PRODUCTION_RELEASED:
    page=page.replace('参考 A · 待审核','参考 A · 已放行制作').replace('当前为二维参考 A，尚未实际减面、重建模型或替换 UE 资源。','用户已明确开始源模型制作。实际 Blender 成品与三视图见 <a href="Production_v1/Review_B_v1.html">成品 B 总览</a>；本页保留二维参考，UE 正式资源未替换。')
(ROOT/REVIEW_FILENAME).write_text(page,encoding='utf-8')
print(json.dumps({'design_sheets':len(manifest['items']),'review':str(ROOT/REVIEW_FILENAME),
                  'dimensions':{x['key']:[x['design']['width'],x['design']['height']] for x in manifest['items']}},ensure_ascii=False))
