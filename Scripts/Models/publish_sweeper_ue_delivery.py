"""Publish the real UE views, scope-specific readbacks and local delivery gallery."""
import ast
import hashlib
import json
import re
from html import escape
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/SweeperTeamColor_v1_20261008'
UE=OUT/'UE'


def read(path):return json.loads(path.read_text(encoding='utf8'))


def main():
    required=['formal-ue-import.json','datatable-saved-readback.json','UE/formal-view-readback.json',
              'UE/fixed-region-readback.json','UE/runtime-local-team-readback.json',
              'UE/runtime-actual-batch-captures.json','UE/runtime-tick-recovery.json','UE/acceptance-scene-readback.json']
    for name in required:
        if not read(OUT/name).get('success'):raise RuntimeError('Incomplete evidence: '+name)
    source_checks=[]
    for name in ['import_sweeper_orange_team.py','promote_sweeper_catalog.py','validate_model_catalog_in_editor.py',
                 'import_sweeper_catalog_in_editor.py','capture_sweeper_formal_views.py','begin_sweeper_runtime_review.py',
                 'readback_sweeper_runtime.py','capture_sweeper_live_batches.py','prepare_sweeper_acceptance_scene.py',
                 'readback_sweeper_acceptance_scene.py','review_sweeper_orange_candidate.py','publish_sweeper_ue_delivery.py']:
        p=ROOT/'Scripts/Models'/name
        ast.parse(p.read_text(encoding='utf8'),filename=str(p))
        source_checks.append({'script':str(p.relative_to(ROOT)),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'syntax':'passed'})
    model=next(r for r in read(ROOT/'Data/Json/DT_GuLiStrikeModels_Models.json') if r['Id']==1005)
    params=[r for r in read(ROOT/'Data/Json/DT_GuLiStrikeModels_MaterialParameters.json') if r['ModelId']==1005 and r['Driver']=='CPD']
    regions=[r for r in read(ROOT/'Data/Json/DT_GuLiStrikeModels_ColorRegions.json') if r['ModelId']==1005 and r['PaintRole'] in [3,4,7]]
    if {(p['ParameterKey'],p['CustomDataIndex'],p['Scope']) for p in params}!={('TeamPrimary',8,'Existing'),('TeamEnabled',16,'Existing')}:
        raise RuntimeError('Sweeper maintained bindings differ.')
    if len(regions)!=1 or regions[0]['MaskSource']!=read(OUT/'formal-ue-import.json')['mask']:
        raise RuntimeError('Sweeper maintained mask differs.')
    rejected=[r for r in read(ROOT/'Data/Json/DT_GuLiStrikeModels_Models.json') if r['Id'] in [2004,2007]]
    if any(r['bTeamColorEnabled'] for r in rejected):raise RuntimeError('Canceled placeholder repaint still enabled.')
    (UE/'source-static-review.json').write_text(json.dumps({'success':True,'python_syntax':source_checks,
        'model_id':1005,'maintained_parameter_keys':['TeamPrimary','TeamEnabled'],'uv_region':regions[0],
        'canceled_models_disabled':[r['Id'] for r in rejected],'native_source_changed':False,
        'source_excel_sha256':hashlib.sha256((ROOT/'Data/Excel/GuLiStrikeModels.xlsx').read_bytes()).hexdigest()},ensure_ascii=False,indent=2),encoding='utf8')
    sections=[]
    for view,title in [('hero','效果图'),('front','正视'),('left','左侧'),('back','后视'),('top','俯视队色')]:
        cards=[]
        for side,label in [('Original','原橙色回放'),('Blue','己方蓝 #6AA4BE'),('Red','敌方红 #A34053'),('Mask','白色为可变区域')]:
            file=f'Images/{side}_LOD0_{view}.png'
            cards.append(f'<figure><a href="{file}" target="_blank"><img src="{file}" alt="{escape(title+label)}" loading="lazy"></a><figcaption>{label}</figcaption></figure>')
        sections.append(f'<h2>{title}</h2><div class="grid">'+''.join(cards)+'</div>')
    for side,label in [('Blue','蓝方原生三档LOD'),('Red','红方原生三档LOD')]:
        cards=[f'<figure><a href="Images/{side}_LOD{i}_hero.png" target="_blank"><img src="Images/{side}_LOD{i}_hero.png" loading="lazy"><figcaption>LOD{i} · {count} 三角</figcaption></a></figure>' for i,count in enumerate([5456,1363,220])]
        sections.append(f'<h2>{label}</h2><div class="three">'+''.join(cards)+'</div>')
    live=read(UE/'runtime-actual-batch-captures.json')
    cards=[]
    for entry in sorted(live['captures'],key=lambda r:(r['player_actual_team'],r['relation'])):
        label=f"真实玩家阵营{entry['player_actual_team']} · {'己方蓝' if entry['relation']=='own' else '敌方红'} · 对象真实阵营{entry['unit_actual_team']}"
        file=entry['file']
        cards.append(f'<figure><a href="{file}" target="_blank"><img src="{file}" alt="{escape(label)}" loading="lazy"></a><figcaption>{label}</figcaption></figure>')
    sections.append('<h2>实际Q召唤后的双方客户端</h2><div class="two">'+''.join(cards)+'</div>')
    html='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>扫荡者 UE 正式入库与自动队色</title><style>body{margin:28px;background:#eeeeeb;color:#2c3735;font:16px system-ui;line-height:1.65}h1{font-size:28px}h2{font-size:21px;margin-top:30px}.grid,.three,.two{display:grid;gap:12px}.grid{grid-template-columns:repeat(4,1fr)}.three{grid-template-columns:repeat(3,1fr)}.two{grid-template-columns:repeat(2,1fr)}figure{margin:0;background:white;border:1px solid #bbb}img{width:100%;display:block}figcaption{padding:9px}a{color:#274e61}@media(max-width:900px){.grid{grid-template-columns:repeat(2,1fr)}}code{overflow-wrap:anywhere}</style>
