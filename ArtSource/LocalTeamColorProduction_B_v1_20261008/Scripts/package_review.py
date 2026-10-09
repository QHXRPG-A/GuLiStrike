"""Package the native B delivery and immutable A/B visual evidence."""
import json,hashlib,html,re
from pathlib import Path
from PIL import Image
from datetime import datetime
O=Path(__file__).resolve().parents[1];A=O.parent/'LocalTeamColorReference_A_v3_20261008'
names={'DefaultSoldier':'先驱号','WM01':'重防号','BiZhiMao':'彼之矛','ShieldGenerator':'护盾发生器','ManualOutpost':'前哨建筑','MissileTurret':'防空炮','SentryTurret':'哨戒炮','ResourceFactory':'矿厂','SSF_AirBase':'空军基地','SSF_CloningCenter':'克隆中心','SSF_CommandCenter':'指挥中心','SSF_MilitaryFactory':'军工厂','SSF_Reactor':'反应堆','SSF_StrategyCenter':'战略中心'}
findings={
'DefaultSoldier':'已纠正前部球形主壳的零散队色串色；主壳整件米砂、肩甲浅队色、后环与腿外护板深队色，枪管和底部机构深灰。保留源六足、两枪和原细节。',
'WM01':'两组各四口导弹仓、四个悬浮盘与固定机械分区按原模型核对；盘顶固定米砂、外裙深队色、武器仓外罩浅队色，炮管恢复固定深灰。',
'BiZhiMao':'长炮顶甲固定米砂、侧甲浅队色，炮口套圈及踝甲深队色；腿甲乳白，炮口工作面和框架深灰。施工体共用该配色设计，原部署/移动动画保留。',
'ShieldGenerator':'修正三个顶部色点误归固定区的问题；三件各50面、共150面均为 TeamLightLamp，两队分别 #6AA4BE / #A34053，红版没有蓝/青点。上部大护盖浅队色，检修小盖深队色，下部米砂与乳白固定，底座深队色。',
'ManualOutpost':'逐连接组件核对，中央最高板整件浅队色，相邻板整件米砂，外窄板整件乳白，外半圆环整件深队色；同一件从顶到底及背面没有高度分层。原纪念碑轮廓保留。',
'MissileTurret':'两仓各五弹头、仓侧护板与检修条、基座管线按原 UE 网格核对。外罩浅队色、检修条深队色，中部箱体及基座固定；三视图正面已校正为炮口方向。',
'SentryTurret':'原炮管、通风孔、软管和四足底座保留；塔侧浅队色、后盖与转台深队色，正面护框乳白，基座米砂；三视图正面已校正为炮口方向。',
'ResourceFactory':'屋顶中心盖与入口侧翼浅队色；已纠正整片侧柜门误染深队色，柜门恢复米砂，仅盖板与饰带使用深队色。门板与外框乳白、主墙和原坡道米砂；原 Blueprint 主体、门、AccessRamp 三组件装配和五门板、三侧柜保留。',
'SSF_AirBase':'校正顶部两个对角扇区及中心盖的队色范围；穹顶余区米砂、既有窄压条乳白，支架与雷达工作面深灰，两队固定区一致。',
'SSF_CloningCenter':'四舱外罩浅队色、侧盖与舱框深队色；软管和中央机构纠正为固定深灰，顶部口深队色、顶盖及入口乳白，背架米砂。',
'SSF_CommandCenter':'校正上环浅队色、次环深队色和塔心固定深灰；外支撑有队色，窄横框乳白，塔冠米砂，补齐四块既有底足外护甲的乳白固定分区，底足机构仍深灰。',
'SSF_MilitaryFactory':'校正整块屋顶拱架及入口立面队色、保留侧后深队色腰带；门板乳白、大墙面米砂，雷达和支脚机械为固定深灰。',
'SSF_Reactor':'校正下部机械裙和外管线为固定深灰；主壳侧甲浅队色、顶环深队色、观察口乳白及固定壳区米砂，腿外甲乳白。',
'SSF_StrategyCenter':'腰部连续护板浅队色、阵列框与上环深队色，工作阵列和天线固定深灰，壳体米砂；已将原两侧下部支撑大护甲从米砂纠正为固定乳白。'}
native=json.loads((O/'Reports/native-save-readback.json').read_text(encoding='utf8'))
assert native['all_native_save_readback_checks_passed']
manifest=json.loads((O/'review-manifest.json').read_text(encoding='utf8'))
assert len(manifest['models'])==14
combined=json.loads((O/'Reports/combined-save-readback.json').read_text(encoding='utf8'))
assert combined['all_passed'] and combined['sha256']==hashlib.sha256((O/'PaletteOnly_14Models_BlueRed_B_v1.blend').read_bytes()).hexdigest()
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,data):p.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf8')
auth={'date':'2026-10-08','scope':'actual Blender color/style B candidates; no geometry change; no UE replacement','user_messages':['开始建模，blender已开，把成品放blender中让我审核','只改配色，不要动模型','保持所有模型的美术风格一致，三渲二、三档明暗、没有线稿的需要适当加上线稿','做完需要自行比对参考图+三视图 看看哪里没对齐，比如护盾发生器，顶部那几个点没有设置成可变色'],'historical_A_record':'A_v3 frozen pending record retained; latest user instruction authorizes B production; does not fabricate per-board approval','B_status':'pending','formal_UE_update_authorized':False}
save(O/'production-authorization.json',auth)
save(O/'approval_B.json',{'version':'PaletteOnly_B_v1','status':'pending','user_decision':None,'models':[{'id':k,'name':v,'Blue_Red_status':'pending','user_decision':None} for k,v in names.items()]})
(O/'Configs').mkdir(exist_ok=True)
qa=[];previews=[]
for key,name in names.items():
    conf=json.loads((A/'Configs'/(key+'.json')).read_text(encoding='utf8'))
    report=json.loads((O/'Reports'/(key+'_production.json')).read_text(encoding='utf8'))
    check=next(x for x in native['model_checks'] if x['key']==key)
    model=O/'Models'/(key+'_PaletteOnly_B_v1.blend');assert digest(model)==check['native_file_sha256']
    conf['reference_A_historical_status']=conf.pop('approval_A',None)
    conf['construction_B']={'status':'review_pending','approved':False,'authorization':'../production-authorization.json','native_file':'../Models/'+model.name,'sha256':digest(model),'source_sha256':report['source_sha256'],'role_attribute':'Bv1_ColorRegion (FACE/INT)','role_ids':['Cream','Sand','Gray','TeamLight','TeamDark','Amber','Cyan','TeamLightLamp'],'palette_source':'original user swatch cards','native_regions':report['teams'][0]['region_mapping'],'native_face_role_counts':[x for x in check['checks'] if 'role_face_counts' in x],'style':{'tone_factors':[.4,.72,1],'thresholds':[.38,.68],'interpolation':'CONSTANT','new_lines':'Freestyle contour/border/major crease on original mesh; no added outline geometry'},'comparison':'../Comparisons/'+key+'_A3_B1.png'}
    conf['runtime_enabled']=False
    save(O/'Configs'/(key+'.json'),conf)
    for team in ('Blue','Red'):
        for view in ('Hero','Front','Left','Back'):
            p=O/'Previews'/f'{key}_{team}_{view}.png'
            with Image.open(p) as im:im.load();assert im.size==(1100,1000)
            previews.append({'key':key,'team':team,'view':view,'path':p.relative_to(O).as_posix(),'sha256':digest(p),'width':1100,'height':1000,'source':'unretouched native Blender render'})
    residual={'ManualOutpost':'参考把部分原曲面板画成直矩形；实际保留原曲面与半圆环，不改轮廓。','MissileTurret':'二维参考圆滑化了弹头、边角和底座；实际仍为原 UE 网格，未重做这些形体。','SentryTurret':'二维参考的圆滑护甲与实际原网格边角不同；仅按现有区域配色。','ResourceFactory':'二维参考简化了门板交叉加强筋与侧后装配；实际原门板、加强筋、通风和坡道保持。'}.get(key,'参考绘画的倒角、细线及轮廓不作新几何；以原网格为结构依据。')
    qa.append({'id':key,'name':name,'views_read':['Blue Hero/Front/Left/Back','Red Hero/Front/Left/Back','A_v3 Blue/Red boards'],'findings_and_corrections':findings[key],'remaining_reference_geometry_differences':residual,'comparison':'Comparisons/'+key+'_A3_B1.png','geometry_authority':'original native mesh, not AI-drawn dimensions/details','B_user_approval':'pending'})
    mr=next(x for x in manifest['models'] if x['id']==key)
    mr.update({'file_sha256':digest(model),'config':'Configs/'+key+'.json','comparison':'Comparisons/'+key+'_A3_B1.png','reference_board_sha256':{t:conf['boards'][t]['sha256'] for t in ('blue','red')}})
