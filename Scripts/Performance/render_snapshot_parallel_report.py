"""Offline tables and figures for the existing snapshot comparison captures.

Run only after all PIE samples finish. Requires matplotlib; no editor connection.
"""
from __future__ import annotations
import argparse
import csv
import json
import math
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Artifacts/PerformanceOptimization20261010'


def cpu_csv(path):
    names = ['FrameTime','GameThreadTime','RenderThreadTime','RenderThreadTime_CriticalPath',
             'RHIThreadTime','GPUTime','PhysicalUsedMB','VirtualUsedMB','CPUUsage_Process','CPUUsage_Idle',
             'GPUMem/LocalBudgetMB','GPUMem/LocalUsedMB','Exclusive/GameThread/EventWait',
             'Exclusive/RenderThread/EventWait']
    values = {k:[] for k in names}
    with path.open(encoding='utf-8-sig',newline='') as f:
        reader=csv.reader(f); header=next(reader)
        indices={k:header.index(k) for k in names if k in header}
        for row in reader:
            try:
                if float(row[indices['FrameTime']])<=0:continue
            except (ValueError,IndexError):
                continue
            # UE omits trailing empty cells and emits non-numeric event columns.
            # Validate each numeric column independently, like the existing capture helper.
            for k,i in indices.items():
                try:
                    value=float(row[i])
                    if math.isfinite(value):values[k].append(value)
                except (ValueError,IndexError):
                    pass
    result={}
    for k,v in values.items():
        if v:
            v.sort();result[k]={'mean':statistics.fmean(v),'p95':v[min(len(v)-1,int(len(v)*.95))],
                              'max':v[-1],'n':len(v)}
    return result


def export_tables(analysis):
    rows=[]
    for run in analysis['runs']:
        r=run['result']; directory=OUT/'Paired'/run['name']
        stats=cpu_csv(directory/'frames.csv')
        row={'run':run['name'],'scene':r['scene'],'variant':r['variant'],'view':r['view'],
             'round':r['round'],'capture_errors':len(r.get('capture_errors',[])),
             'capture_seconds':r['capture_seconds'],'shadow_verification':r['verification_enabled'],
             'view_eligible':r.get('view_eligible',True),'frame_count':stats['FrameTime']['n'],
             'fps_from_mean_frame':1000/stats['FrameTime']['mean'],
             'moved_10cm':r['moving_units_displaced'],'presentation_eligible':r['presentation_eligible'],
             'visible_fractions':json.dumps(r['visible_fractions']),
             'visible_by_world':json.dumps(run['visible_by_world']),
             'build_manifest_sha256':r.get('build_manifest_sha256','build3-see-Build/verification-build3.json')}
        for k,v in stats.items():
            for kind in ['mean','p95','max']:row[k+'_'+kind]=v[kind]
        for k in ['StateTree.ScheduleMs','StateTree.Reads','StateTree.CacheHits','StateTree.AdvanceGroups',
                  'SceneUI.PaintMs','SceneUI.PrepareMs','SceneUI.PartitionMs','SceneUI.ComputeCpuMs',
                  'SceneUI.DispatchJoinMs','SceneUI.DispatchWaitMs','SceneUI.MergeMs','SceneUI.SubmitMs',
                  'Avoidance.TotalMs','Avoidance.PrepareMs','Avoidance.ComputeCpuMs','Avoidance.DispatchJoinMs',
                  'Avoidance.DispatchWaitMs','Avoidance.MergeMs','Avoidance.SubmitMs','Avoidance.Batches',
                  'Network.FlightRecordAccountingMs']:
            row[k]=run['counters'].get(k,{}).get('per_engine_frame')
        rows.append(row)
    keys=list(dict.fromkeys(k for row in rows for k in row))
    with (OUT/'paired-summary.csv').open('w',encoding='utf-8-sig',newline='') as f:
        w=csv.DictWriter(f,fieldnames=keys);w.writeheader();w.writerows(rows)
    (OUT/'cpu-frame-statistics.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')


