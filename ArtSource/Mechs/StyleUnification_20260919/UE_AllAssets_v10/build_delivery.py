"""Build the batch delivery index from completed production reports."""
from pathlib import Path
import json,hashlib,html
OUT=Path(__file__).resolve().parent
ROOT=OUT.parents[3]
def read(name):return json.loads((OUT/name).read_text(encoding='utf-8'))
export=read('export_report.json');imported=read('ue_import.json');show=read('ue_showcase.json')
gallery=read('ue_gallery_saved_readback.json');live=read('live_assets_readback.json')['result']
assert all(v['success'] for v in (export,imported,show,gallery,live,read('fbx_readback.json')))
old=json.loads((OUT.parent/'UE_StyleSync_v9/delivery_manifest.json').read_text(encoding='utf-8'))
G='/Game/GuLiStrike/GroundMech'
specs=[
 ('Mech_Lightest','轻型机甲','v9 弧面封舱 · 对称灰蓝腿甲 · 贴壳排气口','当前 Ground 玩家','线稿 / 三档明暗',G+'/BP_GroundMech_Light',20038,11325),
 ('SpiderMech','SpiderMech','完整原网格 · 四片完整暗红装甲','独立风格蓝图 / Demo 参照','线稿 / 三档明暗','/Game/GuLiStrike/Mechs/SpiderMech/BP_SpiderMech_Styled',839778,839778),
 ('Mecha_01','Mecha 01','低饱和灰蓝 · 原骨架与武器挂点','独立模型 / 原待机与行走动画','连续受光',imported['parts']['Mecha_01']['blueprint'].split('.')[0],3982,0),
 ('Mecha_02','Mecha 02','金黄与黑灰 · 原骨架与武器挂点','独立模型 / 原待机与行走动画','连续受光',imported['parts']['Mecha_02']['blueprint'].split('.')[0],5964,0),
 ('FireWeapon_01','FireWeapon 01 · 三管','灰蓝与黑灰 · 保留三根炮管','独立可装配武器模型','线稿 / 三档明暗',imported['parts']['FireWeapon_01']['blueprint'].split('.')[0],1184,484),
 ('MissileWeapon_01','MissileWeapon 01','灰红外壳 · 双列弹架','独立可装配武器模型','线稿 / 三档明暗',imported['parts']['MissileWeapon_01']['blueprint'].split('.')[0],7181,3202),
 ('Machinegun_lvl1','Machinegun · Lv1','赭黄与深青绿 · 琥珀功能灯','轻型玩家的右侧机枪模型','线稿 / 三档明暗',G+'/Style_v9/Meshes/SK_Machinegun',569,162),
 ('Missile_01','Missile 01 · 独立弹体','灰白弹体 · 深灰分界 · 尾翼','独立弹体模型','连续受光',imported['parts']['Missile_01']['blueprint'].split('.')[0],772,0)]
assets={name:dict(title=title,description=desc,usage=usage,style=style,entry=path,body_triangles=body,outline_triangles=ink,
    capture='UE_'+name+'.png',migration='new_v10' if name in imported['parts'] else 'reused_v9')
    for name,title,desc,usage,style,path,body,ink in specs}
files=[OUT.parent/'Production_v9_VentMount/Mechs_VentMount_v9.blend']
files.extend(OUT/'FBX'/f'{name}.fbx' for name in imported['parts'])
for row in export['parts'].values():
    for paths in row['textures'].values():files.append(Path(paths[0]))
