"""Package the inspected Blender renders, feedback and reproducible art sources."""
import hashlib,json,shutil
from pathlib import Path
ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919')
OUT=ROOT/'Production_v3_InkCel'
data=json.loads((OUT/'saved_readback.json').read_text())
assert data['render_delivery_complete']
feedback=OUT/'Feedback';feedback.mkdir(exist_ok=True)
shutil.copy2('C:/Users/a/AppData/Local/Temp/codex-clipboard-7d97b050-79ab-4664-9258-5aeffadb4ad2.png',feedback/'LightMech_Rear_UserFeedback.png')
assets=[('Mech_Lightest','轻型机甲','对齐背部双侧面板、腿部条带；封闭装甲和原侧挂武器保留。'),
        ('FireWeapon_01','FireWeapon 01 · 三管','灰蓝与黑灰色系；保留源模型三管结构。'),
        ('MissileWeapon_01','MissileWeapon 01','降低饱和度的灰红装甲；分离发射口、主体与弹架轮廓。'),
        ('Machinegun_lvl1','Machinegun · Lv1','赭黄与深青绿；装甲边界、枪管和功能灯保留。'),
        ('SpiderMech','SpiderMech','完整原网格；蓝灰装甲、赭黄机构与暗色骨架。')]
views={'Hero':'效果','Front':'正视','Side':'侧视','Rear':'背视'}
def figure(path,label,cls=''):
    assert (OUT/path).is_file(),path
    return f'<figure class="{cls}"><button class="zoom" data-src="{path}" data-label="{label}"><img src="{path}" alt="{label}" loading="lazy"></button><figcaption>{label}<span>点击放大 ↗</span></figcaption></figure>'
sections=[]
for i,(key,name,desc) in enumerate(assets,1):
    figures=figure(f'Previews/{key}_Hero.png',name+' · 效果','hero')
    figures+='<div class="orthos">'+''.join(figure(f'Previews/{key}_{v}.png',views[v]) for v in ('Front','Side','Rear'))+'</div>'
    if key=='SpiderMech':figures+='<details><summary>检查原网格近景</summary>'+figure('Previews/SpiderMech_Close.png','SpiderMech · 原网格近景')+'</details>'
    sections.append(f'<section id="{key}"><div class="section-title"><span class="number">0{i}</span><div><h2>{name}</h2><p>{desc}</p></div></div><div class="asset">{figures}</div></section>')
