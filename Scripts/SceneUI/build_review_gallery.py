"""Package actual UE renders into a local, side-by-side region review gallery."""
import html
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/LocalTeamColorReview_20261008'
models=json.loads((OUT/'asset-authoring.json').read_text(encoding='utf8'))['models']
ssf_names={'SSF_AirBase':'SSF 空军基地','SSF_CloningCenter':'SSF 克隆中心','SSF_CommandCenter':'SSF 指挥中心',
           'SSF_MilitaryFactory':'SSF 军工厂','SSF_Reactor':'SSF 反应堆','SSF_StrategyCenter':'SSF 战略中心'}
cards=[]
md=['# 红蓝模型分区对照 v1','','18 个现有模型的独立派生材质预览。蓝红为候选队色，黄色为可变区标注；所有实际模型引用尚未切换。',
    '', '[打开并排审核画廊](index.html)', '', '范围：四种 Mass 兵种、八类玩法建筑、六座现有 SSF 建筑。采矿车、建造车、玩家地面机甲和 DIY 飞船模型不改色。',
    '', '先核对黄色分区，再确认蓝红参考色。敌方可配置其他非蓝色，本版先用红色参考。固定机构、线稿、功能色和已有明暗参数保留；黄色标注采用独立显示，便于识别金属区域。',
    '', '基础兵营和地图据点仍是当前玩法占位模型；前哨与工厂首版仅提出窄阵营标识条带。哨戒炮原导入父材质漫反射权重为零，本版仅在独立预览父材质中读取其已有底色纹理，原件不改。',
    '', '这些是编辑器美术预览，不是新 UI、双客户端或环境隔离的运行验收。', '']
for index,m in enumerate(models,1):
    name=m.get('display_name') or ssf_names[m['name']]
    if m['name']=='BiZhiMaoConstruction':name+='（施工体）'
    notes=m['mutable_regions']
    additional=''
    if m.get('preview_only_repair'):additional='<p class="exception">预览例外：哨戒炮原导入材质漫反射权重为零，独立副本读取已有底色纹理；正式原件保留。</p>'
    figures=[]
    for key,label in [('blue','蓝方参考'),('red','红方参考'),('regions','黄色：可变区')]:
        relative='Renders/'+m['name']+'_'+key+'.png'
        if not (OUT/relative).is_file():raise FileNotFoundError(relative)
        figures.append(f'<figure class="{key}"><figcaption>{label}</figcaption><a href="{relative}" target="_blank"><img src="{relative}" alt="{html.escape(name)} · {label}" loading="lazy"></a></figure>')
    cards.append(f'<article data-group="{m["group"]}" id="{m["name"]}"><header><span class="number">{index:02}</span><h2>{html.escape(name)}</h2><span class="group">{m["group"]}</span></header><p>{html.escape(notes)}</p>{additional}<div class="comparison">{"".join(figures)}</div><footer><a href="Configs/{m["name"]}.json">配色配置与材质路径</a><span>同模型 · 同姿态 · 同相机 · 运行时未接入</span></footer></article>')
    md.extend([f'## {index:02} · {name}', '', notes, '',
        f'[蓝色原图](Renders/{m["name"]}_blue.png) · [红色原图](Renders/{m["name"]}_red.png) · [黄色分区](Renders/{m["name"]}_regions.png) · [配色配置](Configs/{m["name"]}.json)', ''])
page='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>GuLiStrike · 红蓝模型分区审核</title>
<style>*{box-sizing:border-box}body{margin:0;background:#11171e;color:#e8eef5;font:15px/1.65 "Segoe UI","Microsoft YaHei",sans-serif}main{max-width:1480px;margin:auto;padding:34px 28px}h1{font-size:30px;line-height:1.3;margin:0 0 14px}p{margin:10px 0;color:#bfcbd7}.intro{max-width:1000px}.status{display:inline-block;color:#ffd37d;border:1px solid #786137;padding:4px 12px;border-radius:20px;margin-bottom:16px}nav{position:sticky;top:0;background:#11171ef5;z-index:2;padding:14px 0;display:flex;gap:8px}button{font:inherit;color:#ccd7e1;background:#1a2430;border:1px solid #344252;padding:8px 18px;border-radius:7px;cursor:pointer}button.active{background:#245080;border-color:#6ea5e9;color:white}article{margin:20px 0 32px;border:1px solid #314052;border-radius:12px;background:#17202a;padding:18px}header{display:flex;align-items:center;gap:12px}h2{margin:0;font-size:22px}.number{color:#7d98b1}.group{margin-left:auto;font-size:13px;color:#90a9bf}.comparison{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}figure{margin:0;background:#050708;border:1px solid #344355;border-radius:8px;overflow:hidden}figcaption{padding:9px 12px;background:#202c3a;font-weight:600}.blue figcaption{border-left:5px solid #2877db}.red figcaption{border-left:5px solid #d7534d}.regions figcaption{border-left:5px solid #ffd032}img{display:block;width:100%;height:auto}footer{display:flex;gap:12px;justify-content:space-between;flex-wrap:wrap;font-size:13px;color:#8aa0b4;padding-top:12px}a{color:#8dbdff}.exception{color:#d1ad79;font-size:13px}article[hidden]{display:none}@media(max-width:680px){main{padding:18px 12px}h1{font-size:24px}.comparison{gap:5px}article{padding:10px}figcaption{font-size:12px;padding:7px 5px}nav{gap:5px}button{padding:6px 9px}}
</style><main><div class="status">候选 v1 · 等待可变区确认 · 正式模型引用未切换</div><h1>红蓝模型分区对照</h1><div class="intro"><p>4 种 Mass 兵种 + 8 类玩法建筑 + 6 座现有 SSF 建筑，共 18 个模型。每行蓝红并排，右侧黄色标出候选可变区；点击图片可查看原图。</p><p>机械结构、线稿、固定功能色、几何、动画与 LOD 沿用现有版本。SSF 复用现有 Accent 队色遮罩；兵营、据点占位件及未标记建筑先提供窄阵营条带候选。黄色用于分区审核。</p><p>这是编辑器美术预览。场景 UI 新代码未编译，环境与双客户端效果待玩家验证。</p></div><nav><button class="active" data-filter="all">全部 18</button><button data-filter="Mass">兵种 4</button><button data-filter="Building">玩法建筑 8</button><button data-filter="SSF">SSF 6</button></nav>'''+''.join(cards)+'''</main><script>document.querySelectorAll('button[data-filter]').forEach(b=>b.onclick=()=>{document.querySelectorAll('button[data-filter]').forEach(x=>x.classList.toggle('active',x===b));document.querySelectorAll('article').forEach(a=>a.hidden=b.dataset.filter!=='all'&&a.dataset.group!==b.dataset.filter)});</script></html>'''
(OUT/'index.html').write_text(page,encoding='utf8')
(OUT/'REVIEW.md').write_text('\n'.join(md),encoding='utf8')
print('Review gallery:',len(models),'models /',len(models)*3,'actual renders')