manifest.update({'native_previews':112,'model_count':14,'team_variant_count':28,'production_authorization':'production-authorization.json','B_approval':'pending','formal_UE_update':False,'native_source_geometry_preserved':True,'review_file':'PaletteOnly_14Models_BlueRed_B_v1.blend','review_file_sha256':digest(O/'PaletteOnly_14Models_BlueRed_B_v1.blend')})
save(O/'review-manifest.json',manifest);save(O/'preview-inventory.json',previews)
save(O/'visual_qa.json',{'performed_by':'assistant actual image reading','stage':'B native color candidates','user_approval':False,'models':qa,'general_limits':['A_v3 is a two-dimensional color design and has drawn details/silhouettes that are not exact engineering views. Original native geometry remains the authority under the user color-only instruction.','Cream visible-surface 20–30% remains a design target, not a claimed pixel measurement; native mesh-area fractions are reported separately.','Supplemental Freestyle is visible in final renders (F12); material preview shows three-tone shading and existing source outlines/masks. New exportable outline geometry was not created.','UE material import, runtime display, animation playback and game performance were not performed.']})
escape=html.escape
cards=[]
for key,name in names.items():
    imgs=''.join(f'<a href="Previews/{key}_{team}_{view}.png" target="_blank"><figure><img loading="lazy" src="Previews/{key}_{team}_{view}.png" alt="{escape(name)} {team} {view}"><figcaption>{team} · {view}</figcaption></figure></a>' for team in ('Blue','Red') for view in ('Hero','Front','Left','Back'))
    refs=''.join(f'<a href="../LocalTeamColorReference_A_v3_20261008/Boards/{key}_{team}.png" target="_blank"><img loading="lazy" src="../LocalTeamColorReference_A_v3_20261008/Boards/{key}_{team}.png" alt="A_v3 {team}"></a>' for team in ('blue','red'))
    cards.append(f'<section id="{key}"><h2>{escape(name)} <small>B 待审核</small></h2><p>{escape(findings[key])}</p><p><a href="Models/{key}_PaletteOnly_B_v1.blend">单模型 Blender</a> · <a href="Configs/{key}.json">固定／队色分区</a> · <a href="Comparisons/{key}_A3_B1.png" target="_blank">参考与成品对照整板</a></p><details><summary>A_v3 蓝红参考图</summary><div class="refs">{refs}</div></details><div class="renders">{imgs}</div></section>')
