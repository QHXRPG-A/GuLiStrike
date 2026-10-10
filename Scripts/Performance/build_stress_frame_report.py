"""Build a trace-backed Chinese diagnosis and interactive frame timeline."""
from __future__ import annotations
import html
import json
import re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'outputs/performance/20261009-stress-frame-analysis'
CAP=OUT/'paired/runtime-p1-frame-diagnosis'


def load(path):return json.loads(path.read_text(encoding='utf-8'))
def write(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')


def gpu_profile():
    queue=''; rows=[]
    for line in (OUT/'gpu-profile-frame.log').read_text(encoding='utf-8',errors='replace').splitlines():
        if 'GPU Profile for Frame' in line:queue=line.split('GPU Profile for Frame')[-1].strip()
        parts=line.split('|')[1:-1]
        if len(parts)!=13:continue
        try:
            rows.append({'queue':queue,'exclusive_ms':float(parts[5].strip().split()[0]),
                'inclusive_ms':float(parts[11].strip().split()[0]),
                'draws':int(parts[6]),'dispatches':int(parts[7]),'event':parts[12].strip(),
                'indent':len(parts[12])-len(parts[12].lstrip())})
        except ValueError:pass
    write(OUT/'gpu-profile-rows.json',rows)
    return rows


def main():
    capture=load(CAP/'result.json'); stats=load(CAP/'insights-windowed/summary.json')
    analysis=load(OUT/'frame-analysis.json'); inventory=load(OUT/'post-window-inventory.json')
    settings=load(OUT/'pool-settings-readback.json')
    gpu=gpu_profile(); cs=capture['csv']; frame_ms=cs['FrameTime']['mean']
    gt={r['timer']:r for r in stats['timers']}
    fs={f['name']:f for f in analysis['frames']}
    before=load(CAP/'counters-before.json'); after=load(CAP/'counters-after.json')
    def delivery(data):
        value=data['worlds'][0]['flight_delivery']
        return {k:int(v) for k,v in re.findall(r'(active|created|ended|payloadBytes|batches|queued)=(\d+)',value)}
    db,da=delivery(before),delivery(after)
    diff={k:da[k]-db[k] for k in ['created','ended','payloadBytes','batches']}
    impacts=[]
    for w in inventory['worlds'][1:]:
        row=next(v for k,v in w['niagara'].items() if 'NS_MachineGunImpact_AllOptimizations.' in k)
        impacts.append({'world':w['world'],'game_seconds':w['game_seconds'],**row,
            'inactive_unassigned_components':w['niagara']['<none>']['components']})
    roots=[r for r in gpu if r['event']=='<root>']
    ribbons=[r for r in gpu if r['event']=='Niagara GPU Ribbons']
    summary={'frame_mean_ms':frame_ms,'fps':1000/frame_ms,
        'gt_mean_ms':cs['GameThreadTime']['mean'],'gpu_mean_ms':cs['GPUTime']['mean'],
        'frame_p95_ms':cs['FrameTime']['p95'],'csv_frames':cs['FrameTime']['n'],
        'trace_complete_frames':stats['frames'],'impact_gt_exclusive_mean_ms':gt['NS_MachineGunImpact_AllOptimizations']['exclusive_ms_per_frame'],
        'impact_gt_all_cpu_exclusive_mean_ms':next(r['exclusive_ms_per_frame'] for r in stats['all_cpu_vfx'] if r['timer']=='NS_MachineGunImpact_AllOptimizations'),
        'actual_impacts_post_window':impacts,'producer_delta':diff,'producer_boundary_before':db,'producer_boundary_after':da,
        'gpu_profile_roots':roots,'gpu_profile_ribbons':ribbons,
        'gpu_ribbon_sum_ms':sum(r['inclusive_ms'] for r in ribbons),
        'source_build_id':json.loads((ROOT/'Binaries/Win64/UnrealEditor.modules').read_text())['BuildId'],
        'formal_assets':inventory['formal_effects'],'asset_settings':settings['assets'],
        'scope':'Process-wide dedicated server + two clients. CPU costs exclusive per engine frame; worker CPU and GPU queues do not add to GT. GPU profile and viewport PNGs are outside CSV/trace window.'}
    write(OUT/'diagnosis.json',summary)
    labels=[('GuLiSceneUI_Update','场景UI更新'),
        ('MulticastFlightBatch','飞行批次接收/应用'),('GuLiCommanderMassStateTreeProcessor_0','Mass StateTree'),
        ('GuLiCommanderPresentation_Interpolation','双客户端单位插值'),('NiagaraComponent','Niagara组件通用开销'),
        ('ProcessLocalPlayerSlateOperations','本地玩家Slate操作'),('Slate::Prepass','Slate Prepass'),
        ('Slate_PaintSlowPath','Slate慢路径绘制'),('GuLiCommander_PredictiveAvoidanceCandidateQuery','避障候选查询'),
        ('GuLiClientFlightPrediction','双客户端飞行预测'),('GuLiSceneUI_Update','场景UI更新'),('GuLiSceneUI_Paint','场景UI绘制')]
    table='\n'.join(f'| {label} | {gt[name]["exclusive_ms_per_frame"]:.3f} | {100*gt[name]["exclusive_ms_per_frame"]/frame_ms:.1f}% |' for name,label in names if name in gt)
    frame_table='\n'.join(f'| {f["name"]} | {f["engine_tick_ms"]:.3f} | {next(r["exclusive_ms"] for r in f["top_gt_timers"] if r["timer"]=="NS_MachineGunImpact_AllOptimizations"):.3f} | {next(r["exclusive_ms"] for r in f["top_gt_timers"] if r["timer"]=="Projectile Movement"):.3f} |' for f in analysis['frames'])
    refs='\n'.join(f'- ID{x["id"]}：`{x["path"].split(".")[0]}`' for x in inventory['formal_effects'])
    report=f'''# 600 移动单位 / 500 弹丸：PIE 截帧分析


## 本次实际捕获

源码 UE5.7 编辑器进程 72432，`GuLiStrikeEditor Win64 Development`，地图 `LVL_CommanderMassPrototype`；同进程一专服、两客户端，各 1280×720，质量3、固定镜头、VSync/帧率上限关闭。i9-14900KF / RTX4090。引擎与项目 BuildId 均为 `{summary['source_build_id']}`；本轮未修改运行代码或资源，未重新编译。

600 移动 Mass 单位，三来源共500枚弹丸；每客户端额外16采矿+16建造持续光束、16枪口+16命中循环预览，与上次整版压力入口一致。接受器运行时长仅由45延至90秒，以便计时结束后仍能截帧；活跃目标、运动/碰撞/网络规则保持。**这是500活弹目标并持续补充的场景，同时存在高频创建/结束事件。**

热身10秒，原生 CSV/CPU/GPU/frame Trace计时30秒；CSV共{summary['csv_frames']}帧，QPC窗口内{summary['trace_complete_frames']}个完整引擎帧。固定相机、人口600及测量窗口通过检查。实际服务端边界活跃 {db['active']}→{da['active']}，客户端数据槽开始324/324、结束475/469；客户端窗口内平均约463/463，Mesh Actor平均约93/93、P95为125。

| 指标 | 平均 | P95 |
|---|---:|---:|
| 整帧 | {frame_ms:.3f} ms / {1000/frame_ms:.2f} FPS | {cs['FrameTime']['p95']:.3f} ms |
| GameThread | {cs['GameThreadTime']['mean']:.3f} ms | {cs['GameThreadTime']['p95']:.3f} ms |
| GPU | {cs['GPUTime']['mean']:.3f} ms | {cs['GPUTime']['p95']:.3f} ms |
| 进程物理内存 | {cs['PhysicalUsedMB']['mean']:.0f} MB | {cs['PhysicalUsedMB']['p95']:.0f} MB |

本次与上次三轮64.41ms/15.53FPS吻合。此前双客户端15–20FPS仅为用户历史观察，没有恢复旧代码，也没有把这个数字当作本次同负载基线。

## 游戏线程热点：整个编辑器进程 / 每引擎帧

以下为 **exclusive独占时间**，已剔除GPU同名计时。服务器、两个客户端和编辑器共享此游戏线程，不能把一项时间再乘2。并行工作线程的CPU量不相加为整帧时间。

| 位置 | 平均独占 ms/帧 | 占整帧 |
|---|---:|---:|
{table}

此外，`UWorld_Tick`未细分部分约5.73ms，`ProcessUntilTasksComplete`自身约2.84ms、`WaitForTasks`约1.09ms。前者包含执行嵌套任务，不能将其19.64ms inclusive全部当成空等；等待与算法CPU工作分别保留。

**命中特效不是仅有一段模拟成本。** 63.57ms普通帧内，命中特效8.887ms独占由：网络事件处理期间3.043ms、帧末Niagara渲染数据更新3.910ms、任务执行期间1.928ms等构成。30秒内该系统全CPU线程独占工作量约15.412ms/引擎帧，其中GT8.174ms；两项不能相加。

## 真实普通帧、P95帧和最慢帧

选帧依据为QPC窗口内完整 `FEngineLoop::Tick` 时长。与CSV P95口径分开；每帧GT所有exclusive之和均与该帧时长吻合。导出保留原生时间线顺序与高精度Duration，避免 %.9g Start/End的约10微秒舍入破坏微小Scope归属。

| 选取 | 引擎Tick ms | 命中特效独占 ms | 物理弹丸移动独占 ms |
|---|---:|---:|---:|
{frame_table}

**88.021ms的P95帧：** 命中特效17.389ms，其中9.883ms发生在飞行网络事件应用期间、5.303ms在帧末更新；物理弹丸移动7.255ms、StateTree3.905ms、WaitForTasks4.762ms。该帧由集中接收、特效启动/提交和移动开销共同放大。`MulticastFlightBatch`有嵌套同名Scope，其inclusive不能直接当作RPC总耗时累加。


## 特效实例与事件吞吐

两个客户端计时后的同场景盘点：ID5分别 **249 / 209活跃组件**，总组件281/241，差额均32。真实Niagara资产 `MaxPoolSize=32`、`PoolPrimeSize=0`、无固定Tick、无预热、EffectType为空。原生Niagara池回收代码在空闲容量达到MaxPoolSize后执行DestroyComponent，后续不足时NewObject。**32是空闲池上限，绝非32个同时活跃的上限。**

边界快照间服务器创建增加 **{diff['created']:,}**、结束增加 **{diff['ended']:,}**，发送{diff['batches']:,}批/{diff['payloadBytes']:,}字节（两连接合计），可靠队列1462→1912。约30秒窗口伴随桥调用边界，折算只是每秒800多次创建和结束的近似数量。压力不只来自500条轨迹计算；短寿命/命中与补充同时驱动客户端事件与六层特效实例。

ObjectIterator还看到两客户端共30,425个无资产、非活跃Niagara对象，其中包含已销毁待GC/诊断残留，**不能据此宣布内存泄漏或3万个在模拟**。空闲池容量和组件再初始化值得优先对照，但本轮没有改池容量，也没有证明扩大池就能省多少ms。

已核对两个命中入口：生产端普通Linear事件 `bUseCatalogImpact=false` 由ApplyState播放；逻辑Projectile终止 `true` 在ApplyFlightEvent转换为Linear后播放，原State在ApplyState不满足Linear条件。本场景证据不支持“两个入口必然重复播放”，不把它当已确认根因。

## GPU单帧

使用UE5.7原生 **ProfileGPU** 捕获实际队列、Draw/Dispatch和Pass树；没有安装RenderDoc/PIX，因此没有伪称取得.rdc或逐Draw材质回放。CPU/GPU Trace、原生GPU帧树和实际客户端PNG已保存。

这次GPU单帧在计时窗口之后：Graphics忙碌 **21.930ms**（3056 Draw、3768 Dispatch），Compute **2.128ms**（186 Dispatch），Copy无记录工作；队列可并行，不能相加为24.058ms的整帧。

两个视图相关的 `Niagara GPU Ribbons` 分别 **4.752 / 4.501ms**，合计9.253ms，约该帧Graphics的42.2%；共2560 Dispatch。SceneRender两个主要视图分支10.861/9.194ms。Ribbon生成/排序固定调度成本是明确GPU热点；降低节点数或改Opaque并不会消除这些计算Pass。CPU发射器也可能采用GPU Ribbon初始化，当前Pass树没有资产身份，不能把9.253ms全算到采矿/建造或某个命中资产上。

GPU依然低于GT平均64ms。本轮不以牺牲分辨率、关闭光照、缩减人口/活弹或静默丢可靠事件来提高测试FPS。

## 已达预算的部分与当前改进顺序

避障查询原生World聚合 **1.223ms/引擎帧，活动P95 1.926ms**，达到≤1.6/≤3ms预算。双客户端飞行预测GT独占约0.499ms；原生每客户端表现总计1.301/1.346ms。场景UI Update0.239/0.171ms、Paint0.160/0.193ms，来源稳态发现0。采矿+建造系统GT独占合计0.160ms。它们当前只占整帧较小部分。

1. **先处理命中特效实例。** 增加真正启动/新建/池复用/完成计数；用更充足空闲池及预分配做3对容量对照，确认是否减少高峰重建；进一步把同用途命中组织为稳定槽位批次，保留六层轮廓、事件跟随、随机顺序、朝向和期限，兼容模块才采用GPU。
2. **处理Ship集中生命周期。** 保留Actor/ProjectileMovement/NotifyHit权威路径，验证服务器Actor复用和完全状态复位，减少125个一起到期、一起Destroy/Spawn的峰值；同时分析移动/场景Sweep。不得更改寿命、伤害/命中顺序或仅错开压力事件冒充优化。
3. **复用飞行批次解码/应用容量。** 保持可靠创建/结束、16条/1000字节及原事件顺序，减少临时数组、每事件配置解析和组件参数重建。现有8批信用和队列压力分别计量。
4. **减少GPU Ribbon调度数量。** 按资产与用途做隔离帧、尝试跨实例批次/共享生成结果；分别记录CPU和GPU变化。Opaque与8节点的视觉合同保留；本轮不直接切换未经对照的新候选。

这是本轮截帧后的新热点处理顺序，尚未执行这些追加修复或承诺新增FPS收益。30FPS需要将总GT从64.33降至33.33ms量级，仅继续减少约1ms的查询或预测不足以达到该目标。

## 正式应用确认与证据入口

用户最新明确“直接替换成新特效新逻辑”。正式使用授权已记录，原先加宽Opaque、保留火花碰撞的视觉确认与现在组合的逐帧效果反馈仍分开。

{refs}

DataPool、曲线预计算、VisitStamps、UI四叶、256/32批次等使用已加载整版默认；采矿7.5cm/建造12cm、Opaque Unlit、真实8节点/GPU、无表现碰撞与独立闪光保持。没有恢复旧代码或旧资源。

- [交互帧时间线与两客户端画面](index.html)
- [CSV整帧数据](paired/runtime-p1-frame-diagnosis/frames.csv)、[原始Unreal Insights轨迹](paired/runtime-p1-frame-diagnosis/session.utrace)
- [窗口内GT计时与CPU资格](paired/runtime-p1-frame-diagnosis/insights-windowed/summary.json)
- [三个真实帧的分析](frame-analysis.json)、[完整事件导出目录](selected-frames/median-events.csv)
- [GPU单帧原始队列/Pass报告](gpu-profile-frame.log)、[GPU结构化行](gpu-profile-rows.json)
- [客户端1截图](client-1.png)、[客户端2截图](client-2.png)、[截图时间与范围](viewport-frames.json)
- [Niagara对象盘点](post-window-inventory.json)、[池配置只读回读](pool-settings-readback.json)
- [测量场景/人口/有效性](paired/runtime-p1-frame-diagnosis/result.json)、[控制恢复](restored-runtime.json)

截图用ReadPixels、对象盘点和ProfileGPU均在计时窗外；截图HUD显示8FPS受盘点/ReadPixels影响，不能代替窗口15.55FPS，也不与选取的CPU帧宣称同步。结束后停止本轮PIE，恢复CaptureSeconds20、Slate节流1及原生GPU分析选项，新优化仍开。此轮仅新增诊断脚本和报告，生产代码/资产未改、对应Map无需重建或再次保存。
'''
    (OUT/'report.md').write_text(report,encoding='utf-8')
    payload={**summary,'frames':[{**f,'timeline':load(OUT/'selected-frames'/(f['name']+'-gt-timeline.json'))} for f in analysis['frames']],
             'mean_hotspots':[{**gt[n],'label':label} for n,label in names if n in gt]}
    data=json.dumps(payload,ensure_ascii=False).replace('</','<\\/')
    template='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>PIE压力帧分析</title>
<style>body{margin:0;background:#10171e;color:#e7eef4;font:16px/1.65 "Microsoft YaHei",sans-serif}main{max-width:1380px;margin:auto;padding:26px}h1{font-size:28px;margin:0 0 8px}h2{font-size:19px;margin:24px 0 10px}.muted{color:#a5b5c4}.metrics{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;margin:22px 0}.card{padding:16px;border:1px solid #334653;border-radius:10px;background:#18232d}.value{font-size:26px;color:#8ed6c6}button{background:#253d50;color:white;border:1px solid #5a7788;border-radius:7px;padding:10px 16px;cursor:pointer;margin-right:10px}button.selected{background:#376f76}a{color:#94d2ff}table{border-collapse:collapse;width:100%;font-size:14px}td,th{text-align:left;border-bottom:1px solid #30414e;padding:8px}#chart{background:#18232d;border-radius:8px;overflow:auto;padding:8px}.shots{display:grid;grid-template-columns:1fr 1fr;gap:12px}.shots img{width:100%;border-radius:8px}.legend span{margin:0 16px 0 0;white-space:nowrap}.summary{padding:16px 20px;border-left:3px solid #ffb76b;background:#1c2b37}#tip{display:none;position:fixed;background:#020b12ee;border:1px solid #6c8a9b;padding:10px;max-width:560px;border-radius:6px;pointer-events:none;z-index:5;font-size:13px}.cols{display:grid;grid-template-columns:1fr 1fr;gap:20px}@media(max-width:900px){.metrics{grid-template-columns:1fr 1fr}.shots,.cols{grid-template-columns:1fr}}</style>
<main><h1>600移动单位 · 500飞行弹丸 · 双客户端PIE</h1><div class="muted">源码UE5.7 / 同进程专服+双客户端 / 1280×720 / 质量3 / 新特效和新逻辑已正式生效</div>
<h2>真实游戏线程帧时间线</h2><div id="buttons"></div><p id="frameinfo" class="muted"></p><div class="legend"><span style="color:#fa947c">■ 特效</span><span style="color:#a895ed">■ 网络</span><span style="color:#e8b871">■ 弹丸</span><span style="color:#70bdbc">■ UI</span><span style="color:#8bc47e">■ Mass/避障</span><span style="color:#909fb0">■ 任务/引擎</span></div><div id="chart"></div>
<div class="cols"><section><h2>该帧独占耗时</h2><table><thead><tr><th>Scope</th><th>ms</th><th>调用</th></tr></thead><tbody id="framecost"></tbody></table></section><section><h2>30秒平均独占耗时 / 引擎帧</h2><table><thead><tr><th>位置</th><th>ms</th></tr></thead><tbody id="means"></tbody></table><p class="muted">包含服务器和两个客户端；同名嵌套inclusive、其他线程工作量、GPU队列不能叠加为整帧耗时。</p></section></div>
<h2>GPU单帧队列与Ribbon生成</h2><div id="gpuinfo" class="card"></div><p class="muted">图形/计算队列并行；9.253ms是Niagara Ribbon组成本，当前GPU Pass树不包含资产身份，不能全部归给某一种激光。</p>
<h2>实际客户端画面</h2><div class="shots"><figure><img src="client-1.png" alt="客户端1实际帧"><figcaption>客户端1 · 计时窗外ReadPixels</figcaption></figure><figure><img src="client-2.png" alt="客户端2实际帧"><figcaption>客户端2 · 同一压力场景</figcaption></figure></div><p class="muted">画面HUD瞬时8FPS受到对象盘点和ReadPixels停顿影响；性能结论使用独立30秒窗口15.55FPS。</p>
<p><a href="report.md">完整中文报告</a> · <a href="paired/runtime-p1-frame-diagnosis/session.utrace">原始.utrace</a> · <a href="gpu-profile-frame.log">原始GPU帧树</a> · <a href="frame-analysis.json">完整帧分析JSON</a></p></main><div id="tip"></div>
<script>const D=__DATA__;const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const color=s=>/Niagara|NS_|Sparks|Glow|FXSystem|Emitter/.test(s)?'#fa947c':/Net|Bunch|Multicast|Receive|Replic/.test(s)?'#a895ed':/Projectile|Collision Sphere|BP_Ship/.test(s)?'#e8b871':/Slate|UI|HUD|Viewport/.test(s)?'#70bdbc':/Mass|Commander|Avoidance/.test(s)?'#8bc47e':'#909fb0';
document.querySelector('#metrics').innerHTML=[['平均FPS',D.fps.toFixed(2)],['整帧',D.frame_mean_ms.toFixed(2)+' ms'],['游戏线程',D.gt_mean_ms.toFixed(2)+' ms'],['GPU',D.gpu_mean_ms.toFixed(2)+' ms']].map(([l,v])=>'<div class="card">'+l+'<div class="value">'+v+'</div></div>').join('');
document.querySelector('#means').innerHTML=D.mean_hotspots.map(x=>'<tr><td>'+esc(x.label)+'</td><td>'+x.exclusive_ms_per_frame.toFixed(3)+'</td></tr>').join('');
document.querySelector('#gpuinfo').innerHTML=D.gpu_profile_roots.map(x=>'<div>'+esc(x.queue)+': <b>'+x.inclusive_ms.toFixed(3)+'ms</b> · '+x.draws+' Draw · '+x.dispatches+' Dispatch</div>').join('')+'<div>Niagara GPU Ribbons: <b>'+D.gpu_ribbon_sum_ms.toFixed(3)+'ms</b>（两个视图分支4.752 / 4.501ms）</div>';
const titles={median:'普通帧',p95:'P95帧',worst:'最慢帧'};D.frames.forEach((f,i)=>{const b=document.createElement('button');b.textContent=titles[f.name]+' '+f.engine_tick_ms.toFixed(2)+'ms';b.onclick=()=>show(i);document.querySelector('#buttons').appendChild(b)});
function show(i){const f=D.frames[i];document.querySelectorAll('button').forEach((b,j)=>b.classList.toggle('selected',i===j));document.querySelector('#frameinfo').textContent='Trace '+f.trace_start.toFixed(5)+'–'+f.trace_end.toFixed(5)+'s · GT独占合计 '+f.gt_exclusive_accounted_ms.toFixed(3)+'ms · 悬停查看Scope，细小叶节点省略，完整数据保存在CSV。';const W=1320,pad=12,scale=(W-2*pad)/f.engine_tick_ms,maxDepth=Math.max(...f.timeline.map(x=>x.depth)),row=27,H=(maxDepth+2)*row+28;let svg='<svg width="'+W+'" height="'+H+'" xmlns="http://www.w3.org/2000/svg">';for(let m=0;m<=f.engine_tick_ms;m+=10){const x=pad+m*scale;svg+='<path d="M'+x+' 23V'+H+'" stroke="#324957"/><text x="'+x+'" y="16" fill="#b6c6d0" font-size="11">'+m+'ms</text>'}f.timeline.forEach((e,j)=>{const x=pad+e.start_ms*scale,y=28+e.depth*row,w=Math.max(1,(e.end_ms-e.start_ms)*scale);svg+='<g data-event="'+j+'"><rect x="'+x+'" y="'+y+'" width="'+w+'" height="23" fill="'+color(e.timer)+'" rx="3"/>';if(w>90){const txt=e.timer.slice(0,Math.floor(w/7));svg+='<text x="'+(x+4)+'" y="'+(y+16)+'" fill="#0a1722" font-size="11">'+esc(txt)+'</text>'}svg+='</g>'});svg+='</svg>';document.querySelector('#chart').innerHTML=svg;document.querySelector('#chart').querySelectorAll('[data-event]').forEach(g=>{g.onmousemove=event=>{const e=f.timeline[Number(g.dataset.event)],tip=document.querySelector('#tip');tip.innerHTML=esc(e.timer)+'<br>区间 '+e.start_ms.toFixed(3)+'–'+e.end_ms.toFixed(3)+'ms<br>独占 '+e.exclusive_ms.toFixed(3)+'ms';tip.style.display='block';tip.style.left=Math.min(innerWidth-580,event.clientX+15)+'px';tip.style.top=Math.min(innerHeight-100,event.clientY+12)+'px'};g.onmouseleave=()=>document.querySelector('#tip').style.display='none'});document.querySelector('#framecost').innerHTML=f.top_gt_timers.slice(0,15).map(x=>'<tr><td>'+esc(x.timer)+'</td><td>'+x.exclusive_ms.toFixed(3)+'</td><td>'+x.calls+'</td></tr>').join('')}show(0);
</script></html>'''
    (OUT/'index.html').write_text(template.replace('__DATA__',data),encoding='utf-8')
    print(json.dumps({'report':str(OUT/'report.md'),'viewer':str(OUT/'index.html'),'diagnosis':summary},ensure_ascii=False,indent=2))


if __name__=='__main__':main()
