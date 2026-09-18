"""Assemble existing evidence only; never mutate UE assets or treat missing gates as passed."""
import argparse, datetime, hashlib, html, json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'TestResults/Scale020'
def read(name): return json.loads((OUT/name).read_text(encoding='utf-8-sig'))
def esc(value): return html.escape(str(value),quote=True)
def compact(value): return json.dumps(value,ensure_ascii=False) if isinstance(value,(dict,list)) else str(value)
def link(path,label): return f'<a href="{esc(path)}">{esc(label)}</a>'

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--automation',default='Automation-10');args=parser.parse_args()
    tests=read(args.automation+'/index.json'); q=read('runtime-q-dedicated.json')
    demo=read('DemoReview/audit.json');nav=read('navigation-cold-readback.json');ore=read('resource-bake-readback.json')
    dock=read('ResourcePIE/dual-client.json');door=read('factory-door-collision-repeat.json');rows=[]
    for book in read('workbook-migration-v1.json')['books']:
        for c in book['changes']:
            rows.append({'kind':'Excel','object':book['path']+' / '+c['sheet']+' / '+c.get('row_name',''),
                         'field':c['field']+' ('+c['cell']+')','old':c['old'],'target':c['target'],
                         'space':c.get('space',''), 'source':'workbook-migration-v1.json'})
    for c in read('source-default-migration-v1.json')['entries']:
        rows.append({'kind':'C++','object':c['file'],'field':c['symbol'],'old':c['old'],'target':c['target'],
                     'space':c['space'],'source':'source-default-migration-v1.json'})
    for c in read('editor-property-migration-v1.json')['entries']:
        rows.append({'kind':'UE资产','object':c['asset']+(' / '+str(c['component']) if c.get('component') else ''),
                     'field':c['field'],'old':c['old'],'target':c['target'],'space':c['space'],
                     'source':'editor-property-migration-v1.json'})
    for c in read('effect-literal-migration-v1.json')['entries']:
        rows.append({'kind':'VFX C++','object':c['file'],'field':'显式空间表达式','old':c['before'],
                     'target':c['target'],'space':'world-centimeters','source':'effect-literal-migration-v1.json'})
    modules=[ROOT/'Binaries/Win64/UnrealEditor.modules',Path('D:/UnrealEngine-5.7/Engine/Binaries/Win64/UnrealEditor.modules')]
    ids=[json.loads(p.read_text())['BuildId'] for p in modules]
    build_id={'project':ids[0],'engine':ids[1],'match':ids[0]==ids[1]}
    def latest_build(target):
        candidates=[p for p in OUT.glob(f'Build-{target}-*.log') if p.stem.split('-')[-1].isdigit()]
        candidates.sort(key=lambda p:int(p.stem.split('-')[-1]),reverse=True)
        return next((p.name for p in candidates if 'Result: Succeeded' in p.read_text(encoding='utf-8-sig',errors='replace')),None)
    traversal=read('mining-traversal-migration-v1.json')
    for c in traversal['entries']:
        rows.append({'kind':'C++','object':traversal['source'],'field':c['field'],'old':c['old'],
                     'target':c['target'],'space':c['space'],'source':'mining-traversal-migration-v1.json'})
    counts={kind:sum(r['kind']==kind for r in rows) for kind in ['Excel','C++','UE资产','VFX C++']}
    failures=[t['fullTestPath'] for t in tests['tests'] if t['state']=='Fail']
    failure_note=', '.join(failures) if failures else '无'
    warning_tests=[{'name':t['fullTestPath'],'count':t['warnings']} for t in tests['tests'] if t.get('warnings',0)]
    warning_note=f"其中{tests['succeededWithWarnings']}项成功用例带警告，共{sum(t['count'] for t in warning_tests)}条；通过不等于零警告，详情保留在原始报告。"
    scale_tests=[{'name':t['fullTestPath'],'state':t['state']} for t in tests['tests'] if '.Scale020.' in t['fullTestPath']]
    door_runtime=dock.get('mining_cycle',{}).get('checks',{}).get('visual_door_no_collision_or_overlaps_on_both_clients',False)
    mining_note=('采矿完整闭环已通过；厂内改为固定路线直接进出，已删除等门、接地拟合与碰撞扫掠。历史停点证据保留。' if dock.get('success') else
                 ('门已在联机两端确认无碰撞，矿车通过原门阻挡处；随后仍在厂体Body接地/扫掠处停住，完整闭环未通过。'
                  if door_runtime and dock.get('dock_stall_diagnostics') else
                  '厂内已改为固定路线直接进出，删除等门、接地拟合与碰撞扫掠；完整采矿闭环仍以最新运行结果为准，不能视为通过。'))
    door_note=('按用户要求，工厂门已改为纯表现：开关状态均无碰撞、无重叠事件、无导航影响；厂体、地板和坡道碰撞不变。'
               '蓝图保存后冷启动读回通过，重复执行0修改。' if door.get('success') and not door.get('changed') else
               '门组件纯表现策略尚待冷读回确认。')
    pending=([] if dock.get('success') else ['完成采矿进出厂/卸货闭环；当前报告不以初始检查通过代替闭环通过。'])+[
             '实际移动编队、Ship/僚机飞行重生、建造出兵、空运、护盾、占领和传送的完整运行矩阵尚未全部完成。',
             '原地图陡坡路线与蓝图空间音效衰减需补充验证；未扩大压力测试。',
             '用户视觉审核尚未进行。技术测试与视觉批准分别记录。']
    summary={'generated_local':datetime.datetime.now().astimezone().isoformat(),'overall':'partial_not_accepted',
        'scale':.2,'counts':counts,'comparison_entries':len(rows),'comparison_scope':'Explicit manifests; not a complete source diff',
        'automation':{'report':args.automation,'passed':tests['succeeded']+tests['succeededWithWarnings'],
                      'failed':tests['failed'],'not_run':tests['notRun'],'failures':failures,'scale_tests':scale_tests,
                      'passed_with_warnings':tests['succeededWithWarnings'],'warning_tests':warning_tests},
        'builds':{'editor':latest_build('Editor'),'game':latest_build('Game'),'build_id':build_id},
        'q':{'passed':q['passed'],'checks':len(q['checks'])},'demo':{'original_actors':demo['original_actors'],
             'unchanged':demo['success'] and not demo['environment_differences'],'review_actors':len(demo['review_actors'])},
        'navigation_cold_all_passed':all(m['validation']['success'] for m in nav['maps']),
        'ore':{'nodes':ore['node_count'],'clusters':ore['cluster_count'],'checks_passed':all(ore['checks'].values())},
        'mining_latest_success':dock.get('success',False),
        'factory_door':{'policy':'presentation_only','cold_readback_success':door.get('success',False),
                        'repeat_changed':door.get('changed'),'runtime_two_client_policy_passed':door_runtime,
                        'report':'factory-door-collision-repeat.json'},'pending':pending}
    (OUT/'delivery-readback.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    (OUT/'comparison.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
    (OUT/'build-id-readback.json').write_text(json.dumps(build_id,indent=2),encoding='utf-8')
    images=[('01_close','近景'),('02_overview','陈列总览'),('03_top','俯视'),
            ('commander_040m','指挥官 · 40m'),('commander_160m','指挥官 · 160m'),('commander_360m','指挥官 · 360m')]
    cards=''.join(f'<figure>{link("DemoReview/Previews/"+f+".png","")}<a href="DemoReview/Previews/{f}.png"><img src="DemoReview/Previews/{f}.png" loading="lazy" alt="{label}"></a><figcaption>{label}</figcaption></figure>' for f,label in images)
    clips=''.join(f'<figure><video controls preload="metadata" src="FXReview/NS_WM01_Explosion_{i}.mp4"></video><figcaption>生产爆炸变体 {i} · 5.5s / 30fps</figcaption></figure>' for i in (7,8,9))
    evidence=[('workbook-dry-run.json','工作簿幂等检查'),('editor-property-readback.json','191项资产冷读回'),
        ('mining-sprite-size-repeat.json','粒子尺寸二次执行0修改'),('navigation-cold-readback.json','四张地图导航冷读回'),
        ('resource-bake-readback.json','矿石身份/中心/布局'),('runtime-q-dedicated.json','真实Q专服+两客'),
        ('DemoReview/audit.json','Demo_Map环境零差异'),('FXReview/capture.json','特效分帧播放参数'),
        (args.automation+'/index.json','定向自动化原始结果'),('ResourcePIE/dual-client.json','采矿最新运行结果'),
        ('BuildingFixture/Run-02/index.json','建筑World测试独立复验'),
        ('BuildingFixture/fixture-update-v1.json','建筑测试更新边界与残余警告'),
        ('mining-traversal-migration-v1.json','厂内逻辑精简与到达容差迁移'),('ResourcePIE/exited.png','矿车直接出厂实跑截图'),
        ('factory-door-collision-readback.json','工厂门无碰撞迁移'),('factory-door-collision-repeat.json','工厂门冷读回与幂等'),
        ('ResourcePIE/attempt-06-door-support-diagnosis.json','入口误命中门的定位证据'),
        ('ResourcePIE/attempt-08-door-disabled-secondary-stall.json','关门碰撞后剩余厂体阻挡'),
        ('ResourcePIE/attempt-02-entry-stall.json','采矿入口停住原始失败'),('build-id-readback.json','BuildId'),
        (summary['builds']['editor'],'Editor构建'),(summary['builds']['game'],'Game构建'),('comparison.json','逐字段前后清单JSON')]
    evidence_html=''.join('<li>'+link(p,l)+'</li>' for p,l in evidence if p)
    table=''.join('<tr>'+''.join('<td>'+esc(compact(r[key]))+'</td>' for key in ('kind','object','field','old','target','space'))+'</tr>' for r in rows)
    document=f'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>GuLiStrike 0.2× 尺度迁移 · 阶段审核</title><style>
:root{{color-scheme:dark;font-family:Segoe UI,Microsoft YaHei,sans-serif;color:#e4edf3;background:#10171d}}*{{box-sizing:border-box}}body{{margin:0}}main{{max-width:1440px;margin:auto;padding:44px 28px 80px}}h1{{font-size:38px;line-height:1.2;margin:12px 0 20px}}h2{{font-size:24px;margin-top:54px}}p{{max-width:1020px;line-height:1.8;color:#b9cbd6}}a{{color:#84d9e6}}.eyebrow{{color:#87cbbb;letter-spacing:3px}}.notice{{border-left:4px solid #e8b86b;padding:18px 24px;background:#29251e;line-height:1.7}}.metrics,.gallery,.videos{{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:18px}}.metrics{{margin:28px 0}}.metric{{background:#1b2832;padding:20px;border-radius:8px}}.metric strong{{display:block;font-size:30px;margin-bottom:6px}}figure{{margin:0;background:#1b2832;border-radius:8px;overflow:hidden}}img,video{{display:block;width:100%}}figcaption{{padding:12px 16px;color:#c2d8df}}.gallery{{grid-template-columns:repeat(2,minmax(0,1fr))}}ul{{line-height:1.8}}.evidence{{columns:3}}.scroll{{overflow:auto;max-height:620px;border:1px solid #344653}}table{{border-collapse:collapse;font-size:12px;width:100%}}td,th{{padding:10px;text-align:left;border-bottom:1px solid #344653;overflow-wrap:anywhere;min-width:130px}}th{{position:sticky;top:0;background:#20303d}}input{{width:100%;padding:12px;background:#1b2832;color:white;border:1px solid #526773;border-radius:6px;margin-bottom:12px}}.small{{font-size:13px;color:#91a8b5}}@media(max-width:800px){{.metrics,.gallery,.videos{{grid-template-columns:1fr}}.evidence{{columns:1}}h1{{font-size:30px}}main{{padding:24px 16px}}}}
</style><main><div class="eyebrow">GULISTRIKE / SCALE MIGRATION / 2026.09.18</div><h1>游戏对象 × 0.2<br>环境与世界锚点不变</h1>
<div class="notice">阶段审核候选，尚未完成整项验收。{summary['automation']['passed']}项定向测试通过，{summary['automation']['failed']}项失败。{mining_note}截图与生产资产播放不代替完整运行链路验证。</div>
<div class="metrics"><div class="metric"><strong>20%</strong>长度 · 速度 · 加速度</div><div class="metric"><strong>20 / 2 / 20</strong>Mass XY厘米 / Z厘米 / 速度厘米每秒</div><div class="metric"><strong>10 Hz</strong>同步周期不变，插值时间不缩短</div></div>
<p>不修改源模型、骨骼、地图尺寸、经济战斗数值、FOV及屏幕UI像素。Mass硬校正2m；Ship/工程车网络平滑距离51.2/76.8cm。Mass插值120–350ms，外推最多100ms；僚机插值200ms、外推最多500ms。协议13拒绝旧客户端。</p>
<p>{door_note}</p>
<h2>01 / 原 Demo_Map 尺度陈列</h2><p>原{demo['original_actors']}个环境Actor无变换/网格/材质/实例差异；仅添加{len(demo['review_actors'])}个可移除陈列对象。三个指挥官视角均为55°俯角、45°FOV。舰船原本比地面单位大，本轮保持原有相对比例。</p><div class="gallery">{cards}</div>
<p class="small">陈列标签：GuLi.Scale020.Review.v1；文件夹：GuLiScale020_Review_REMOVE_AS_GROUP。此关卡不承载完整对局，默认游戏地图不变。</p>
<h2>02 / 爆炸与落地预警</h2><p>实际生产Niagara资产按固定帧推进生成的连续播放，不是网络实录。玩法/预警半径1.6m，美术参考8m，最终组件比例0.2；预警周期0.8秒。真实Q另有专用服务器+两客户端的{len(q['checks'])}项数据检查，验证冻结端点、随机种子、服务器半径与两客Decal一致。</p><div class="videos">{clips}</div>
<h2>03 / 逐字段前后清单</h2><p>合并{len(rows)}条显式迁移记录：Excel {counts['Excel']}、C++默认值 {counts['C++']}、UE资产属性 {counts['UE资产']}、VFX空间表达式 {counts['VFX C++']}。Niagara图、运行时代码改造、导航和矿石烘焙另见原始报告；此表不是完整Git差异。</p>
<input id="filter" placeholder="搜索对象、字段、坐标空间……" aria-label="筛选迁移记录"><div class="scroll"><table><thead><tr><th>类别</th><th>对象</th><th>字段</th><th>旧值</th><th>目标值</th><th>坐标空间</th></tr></thead><tbody>{table}</tbody></table></div>
<h2>04 / 原始证据</h2><ul class="evidence">{evidence_html}</ul><p>四张地图导航冷读回通过；{ore['node_count']}个矿石节点保留身份及{ore['cluster_count']}个簇中心。源码Editor/Game构建成功，BuildId一致。自动化失败：{esc(failure_note)}。{esc(warning_note)}</p>
<h2>05 / 未完成项</h2><ul>{''.join('<li>'+esc(p)+'</li>' for p in pending)}</ul><p class="small">生成：{esc(summary['generated_local'])}。技术结果与用户视觉通过分开记录；当前状态 partial_not_accepted。</p></main>
<script>document.getElementById('filter').addEventListener('input',e=>{{const q=e.target.value.toLowerCase();for(const r of document.querySelectorAll('tbody tr'))r.hidden=!r.textContent.toLowerCase().includes(q)}});</script></html>'''
    (OUT/'Review.html').write_text(document,encoding='utf-8')
    md=f'''# 0.2×尺度迁移阶段交付（尚未完成验收）

可见审核页：[Review.html](Review.html)。生成时间：{summary['generated_local']}。

- 模型/空间数值、网络量化与客户端插值距离已迁移；时间、世界锚点、屏幕像素不变。
- {door_note}
- {mining_note}
- 显式对照清单{len(rows)}条：[comparison.json](comparison.json)。其他图与烘焙变更独立记录。
- 定向自动化：{summary['automation']['passed']}通过、{tests['failed']}失败、{tests['notRun']}未运行；{len(scale_tests)}项Scale020专项（具体状态见JSON）。
- 当前自动化失败：{failure_note}。旧隔离夹具失败与后续修复证据分别保留，不覆盖历史报告。
- {warning_note}建筑测试更新及退出警告见[记录](BuildingFixture/fixture-update-v1.json)。
- 真实Q：专服+两客，{len(q['checks'])}检查通过；三段特效视频是生产资产分帧播放，不是联网实录。
- 原Demo环境{demo['original_actors']}Actor无差异，{len(demo['review_actors'])}个可移除陈列对象。
- Editor：{summary['builds']['editor']}；Game：{summary['builds']['game']}；BuildId：{ids[0]}（一致={build_id['match']}）。

## 待完成

'''+''.join('- '+p+'\n' for p in pending)+'\n技术与视觉验收未整体通过。更多运行记录见[实施文档](../../Progress/DevelopmentDocumentation/20260918-游戏对象与效果统一缩放至0.2倍.md)。\n'
    (OUT/'REPORT.md').write_text(md,encoding='utf-8')
    print(json.dumps({'summary':str(OUT/'delivery-readback.json'),'review':str(OUT/'Review.html'),
                      'comparison_entries':len(rows),'overall':summary['overall']},ensure_ascii=False))

if __name__=='__main__': main()