nav=''.join(f'<a href="#{k}">{escape(v)}</a>' for k,v in names.items())
page=f'''<!doctype html><html lang="zh-CN"><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>14 模型 Blender 配色成品 · B 审核</title><style>body{{margin:0;background:#efede6;color:#2C3735;font:16px/1.6 "Microsoft YaHei",sans-serif}}header,main{{max-width:1500px;margin:auto;padding:24px}}header{{background:#f8f4ed}}h1{{font-size:30px;margin:0}}small{{font-size:14px;font-weight:normal;color:#A34053}}nav{{display:flex;gap:12px;flex-wrap:wrap;padding:15px 0;position:sticky;top:0;background:#efede6;z-index:1}}a{{color:#274E61}}section{{padding:24px 0;border-bottom:2px solid #D5C09C}}.renders{{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}}.refs{{display:grid;grid-template-columns:1fr 1fr;gap:8px}}img{{width:100%;display:block}}figure{{margin:0;background:#fff}}figcaption{{padding:4px 12px}}.palette{{display:flex;gap:14px;flex-wrap:wrap}}.swatch{{display:inline-block;width:24px;height:24px;vertical-align:middle;border:1px solid #999}}summary{{cursor:pointer;padding:12px}}@media(max-width:900px){{.renders{{grid-template-columns:1fr 1fr}}.refs{{grid-template-columns:1fr}}}}</style><header><h1>十四模型 · 蓝红配色成品 B_v1</h1><p>实际 Blender 模型与 112 张效果／正／左／后原生渲染。点击图片放大，展开 A_v3 参考核对分区。全部等待用户 B 审核。</p><p><a href="PaletteOnly_14Models_BlueRed_B_v1.blend">打开全部蓝红 Blender 成品</a> · <a href="README.md">审核说明</a> · <a href="Reports/native-save-readback.json">保存回读</a></p><div class="palette">{''.join(f'<span><i class="swatch" style="background:{h}"></i> {label} {h}</span>' for label,h in [('乳白','#FEE4D9'),('米砂','#D5C09C'),('机构','#2C3735'),('蓝浅','#6AA4BE'),('蓝深','#274E61'),('红浅','#A34053'),('红深','#662249')])}</div><p>仅改配色与授权的三渲二、三档明暗、适量线稿；原几何、UV、骨架、动画、LOD 保持。补充线稿见 F12 原生渲染，未新增网格壳。正式 UE 尚未更新。</p><a href="Previews/Overview_BlueRed_B_v1.png" target="_blank"><img src="Previews/Overview_BlueRed_B_v1.png" alt="十四模型蓝红实际成品总览"></a></header><main><nav>{nav}</nav>{''.join(cards)}</main></html>'''
(O/'reference-comparison.html').write_text(page,encoding='utf8');(O/'index.html').write_text(page,encoding='utf8')
(O/'README.md').write_text('''# 十四模型蓝红实际 Blender 配色成品 B_v1

[全部模型 Blender](PaletteOnly_14Models_BlueRed_B_v1.blend) · [参考图与实际三视图对照](reference-comparison.html) · [总览](Previews/Overview_BlueRed_B_v1.png)

用户已明确开始实际 Blender 制作并限定“只改配色，不要动模型”，随后要求统一三渲二、三档明暗及适量线稿，并逐个对照参考。当前全部 B 待用户审核；旧 A_v3 冻结记录保留，不虚构逐板 A 通过。正式 UE 资源没有更新。

默认场景 `00_十四模型蓝红成品_B待审核` 使用集合实例展示十四对。模型本身保留原几何和变换；总览实例仅调整展示大小和摆位。右上场景菜单选 `Blue_` / `Red_` 加模型英文标识可旋转近看，`Models/` 内另有十四个独立源文件。LOD1/2 在单独隐藏集合，原 Action 已保留。重防号对应 WM01/UnitTypeId=2，不是玩家 Ground。

三档明暗为 0.40/0.72/1.00，与浅/深队色独立。保留既有线稿和壳；没有原线稿的网格使用 Freestyle 从原面生成轮廓、边界和主要折线，**F12 与 Previews 展示完整补充线稿**。材质预览不会显示 Freestyle。没有新增可导出的描边壳，后续引擎线稿实现需另行确认，不能把本文件等同 UE 成品。

护盾顶部三件均为队色灯，蓝 #6AA4BE、红 #A34053；固定区在两队保持一致。`Bv1_ColorRegion` 面属性记录固定/队色角色，实际颜色在原顶点色层或 `Bv1_PaintLinear`。`Configs/` 记录 A_v3 设计与实际分区，`Reports/*_production.json` 保存原来源、几何签名和配色面积，`Reports/native-save-readback.json` 为保存后独立回读。

112 张原生图为十四模型 × 蓝红 × 效果/正/左/后，没有修图。`Comparisons/` 仅拼排这些原图和 A_v3。二维参考有绘画细节和轮廓差异，按用户不改模型要求以原网格为结构依据；不宣称像素一比一复刻。乳白20–30%为设计目标，网格面积统计不冒充相机可见像素占比。

没有编译、启动游戏、更新正式 UE 或新增自动化测试。HTML 静态链接与图片回读核对；本地浏览器交互未验证，沿用原工具访问限制，没有绕过。
''',encoding='utf8')
frozen=json.loads((A/'frozen_delivery_manifest.json').read_text(encoding='utf8'));errors=[]
for f in frozen['files']:
    if digest(A/f['path'])!=f['sha256']:errors.append(f['path'])
assert not errors,errors
links=[]
for filename in ('index.html','reference-comparison.html'):
    for ref in re.findall(r'(?:href|src)="([^"]+)"',(O/filename).read_text(encoding='utf8')):
        if ref.startswith('#'):continue
        assert (O/ref).is_file(),(filename,ref)
        links.append({'page':filename,'reference':ref})
save(O/'delivery-check.json',{'native_models':14,'Blue_Red_variants':28,'native_four_views':112,'all_pngs_decoded':True,'A_v3_frozen_files_checked':len(frozen['files']),'A_v3_changed_files':[],'html_static_links_checked':len(links),'static_missing_links':[],'local_browser_interaction_verified':False,'reason':'previous file:// tool restriction retained; no bypass','Blender_live_open':'see Reports/live-review.json','B_approval':'pending'})
print('B_REVIEW_PACKAGE_READY_14_MODELS_28_VARIANTS_112_NATIVE_VIEWS',len(links))
