"""Summarize the whole loaded builds and create the final actual-frame player."""
import json
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'outputs/performance/20261009-all-optimizations'


def load(path):
    return json.loads(path.read_text(encoding='utf-8'))


def build_results():
    before = load(OUT/'before/valid-results.json')
    final = load(OUT/'final/whole-results.json')
    assert len(before) == len(final) == 3
    assert all(r['adoption_eligible'] for r in before+final)
    groups = {'before':before, 'final':final}
    result = {'historical_user_fps':[15,20], 'historical_comparison':'Approximate user observation; no pre-plan build was recovered or restored.',
              'scope':'Whole loaded delivered build before this append vs final all-optimization build. Baseline already includes the prior main four-stage implementation. Same server fixtures, different actually received/rendered client loads under transport backpressure.',
              'samples':groups, 'aggregate':{}, 'profiles':{}, 'normal_current':load(OUT/'normal-current/results.json')}
    for key, records in groups.items():
        aggregate = {}
        for metric in ['FrameTime','GameThreadTime','GPUTime','PhysicalUsedMB','VirtualUsedMB']:
            aggregate[metric] = {'mean_of_three_means':statistics.fmean(r['csv'][metric]['mean'] for r in records),
                                 'mean_of_three_p95s':statistics.fmean(r['csv'][metric]['p95'] for r in records),
                                 'sample_mean_range':[min(r['csv'][metric]['mean'] for r in records),max(r['csv'][metric]['mean'] for r in records)]}
        aggregate['fps_from_mean_frame'] = 1000/aggregate['FrameTime']['mean_of_three_means']
        result['aggregate'][key] = aggregate
        profile_rows = []
        for record in records:
            directory = OUT/key/'paired'/record['source_directory']
            start = load(directory/'counters-before.json')['worlds']
            end = load(directory/'counters-after.json')['worlds']
            row = {'sample':record['pair'],'source_directory':record['source_directory'],'worlds':[]}
            for a,b in zip(start,end):
                row['worlds'].append({'world':b['world'],'profile':b.get('profile'),
                    'flight_before':a.get('flight'),'flight_after':b.get('flight'),
                    'server_load_before':a.get('load'),'server_load_after':b.get('load'),
                    'delivery_before':a.get('flight_delivery'),'delivery_after':b.get('flight_delivery'),
                    'registry':b.get('registry'),'ui':b.get('ui')})
            profile_rows.append(row)
        result['profiles'][key] = profile_rows
    a,b = result['aggregate']['before'],result['aggregate']['final']
    result['changes_percent'] = {m:(b[m]['mean_of_three_means']/a[m]['mean_of_three_means']-1)*100
                                 for m in ['FrameTime','GameThreadTime','GPUTime','PhysicalUsedMB','VirtualUsedMB']}
    result['fps_change_percent'] = (b['fps_from_mean_frame']/a['fps_from_mean_frame']-1)*100
    result['normal_fps'] = [1000/r['csv']['FrameTime']['mean'] for r in result['normal_current']]
    (OUT/'whole-build-comparison.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    lines = ['# 四项 PIE 优化：整版追加实施与对照', '',
             '用户已要求直接应用所有优化。此前双客户端 15–20 FPS 作为粗略历史参考；本轮没有恢复旧代码，也没有把本轮追加收益称为整个计划的累计收益。', '',
             f'普通双客户端启动当前三轮约 **{min(result["normal_fps"]):.1f}–{max(result["normal_fps"]):.1f} FPS**。默认角色/镜头和正常生产，不注入600单位、500弹丸或特效预览；1280×720双视口。历史人口、镜头和分辨率未冻结，因此只作大致比较。', '',
             f'固定压力场景整版：**{a["fps_from_mean_frame"]:.2f} → {b["fps_from_mean_frame"]:.2f} FPS（约 +{result["fps_change_percent"]:.1f}%）**；Frame {a["FrameTime"]["mean_of_three_means"]:.2f} → {b["FrameTime"]["mean_of_three_means"]:.2f} ms。', '',
             '## 全帧结果', '', '| 轮次 | 追加前 FPS / Frame / GT / GPU ms | 整合版 FPS / Frame / GT / GPU ms | Frame P95 前→后 ms |', '|---|---|---|---|']
    for x,y in zip(before,final):
        def metrics(r):
            c=r['csv'];return f'{1000/c["FrameTime"]["mean"]:.2f} / {c["FrameTime"]["mean"]:.2f} / {c["GameThreadTime"]["mean"]:.2f} / {c["GPUTime"]["mean"]:.2f}'
        lines.append(f'| {x["pair"]} | {metrics(x)} | {metrics(y)} | {x["csv"]["FrameTime"]["p95"]:.2f} → {y["csv"]["FrameTime"]["p95"]:.2f} |')
    lines += ['', '同进程一专服两客户端，600个移动单位、三来源总500枚服务器弹丸，每客户端32条采矿/建造光束及32个枪口/命中预览。每轮热身10秒、采集30秒，质量3、镜头与服务器人口保持，真实World/LocalPlayer和接纳活跃量已保存。CSV和Trace覆盖整个编辑器进程。', '',
              f'GPU平均 {a["GPUTime"]["mean_of_three_means"]:.2f} → {b["GPUTime"]["mean_of_three_means"]:.2f} ms（+{result["changes_percent"]["GPUTime"]:.1f}%）。启用GPU光束转移了工作且新版本实际命中特效量增加，不能将总GPU差异只归给光束。当前仍主要受GT限制。Frame P95没有改善，整帧流畅性目标尚未全部达到。', '',
              f'整进程物理工作集均值 {a["PhysicalUsedMB"]["mean_of_three_means"]:.0f} → {b["PhysicalUsedMB"]["mean_of_three_means"]:.0f} MB，虚拟使用 {a["VirtualUsedMB"]["mean_of_three_means"]:.0f} → {b["VirtualUsedMB"]["mean_of_three_means"]:.0f} MB。不同编辑器进程驻留时间、资产缓存及接纳特效量不同；原始值保留，不能据此直接认定数据池节省MB或出现内存泄漏。', '',
              '## 全部追加项已应用', '',
              '| 项目 | 当前实现 / 正式配置 |', '|---|---|',
              '| 避障 A0–A4 | 复用连续快照/工作数组、环境代次、XY平方距离、单位12/环境2/最终12；30Hz三相及强制改令保持。 |',
              '| 场景UI A–D | World弱注册与修订、完整裁剪/模板/批次缓存、Capture名单分离、HUD双缓冲/F+2；追加四叶节点及分区失效。 |',
              '| 持续/瞬时特效 | 共用完整Bounds、采矿/建造Visible/Suspended独立唤醒、屏外瞬时生成裁剪与回池。 |',
              '| 特效资产 | 7.5/12cm Opaque+Unlit，实际8节点、Beam GPU、删除Beam求解与Spark表现碰撞；枪口/命中独立六层副本删除Sparks/Debris表现碰撞。 |',
              '| 飞行 A/B/C | 空Actor消除、完整身份姿态缓存、稳定数据池/Generation/Epoch、按分类推进/显示复用、Niagara静态/动态/时间和活跃/退出槽准备。 |',
              '| 追加飞行优化 | 曲线系数预计算、服务器候选代次标记、Laser每批256/导弹每批32（原索引合同保留）。 |',
              '| Ship服务器漏点 | 目标目录维护类别索引，仅获取新鲜Wingman快照并复用工作数组，保留ProjectileMovement和原结算顺序。 |', '',
              '正式Excel管线切换ID36/45/5/52及池ID4/38，基础缩放保持。原80节点CPU及原碰撞副本保留供可播放比较，不覆盖商城资源。服务器碰撞/伤害及每批16条/1000字节可靠启停协议保持。', '',
              '## 分项证据与仍然耗时的原因', '',
              '- 两种光束GT系统独占约1.82 → 0.146ms/帧，当前真实8节点+无Spark碰撞+GPU组合收益，不归给Opaque单独变化。',
              '- 当前压力轨迹命中系统独占约7.96ms、Niagara组件约1.81ms，实际接纳的短寿命命中特效比追加前多。ProjectileMovement约3.15ms，Slate本地操作约2.52ms；同步等待单独记录，不当作可直接删除的业务CPU成本。',
              '- 避障查询三轮均值1.25/1.29/1.32ms，活动批次P95为1.85/1.95/1.99ms，满足本轮600单位预算。UI来源发现稳态0；每客户端Update约0.17–0.24ms，Paint约0.16–0.20ms。整版比较同时改变多项，不提供独立拆叶加速倍数。',
              '- 原16/8节点独立对照经回读发现主Beam实际80，旧节点降幅结论作废；现有四份CPU候选已修正并保存，组合版测量前已确认全部Beam RI为8。', '',
              '## 验证与玩家入口', '',
              '源码UE5.7 GuLiStrikeEditor Win64 Development构建成功，BuildId与源码引擎一致。已有优化回归21/21、Actor回退1/1、Ship查询回退3/3通过。采矿、施工暂停/恢复/完成，HUD空提交/F+2/重挂载，Ship控制/重生以及中途加入均有真实运行回读。', '',
              'Map：`/Game/Maps/LVL_CommanderMassPrototype`。24个默认停用预览Actor、3个指南及1个观察相机；四个All入口为 `gs.Perf.Review start PR_Mining_All__Actor`、`PR_Construction_All__Actor`、`PR_Flash_AllMuzzle__Actor`、`PR_Flash_AllImpact__Actor`；`gs.Perf.Review stop`停止当前客户端全部预览。真实施工区域(15000,72000,902)，三来源区域(0,65000,902)，服务器 `gs.Flights.Load 500 45`。', '',
              '[可播放开始/峰值/停止对照](visual-review/index.html) · [全部数据](whole-build-comparison.json) · [已有回归](final-tests/results.json) · [生命周期](runtime-lifecycle-final-review.json) · [正式引用](formal-reference-final-readback.json) · [保存地图实体](final-saved-scene-readback.json)', '',
              '本轮“直接应用所有优化”是正式接入授权；新8节点/GPU/去表现碰撞组合与先前视觉确认的80节点CPU/保留火花碰撞版本不同。技术验证、性能结果与玩家视觉确认分别记录，当前新组合的玩家视觉反馈待记录。']
    (OUT/'whole-build-results.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    return result


def build_player():
    directory = OUT/'visual-review'
    takes = load(directory/'takes.json')
    assert len(takes)==8 and all(len(t['frames'])==21 for t in takes)
    assert all((directory/f['file']).is_file() and f['success'] for t in takes for f in t['frames'])
    for t in takes:
        t['display_role'] = ('Muzzle' if 'Muzzle' in t['variant'] else 'Impact') if t['role']=='Flash' else t['role']
    html = r'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>四项优化 · 实际PIE对照</title>
<style>*{box-sizing:border-box}body{margin:0;background:#151b20;color:#e7eee9;font:16px/1.5 system-ui,"Microsoft YaHei",sans-serif}main{max-width:1500px;margin:auto;padding:24px}h1{font-size:26px;margin:0 0 8px}p{color:#b9c9c3}a{color:#a8cab8}button,select{font:inherit;color:inherit;background:#303c3d;border:1px solid #52615d;border-radius:6px;padding:7px 12px;cursor:pointer}.controls{display:flex;gap:12px;flex-wrap:wrap;align-items:center;margin:16px 0}.players{display:grid;grid-template-columns:1fr 1fr;gap:16px}.card{border:1px solid #465251;border-radius:9px;overflow:hidden;background:#1d272c}.card h2{margin:0;font-size:17px;padding:12px}.card img{width:100%;aspect-ratio:16/9;object-fit:contain;display:block;background:#000}.card small{display:block;padding:12px;overflow-wrap:anywhere;color:#b9c9c3}input{flex:1;min-width:180px;accent-color:#89b5a1}.note{border-left:3px solid #9587ac;padding-left:12px}@media(max-width:800px){.players{grid-template-columns:1fr}main{padding:14px}}</style>
<main><h1>全部优化版 · 开始、峰值、停止对照</h1><p>左右均为真实PIE客户端画面，每段21帧；可播放、暂停和逐帧查看。左为上一交付资源，右为当前正式资源。</p>
<div class="controls"><label>用途 <select id="role"><option value="Mining">采矿 ID36</option><option value="Construction">建造 ID45</option><option value="Muzzle">枪口 ID52</option><option value="Impact">命中 ID5</option></select></label></div>
<div class="players"><section class="card"><h2 id="lt"></h2><img id="li" alt="上一交付版实际帧"><small id="lc"></small></section><section class="card"><h2 id="rt"></h2><img id="ri" alt="全部优化版实际帧"><small id="rc"></small></section></div>
<div class="controls"><button id="play">播放</button><button id="prev">上一帧</button><button id="next">下一帧</button><input id="frame" type="range" min="0" max="20" step="1" value="5" aria-label="帧位置"><span id="time"></span></div>
<div class="controls"><button data-frame="0">开始</button><button id="peak" data-frame="5">持续／峰值</button><button data-frame="15">停止后</button></div><p id="contract" class="note"></p><p id="stamp"></p>
<p>宽度均保持采矿7.5cm／建造12cm；新版本光束8节点、GPU、无力求解，火花无表现碰撞。枪口／命中六层、事件关联和原比例保留，取消火花／碎屑表现碰撞；服务器命中规则保持。</p>
<p>正式引用已按“直接应用所有优化”接入。新组合的玩家视觉反馈尚未记录。随机火花布局未固定，画面角落瞬时FPS不作结论。<a href="../whole-build-results.md">整版性能结果</a> · <a href="takes.json">原始帧与时间</a> · <a href="../final-saved-scene-readback.json">保存地图入口</a></p></main>
<script>const takes=__TAKES__,$=id=>document.getElementById(id);let timer=null;function pair(){return takes.filter(t=>t.display_role===$('role').value)}function stop(){if(timer)clearInterval(timer);timer=null;$('play').textContent='播放'}function draw(){let i=+$('frame').value,ts=pair();for(let n=0;n<2;n++){let p=n?'r':'l',t=ts[n];$(p+'t').textContent=n?'当前全部优化版':'上一交付版';$(p+'i').src=t.frames[i].file;$(p+'c').textContent=t.setup.system.replace(/ \(0x[0-9A-F]+\)/gi,'');}$('time').textContent=(i*.2).toFixed(1)+' s / '+i;$('stamp').textContent='实际游戏时间：左 '+(ts[0].frames[i].game_seconds-ts[0].frames[0].game_seconds).toFixed(3)+' s，右 '+(ts[1].frames[i].game_seconds-ts[1].frames[0].game_seconds).toFixed(3)+' s'}function change(){stop();$('contract').textContent=$('role').value==='Mining'?'采矿：绿色，不透明7.5cm。80节点CPU → 8节点GPU，端点火花不再与场景碰撞。':$('role').value==='Construction'?'建造：紫色，独立ID45，不透明12cm；保留扫描端点与宽度收束。':$('role').value==='Muzzle'?'枪口：基础缩放1；六层与事件关联保留。':'命中：基础缩放2；六层与事件关联保留。';draw()}$('role').onchange=change;$('frame').oninput=()=>{stop();draw()};$('play').onclick=()=>{if(timer)return stop();$('play').textContent='暂停';timer=setInterval(()=>{$('frame').value=(+$('frame').value+1)%21;draw()},200)};for(const [id,d]of[['prev',-1],['next',1]])$(id).onclick=()=>{stop();$('frame').value=Math.max(0,Math.min(20,+$('frame').value+d));draw()};for(const b of document.querySelectorAll('[data-frame]'))b.onclick=()=>{stop();$('frame').value=b.dataset.frame;draw()};change();</script></html>'''
    html = html.replace('function change(){stop();',
                        "function change(){stop();const flash=['Muzzle','Impact'].includes($('role').value);$('frame').value=flash?0:5;$('peak').dataset.frame=flash?'0':'5';")
    (directory/'index.html').write_text(html.replace('__TAKES__',json.dumps(takes,ensure_ascii=False).replace('</','<\\/')),encoding='utf-8')


if __name__ == '__main__':
    result = build_results()
    build_player()
    print(json.dumps({'success':True,'fps':{k:v['fps_from_mean_frame'] for k,v in result['aggregate'].items()},'normal_fps':result['normal_fps']},ensure_ascii=False))