comparison=''.join([figure('Feedback/LightMech_Rear_UserFeedback.png','用户反馈 · 旧版'),figure('Previews/Mech_Lightest_Rear.png','修订 v3 · 背视'),figure('Previews/Mech_Lightest_Rear_Paint.png','修订 v3 · 纯色核对')])
rows=''.join(f'<tr><td>{name}</td><td>{data["assets"][key]["body_evaluated_triangles"]:,}</td><td>{data["assets"][key]["outline_triangles"]:,}</td></tr>' for key,name,_ in assets)
page='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>机甲与武器 · 线稿 / 三渲二 v3</title>
<style>
:root{color-scheme:light;--paper:#f4f2ed;--ink:#20343f;--muted:#69777a;--gold:#a97829}*{box-sizing:border-box}body{margin:0;background:var(--paper);color:var(--ink);font:16px/1.65 system-ui,"Microsoft YaHei",sans-serif}a{color:inherit}header,main,footer{max-width:1380px;margin:auto;padding:40px 38px}header{padding-bottom:26px}.eyebrow{letter-spacing:.16em;font-size:12px;color:var(--gold);font-weight:700}h1{font-size:clamp(30px,4vw,58px);font-weight:650;line-height:1.25;margin:18px 0}header p{max-width:810px;color:var(--muted)}.chips{display:flex;gap:8px;flex-wrap:wrap}.chips span{border:1px solid #cbd0cb;padding:5px 12px;border-radius:99px;font-size:13px}.nav{position:sticky;top:0;background:#f4f2edf5;border-top:1px solid #cbd0cb;border-bottom:1px solid #cbd0cb;z-index:3;padding:13px 28px;display:flex;gap:25px;justify-content:center;flex-wrap:wrap;font-size:14px}.nav a{text-decoration:none}.nav a:hover{color:var(--gold)}main{padding-top:18px}section{scroll-margin-top:90px;margin-bottom:70px}.section-title{display:flex;gap:22px;align-items:baseline;border-bottom:1px solid #cbd0cb;margin-bottom:22px;padding-bottom:16px}.number{font:32px Georgia;color:var(--gold)}h2{font-size:26px;margin:0}.section-title p{margin:4px 0 0;color:var(--muted);font-size:14px}.asset{display:grid;grid-template-columns:1.18fr 1fr;gap:18px}.orthos{display:grid;grid-template-columns:repeat(3,1fr);align-content:center;gap:10px}.orthos figure{min-width:0}.hero img{max-height:650px;object-fit:contain}figure{margin:0;background:#fff;overflow:hidden;border-radius:5px}figure img{width:100%;display:block}button.zoom{appearance:none;border:0;background:transparent;width:100%;display:block;padding:0;cursor:zoom-in}figcaption{padding:10px 14px;font-size:12px;color:var(--muted);display:flex;justify-content:space-between;gap:10px}figcaption span{font-size:11px;white-space:nowrap}.orthos figcaption span{display:none}.compare{display:grid;grid-template-columns:repeat(3,1fr);gap:16px}.compare img{height:480px;object-fit:contain;background:#f8f8f5}details{grid-column:1/-1;padding:16px 20px;border:1px solid #cbd0cb;border-radius:5px}summary{cursor:pointer}details figure{max-width:860px;margin:20px auto 0}.technical{font-size:14px;color:var(--muted)}table{border-collapse:collapse;width:100%;margin:18px 0}th,td{text-align:left;border-bottom:1px solid #cbd0cb;padding:8px}footer{padding-top:0;color:var(--muted);font-size:13px}dialog{border:0;padding:0;background:#f4f2ed;max-width:96vw;max-height:96vh}dialog::backdrop{background:#0b161de0}dialog img{display:block;max-width:94vw;max-height:88vh;object-fit:contain;margin:auto}dialog .bar{display:flex;justify-content:space-between;align-items:center;padding:8px 16px}dialog button{border:0;border-radius:4px;padding:8px 14px;background:#20343f;color:#fff;cursor:pointer}@media(max-width:850px){header,main,footer{padding-left:18px;padding-right:18px}.asset{grid-template-columns:1fr}.compare{grid-template-columns:1fr}.compare img{height:420px}.nav{gap:12px;padding:10px}.orthos figcaption{padding:8px;font-size:11px}}
</style>
<header><div class="eyebrow">GULISTRIKE / MODEL ART / REVISION 03</div><h1>机甲与武器<br>线稿 · 三渲二</h1><p>修正轻型机甲的背部涂装；为指定五项补齐深色轮廓、主要结构线与亮／中／暗三档明暗。以下全部为实际 Blender 模型渲染。</p><div class="chips"><span>五项实际成品</span><span>效果 + 正 / 侧 / 背</span><span>SpiderMech 保留原网格</span><span>灰蓝 / 灰红继续降饱和</span></div></header>
<nav class="nav"><a href="#paint">背部修正</a>'''+''.join(f'<a href="#{k}">{n}</a>' for k,n,_ in assets)+'''</nav><main>
<section id="paint"><div class="section-title"><span class="number">↔</span><div><h2>轻型机甲 · 配色对齐</h2><p>背部通风口底色与腿部条带统一。纯色视图去除光照分档，便于区分涂装和受光差异。</p></div></div><div class="compare">'''+comparison+'''</div></section>'''+''.join(sections)+'''
<details class="technical"><summary>源文件与制作记录</summary><p><a href="Mechs_InkCel_v3.blend">打开 / 下载 Blender 源文件</a> · <a href="README.md">制作说明</a> · <a href="saved_readback.json">保存回读</a></p><p>内部线稿使用独立遮罩，外轮廓为无投影的独立壳。三档明暗使用固定可调美术光方向。原色系和功能发光保留；本页是 Blender 修订候选，UE 当前资源尚未同步本次材质。</p><table><thead><tr><th>资产</th><th>主体三角面（含已有修改器）</th><th>独立轮廓壳</th></tr></thead><tbody>'''+rows+'''</tbody></table><p>SpiderMech 主体仍为 839,778 三角面、533,878 顶点、299 骨骼、10 材质槽；位置、拓扑、UV、权重和导入法线与源副本一致。轮廓壳成本单独记录。</p></details></main>
<footer>Revision 03 · 实际 Blender 成品候选 · 用户视觉审核状态单独记录<br><a href="../Production_v1/Review_B_v1.html">历史 v1</a> / <a href="../Production_v2_Spider/Review_Spider_v2.html">SpiderMech 原网格 v2</a></footer>
<dialog id="viewer"><div class="bar"><span id="caption"></span><button id="close">关闭 ×</button></div><img id="full" alt=""></dialog>
<script>const d=document.querySelector('#viewer'),im=document.querySelector('#full'),cap=document.querySelector('#caption');document.querySelectorAll('.zoom').forEach(b=>b.onclick=()=>{im.src=b.dataset.src;im.alt=b.dataset.label;cap.textContent=b.dataset.label;d.showModal()});document.querySelector('#close').onclick=()=>d.close();d.onclick=e=>{if(e.target===d)d.close()};</script></html>'''
(OUT/'Review_B_v3.html').write_text(page,encoding='utf-8')
readme='''# 五项机甲与武器 · 线稿 / 三渲二修订 v3

当前[实际效果与三视图](Review_B_v3.html)、[Blender 源文件](Mechs_InkCel_v3.blend)。按用户指出的“轻型机甲背部染色不对称”和五项缺少线稿、三渲二的反馈制作；参考授权延续，不把此修改请求记为新成品视觉通过。

- 轻型机甲：修正 23 个面上的左右取色差异；296 组对应面保存回读颜色一致。保持封闭舱、无驾驶员、原单侧机枪与肩甲装配。
- FireWeapon 01（三管）、MissileWeapon 01、Machinegun Lv1、SpiderMech、轻型机甲：补充结构内线遮罩及独立外轮廓，表面改为固定可调美术光方向的三档明暗。红蓝继续使用灰蓝、灰红。
- 用户本次明确要求三渲二，取代此前仅用连续 Principled 明暗表达的方案；保留色系与功能发光，不能宣称材质逻辑完全未变。
- 内线按真实边界、较大折角及色块边界选择；过滤细小边线，避免三角拓扑线框。轻型与武器遮罩 4K，Spider 各原材料槽遮罩 2K，均打包在 blend 中。
- 轮廓壳沿同位置、同机械部件的平均法线扩张，避免 UV / 硬法线断点撕开描边；跟随原绑定，不投影。主体与轮廓面数分开统计，未声称该壳已通过游戏性能验证。
- Spider 主体保持 839,778 三角面 / 533,878 顶点，不减面，不改拓扑、UV、权重或导入法线；299 骨骼 / 10 槽保留。

[制作报告](production_report.json)、[独立保存回读](saved_readback.json)、[交付哈希清单](production_manifest.json)。脚本为项目 `Scripts/Blender/style_mechs_ink_cel_v3.py`、`finish_mechs_ink_cel_v3.py` 和 `publish_mechs_ink_cel_review.py`。

所有预览均为实际模型渲染，包括五套效果与正/侧/背视图、Spider 近景及轻型机甲纯色背视。未用生成图代替模型结果。新材质尚未导入 UE，现有 Ground 玩法资源仍为上一版；此阶段未新增动画、碰撞或性能验证。
'''
(OUT/'README.md').write_text(readme,encoding='utf-8')
manifest={'version':'3','approval_B':'pending','ue_integration_this_revision':'not_run','source_blend':'../Production_v2_Spider/SpiderMech_SourceStyle_v2.blend',
          'user_direction':['correct mirrored rear paint','five specified assets require ink and cel shading','SpiderMech: no further geometry reduction'],
          'files':{}}
for path in [OUT/'Mechs_InkCel_v3.blend',OUT/'production_report.json',OUT/'saved_readback.json',OUT/'Review_B_v3.html',OUT/'README.md',*sorted((OUT/'Previews').glob('*.png')),*sorted((OUT/'Textures').glob('*.png')),*feedback.glob('*.png')]:
    manifest['files'][str(path.relative_to(OUT)).replace('\\','/')]={'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
(OUT/'production_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'review':str(OUT/'Review_B_v3.html'),'images':len(data['review_images']),'readback':data['spider']},ensure_ascii=False))