def network_figure(a, b):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    plt.rcParams.update({'font.family':'Microsoft YaHei','axes.unicode_minus':False,'font.size':10})
    fig,axes=plt.subplots(4,2,figsize=(13,11),sharex='col',sharey='row',layout='constrained')
    for col,name in enumerate([a,b]):
        profiles=json.loads((OUT/'Paired'/name/'native-capture.json').read_text(encoding='utf-8'))
        server=next(w for w in profiles if w['network_samples'][0]['net_mode']==1)
        samples=server['network_samples'];origin=samples[0]['wall_seconds']
        ids=[c['connection'] for c in samples[0]['connections']]
        peer_guids=[c.get('player_guid') for c in samples[0]['connections']]
        for i,identity in enumerate(ids):
            series=[(s,c) for s in samples for c in s['connections'] if c['connection']==identity]
            times=[s['wall_seconds']-origin for s,c in series]
            valid=[(s,c) for s,c in series if c['rates_valid']]
            color=['#2563eb','#ea580c'][i%2]
            axes[0,col].plot([s['wall_seconds']-origin for s,c in valid],
                             [c['send_bytes_per_second']/1000 for s,c in valid],color=color,label=f'连接 {i+1}')
            axes[1,col].plot(times,[c['flight_peer']['queued_events']/1000 for s,c in series],color=color)
            axes[2,col].plot(times,[c['flight_peer']['head_event_age_ms']/1000 for s,c in series],color=color)
        axes[0,col].axhline(250,color='#64748b',ls='--',lw=1,label='配置预算 250 KB/s')
        axes[0,col].legend(fontsize=8)
        clients=[w for w in profiles if w['network_samples'][0]['net_mode']==3]
        for client in clients:
            guid=client['network_samples'][0]['connections'][0].get('player_guid')
            assert guid in peer_guids, 'Cannot match client clock to a server connection'
            i=peer_guids.index(guid)
            valid=[s for s in client['network_samples'] if 'pie_clock_estimate_error_ms' in s]
            axes[3,col].plot([s['wall_seconds']-origin for s in valid],
                             [s['pie_clock_estimate_error_ms'] for s in valid],
                             color=['#2563eb','#ea580c'][i%2],label=f'连接 {i+1} 客户端')
        axes[3,col].axhline(0,color='#64748b',lw=.7)
        axes[0,col].set_title('同版本串行基线' if col==0 else '条件缓存＋避障候选（未默认采用）')
        axes[3,col].set_xlabel('采样开始后的实际时间（秒）')
        for row in range(4):axes[row,col].grid(alpha=.2)
    for ax,label in zip(axes[:,0],['服务器每连接发送（KB/s）','可靠飞行队列（千条）','队首事件年龄（秒）','PIE 时钟估计误差（毫秒）']):
        ax.set_ylabel(label)
    fig.suptitle('600 单位＋三来源 500 附加飞行物：吞吐、积压与时钟分开观察',fontsize=14)
    fig.savefig(OUT/'network-pressure.png',dpi=150)
    fig.savefig(OUT/'network-pressure.svg')
    plt.close(fig)
    (OUT/'network-figure-inputs.json').write_text(json.dumps({'baseline':a,'combined':b,
        'KB_bytes':1000,'note':'UE connection bytes include all streams; never sum both endpoints. Head event age includes time before enqueue. Clock error is a same-process PIE diagnostic.'},ensure_ascii=False,indent=2),encoding='utf-8')


def p95_figure(analysis):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    plt.rcParams.update({'font.family':'Microsoft YaHei','axes.unicode_minus':False,'font.size':11})
    pairs=sorted([p for p in analysis['pairs'] if p['candidate']=='combined' and p['scene']=='dense200'],
                 key=lambda p:p['round'])
    assert len(pairs)==3
    deltas=[p['candidate_frame_p95_ms']-p['baseline_frame_p95_ms'] for p in pairs]
    threshold=analysis['decisions']['dense200/combined']['frame_p95_noise_ms']
    fig,ax=plt.subplots(figsize=(8.5,4.5),layout='constrained')
    bars=ax.bar([1,2,3],deltas,color='#e16b34',width=.45)
    ax.axhspan(0,threshold,color='#94a3b8',alpha=.2)
    ax.axhline(threshold,color='#475569',ls='--',label=f'同版本 A/A 最大差值 {threshold:.4f} ms')
    for bar,p,d in zip(bars,pairs,deltas):
        ax.text(bar.get_x()+bar.get_width()/2,d+.05,
                f"+{d:.4f} ms\n{p['baseline_frame_p95_ms']:.2f} → {p['candidate_frame_p95_ms']:.2f}",
                ha='center',va='bottom',fontsize=10)
    ax.set_xticks([1,2,3],['第 1 轮 A/B','第 2 轮 B/A','第 3 轮 A/B'])
    ax.set_ylim(0,max(deltas)+.55)
    ax.set_ylabel('整帧 P95 增量（ms；正值为变慢）')
    ax.set_title('200 单位：条件缓存＋避障候选组合未通过 P95')
    ax.grid(axis='y',alpha=.2)
    ax.set_axisbelow(True)
    ax.legend(loc='upper left',fontsize=9)
    fig.supxlabel('A/A 范围是三对样本的工程门槛，不是置信区间，也不证明退化的因果根因。',fontsize=9)
    fig.savefig(OUT/'p95-dense200.png',dpi=150)
    fig.savefig(OUT/'p95-dense200.svg')
    plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',default='combined-stress-onscreen-combined-r1-baseline')
    parser.add_argument('--combined',default='combined-stress-onscreen-combined-r1-combined')
    args=parser.parse_args()
    analysis=json.loads((OUT/'analysis.json').read_text(encoding='utf-8'))
    export_tables(analysis)
    network_figure(args.baseline,args.combined)
    p95_figure(analysis)


if __name__=='__main__':main()