<h1>扫荡者 · 正式 UE · 原橙色区域自动队色</h1><p>2026-10-09 用户明确放行“导入 UE、接入自动改色。”。原橙色区域使用 TeamPrimary，其他原涂装、三档明暗、刚性动画与无线稿例外保留。以下均为真实UE渲染；静态对照锁定曝光并关闭捕获相机的环境与时间效果，运行图保留真实游戏环境。</p>
<p><a href="../formal-import-authorization.json">具体版本放行</a> · <a href="../formal-ue-import.json">正式材质与原网格哈希</a> · <a href="../datatable-saved-readback.json">三表保存回读</a> · <a href="runtime-local-team-readback.json">两客户端实际参数</a> · <a href="runtime-tick-recovery.json">未知归属与自动恢复</a> · <a href="acceptance-scene-readback.json">地图保存回读</a> · <a href="fixed-region-readback.json">固定区域比较</a> · <a href="../index.html">Blender来源与原审核记录</a></p>
<p>同一正式网格，CPD浅队色8–11／启用16；不占受击7或Mass每实例动画63个槽。两队各自Q召唤5台、双方己蓝敌红，登记归属变化和清零后Tick恢复通过。五个对照视角的固定内部区域最大8位RGB差为1，纹理与屏幕边界不计入该结论。</p>
<p>审核地图 <code>/Game/Maps/LVL_CommanderMassPrototype</code>；编辑器文件夹 <code>GuLiStrike/Review/Sweeper_20261009</code> 中蓝红两台为EditorOnly，保存回读通过。实际游玩使用原先驱号Q入口；没有增加初始扫荡者部署。两类占位2004／2007不制作，原资源和稳定ID保留。</p>'''+''.join(sections)+'''<p>本次没有修改网格、重新构建LOD、改变原明暗或新增原生代码；没有原生编译或新增自动化测试框架。既有脚环/UI逻辑保持；此次未重复六环境UI、晚加入及长时间性能验证。最终视觉反馈待用户在UE查看。</p></html>'''
    (UE/'index.html').write_text(html,encoding='utf8')
    links=[]
    for m in re.finditer(r'(?:href|src)="([^"]+)"',html):
        value=m.group(1)
        if not (UE/value).resolve().exists():raise RuntimeError('Broken gallery link: '+value)
        links.append(value)
    font=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',30)
    sheet=Image.new('RGB',(1600,800),'#EEEEEB');draw=ImageDraw.Draw(sheet)
    for x,side,label in [(0,'Blue','己方 · #6AA4BE'),(800,'Red','敌方 · #A34053')]:
        image=Image.open(UE/f'Images/{side}_LOD0_hero.png').convert('RGB');image.thumbnail((800,730))
        sheet.paste(image,(x+(800-image.width)//2,70+(730-image.height)//2));draw.text((x+28,20),label,fill='#2C3735',font=font)
    sheet.save(UE/'overview.png')
    report={'success':True,'static_links':len(links),'real_ue_static_images':25,'actual_runtime_batch_images':4,
            'browser_interaction':'not verified; existing local-page restriction not bypassed',
            'evidence':[{ 'file':name,'sha256':hashlib.sha256((OUT/name).read_bytes()).hexdigest()} for name in required],
            'images':[{ 'file':str(p.relative_to(OUT)),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted((UE/'Images').glob('*.png'))]}
    (UE/'delivery-validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps({k:report[k] for k in ['success','static_links','real_ue_static_images','actual_runtime_batch_images']}))


if __name__=='__main__':main()