files.extend(OUT/name for name in show['captures'])
files.extend(OUT/name for name in ['export_report.json','fbx_readback.json','ue_import.json','live_assets_readback.json','ue_showcase.json','ue_gallery_saved_readback.json'])
hashes={str(f.relative_to(ROOT)).replace('\\','/'):{'sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'bytes':f.stat().st_size} for f in files}
assert next(iter(hashes.values()))['sha256']==old['source_sha256']
manifest={'date':'2026-09-19','authorization':'把已经制作了的资源都同步至UE，然后做一波总结',
    'scope':'Eight completed assets in the Mech_Project / MechaController style batch; retired cockpit and rejected decimated Spider excluded.',
    'engine':'D:/UnrealEngine-5.7/Engine/Binaries/Win64/UnrealEditor.exe','source':old['source'],'source_sha256':old['source_sha256'],
    'asset_count':8,'unique_mesh_count':10,'new_meshes_this_turn':5,'assets':assets,'showcase_map':show['map'],
    'ground_play_map':'/Game/Maps/LVL_GroundMech_Demo','previous_delivery':'../UE_StyleSync_v9/delivery_manifest.json',
    'checks':{n:read(n)['success'] for n in ['export_report.json','fbx_readback.json','ue_import.json','ue_showcase.json','ue_gallery_saved_readback.json']},
    'live_asset_readback':live['success'],'native_build':'not_applicable: no native source or build configuration changes',
    'gameplay_regression':'not_rerun; v9 root-motion and multiplayer results remain separately recorded',
    'performance':'not_run','visual_acceptance':'UE transfer authorized; no new user final aesthetic acceptance inferred',
    'known_limits':['Spider body 839778 + outline 839778 triangles; no further reduction',
      'Mecha_01, Mecha_02 and Missile_01 retain authored continuous-lit style',
      'Light v9 nose close-up retains minor outline intersections',
      'Existing flight-navigation WorldValidator returns NotValidated on non-flight gallery and emits handled ensure; save and reload passed'],
    'hashes':hashes}
(OUT/'delivery_manifest.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False),encoding='utf-8')
e=html.escape
cards=[]
for name,row in assets.items():
    walk=f'<a class="walk" href="UE_{name}_Walk.png" target="_blank">查看原行走动画姿态 ↗</a>' if name in ('Mecha_01','Mecha_02') else ''
    cards.append(f'''<article id="{name}"><div class="cardtitle"><h2>{e(row['title'])}</h2><span>{e(row['style'])}</span></div>
    <a href="{row['capture']}" target="_blank"><img src="{row['capture']}" alt="{e(row['title'])} UE 实际画面" loading="lazy"></a>
    <div class="details"><strong>{e(row['usage'])}</strong><p>{e(row['description'])}</p>
    <p class="muted">主体 {row['body_triangles']:,} tris · 描边 {row['outline_triangles']:,} tris</p>
    <code>{e(row['entry'])}</code>{walk}</div></article>''')
navigation=''.join(f'<a href="#{name}">{e(row["title"])}</a>' for name,row in assets.items())
page='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>机甲与武器 · UE 全批交付</title><style>
*{box-sizing:border-box}html{scroll-behavior:smooth;scroll-padding-top:24px}body{margin:0;background:#10181f;color:#ecf0f0;font:16px/1.7 "Segoe UI","Microsoft YaHei",sans-serif}main{max-width:1480px;margin:auto;padding:42px 28px 70px}h1{font-size:clamp(28px,4vw,48px);line-height:1.2;margin:12px 0 18px}h2{font-size:22px;margin:0}.eyebrow{letter-spacing:.12em;color:#9bc5dc;font-size:13px}.lead{color:#bac8d1;max-width:940px}a{color:#a7d4ef;text-decoration:none}a:hover{text-decoration:underline}.stats{display:flex;gap:12px;flex-wrap:wrap;margin:26px 0}.stats b{font-size:25px;color:#e7bd72;margin-right:8px}.stats div{padding:10px 20px;background:#1c2a34;border-radius:9px}.hero{display:block;width:100%;border-radius:12px}figcaption{color:#9bb0bf;font-size:14px;margin-top:12px}figure{margin:0 0 26px}.paths{padding:18px 22px;background:#1a2832;border-left:3px solid #e7bd72;border-radius:4px}.paths p{margin:6px 0}code{display:block;font:13px/1.8 Consolas,monospace;overflow-wrap:anywhere;color:#9eb9cd}nav{display:flex;gap:10px;flex-wrap:wrap;margin:28px 0}nav a{border:1px solid #344853;border-radius:20px;padding:5px 13px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:24px}article{border:1px solid #31434f;border-radius:12px;overflow:hidden;background:#19262f;scroll-margin-top:24px}.cardtitle{padding:17px 21px;display:flex;align-items:center;justify-content:space-between;gap:12px}.cardtitle span{color:#abc3d4;font-size:13px;white-space:nowrap}article img{display:block;width:100%;aspect-ratio:1.5;object-fit:contain;background:#8095a5}.details{padding:17px 21px 22px}.details p{margin:5px 0 12px}.muted{color:#9bb0bf;font-size:14px}.walk{display:block;margin-top:12px}.foot{margin-top:32px;padding-top:24px;border-top:1px solid #31434f;color:#a9b9c4}.links{display:flex;gap:24px;flex-wrap:wrap}@media(max-width:850px){.grid{grid-template-columns:1fr}main{padding:24px 14px}.cardtitle{align-items:start;flex-direction:column}.stats div{flex:1 0 40%}.stats b{display:block}}@media(prefers-reduced-motion:reduce){html{scroll-behavior:auto}}
</style><main><div class="eyebrow">GULISTRIKE / SOURCE UE 5.7 / 2026.09.19</div>
<h1>机甲与武器，整批同步完成</h1>
<p class="lead">本批已制作的四台机甲、三套武器和一枚独立导弹均已进入 UE。以下均为保存后资源在引擎中的实际画面，保留各自当前制作版本。</p>
<div class="stats"><div><b>8 / 8</b>成品已同步</div><div><b>10</b>独立网格</div><div><b>5</b>本次补齐</div><div><b>11</b>引擎截图</div></div>
<figure><a href="UE_All_Assets.png" target="_blank"><img class="hero" src="UE_All_Assets.png" alt="八项资源在同一 UE 展示关卡中的实际总览"></a><figcaption>后排左起：Mecha 01、Mecha 02、轻型机甲、SpiderMech。前排：三管炮、导弹武器、Lv1 机枪、独立导弹。总览保持实际尺寸；单件相机各自调整。</figcaption></figure>
<div class="paths"><p><strong>全部资源展示关卡</strong></p><code>/Game/GuLiStrike/Mechs/StyleShowcase/LVL_MechAsset_Showcase</code><p><strong>地面玩家操控测试</strong></p><code>/Game/Maps/LVL_GroundMech_Demo</code><p class="muted">轻型机甲与 Lv1 机枪用于当前 Ground 玩家。其他武器是独立模块，本次同步模型，没有新增开火、伤害或换枪功能。</p></div>
<nav>'''+navigation+'''</nav><section class="grid">'''+''.join(cards)+'''</section>
<div class="foot"><p>已核对 FBX 尺寸与权重、UE 骨架和挂点、保存引用、8 项相关蓝图编译及 Mecha 01/02 原动画姿态。五项保留线稿与三档明暗；Mecha 01、Mecha 02 和独立导弹沿用已制作的连续受光材质。</p>
<p>Spider 保留完整原网格，主体与描边各 839,778 三角面。当前未做性能压测。轻型头甲端部近景仍有少量原 v9 描边交叠点；地图保存时的项目飞行导航验证器提示已在说明中记录。用户最终视觉评价单独记录。</p>
<p class="links"><a href="README.md">完整总结与资源路径 ↗</a><a href="delivery_manifest.json">版本、哈希与验证清单 ↗</a><a href="ue_showcase.json">引擎验证记录 ↗</a><a href="../UE_StyleSync_v9/Review_UE_v9.html">此前 Demo 实机画面 ↗</a></p></div></main></html>'''
(OUT/'Review_UE_All_v10.html').write_text(page,encoding='utf-8')
print(json.dumps({'success':True,'assets':len(assets),'files_hashed':len(hashes),'page':str(OUT/'Review_UE_All_v10.html')},ensure_ascii=False))
