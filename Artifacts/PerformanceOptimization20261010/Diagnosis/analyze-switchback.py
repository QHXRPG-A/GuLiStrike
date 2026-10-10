"""Offline analysis; do not run Trace export during a measured window."""
import csv
import json
from pathlib import Path
import statistics
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'Scripts'))
from Performance.analyze_snapshot_parallel_review import summarize_run

OUT = Path(__file__).resolve().parent


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def stats(vals):
    v = sorted(vals)
    return {'mean':statistics.mean(v), 'p95':v[int(len(v)*.95)], 'max':max(v), 'n':len(v)}


def summarize(path, os_samples):
    run = summarize_run(path)
    begin, end = read(path.parent/'clock.json')['window_monotonic_seconds']
    c = run['counters']
    row = {'name':run['name'], 'variant':run['result']['variant'], 'begin':begin, 'end':end,
           'block':run['result']['candidate'], 'position':run['result']['round'],
           'visible':run['visible_by_world'], 'load_normalized':run['load_normalized'],
           'capture_errors':run['result']['capture_errors'], 'network':run['network']}
    row.update({f'{scope}_{stat}':run['result']['csv'][scope][stat]
                for scope in ['FrameTime','GameThreadTime','GPUTime'] for stat in ['mean','p95']})
    keys = ['StateTree.ScheduleMs','StateTree.ActorMs','StateTree.MassMs','StateTree.Conditions',
            'StateTree.Reads','StateTree.CacheHits','Avoidance.TotalMs','Avoidance.Solves',
            'Avoidance.ComputeCpuMs','Avoidance.DispatchJoinMs','Avoidance.DispatchWaitMs',
            'SceneUI.PaintMs','SceneUI.UpdateMs','SceneUI.Commands','SceneUI.PrepareMs','SceneUI.SubmitMs',
            'SceneUI.ComputeCpuMs','Muzzle.LifecycleMs','Muzzle.Active','Flight.Records','Flight.PresentationMs']
    row.update({key:c.get(key,{}).get('per_engine_frame',0) for key in keys})
    row['avoidance_parallel_excess_batches'] = c['Avoidance.Batches']['total']-c['Avoidance.Batches']['calls']
    row['avoidance_dispatch_calls'] = c['Avoidance.Batches']['calls']
    row['query_ms_per_1000_solves'] = c['Avoidance.ComputeCpuMs']['total']*1000/max(1,c['Avoidance.Solves']['total'])
    samples = [s for s in os_samples if begin <= s['qpc_seconds'] <= end and 'error' not in s]
    if len(samples) >= 2:
        a,b = samples[0],samples[-1]
        duration=b['qpc_seconds']-a['qpc_seconds']
        gt=b['gt_cpu_seconds']-a['gt_cpu_seconds']
        row['os_sample_seconds']=duration
        row['gt_cpu_wall_fraction']=gt/duration
        row['gt_cpu_ms_per_frame_estimate']=gt/duration*row['FrameTime_mean']
        row['gt_offcpu_ms_per_frame_estimate']=(1-gt/duration)*row['FrameTime_mean']
        row['process_used_cores']=(b['process_cpu_seconds']-a['process_cpu_seconds'])/duration
        row['system_cpu_busy_fraction']=1-(b['system_idle_seconds']-a['system_idle_seconds'])/(b['system_cpu_capacity_seconds']-a['system_cpu_capacity_seconds'])
    return row


def main():
    os_samples = read(OUT/'os-times.json')['samples']
    runs = [summarize(p,os_samples) for p in sorted((OUT/'Paired').glob('*/result.json'))]
    metrics = ['FrameTime_mean','FrameTime_p95','GameThreadTime_mean','GameThreadTime_p95',
               'StateTree.ScheduleMs','Avoidance.TotalMs','SceneUI.PaintMs','SceneUI.UpdateMs',
               'gt_cpu_ms_per_frame_estimate','gt_offcpu_ms_per_frame_estimate',
               'StateTree.Conditions','SceneUI.Commands','Muzzle.Active','Flight.Records']
    comparisons=[]
    for block in sorted(set(r['block'] for r in runs)):
        rows=sorted([r for r in runs if r['block']==block],key=lambda r:r['position'])
        assert len(rows)==3, (block,len(rows))
        left,mid,right=rows
        # Interpolate flanks by actual window centers; removes only linear time drift.
        centers=[(r['begin']+r['end'])/2 for r in rows]
        weight=(centers[1]-centers[0])/(centers[2]-centers[0])
        sign=-1 if block.startswith('BAB') else 1
        delta={k:sign*(mid[k]-(left[k]*(1-weight)+right[k]*weight)) for k in metrics}
        comparisons.append({'block':block,'variants':[r['variant'] for r in rows],
            'flank_weight':weight,'candidate_minus_baseline' if not block.startswith('AAA') else 'middle_minus_interpolated_flanks':delta,
            'raw_p95':[r['FrameTime_p95'] for r in rows], 'raw_mean':[r['FrameTime_mean'] for r in rows]})
    result={'runs':runs,'comparisons':comparisons,
            'limits':['Diagnostic windows are 15 seconds, not new adoption samples.',
                      'Natural battle and visibility change with time. Flank interpolation controls linear drift only.',
                      'Thread CPU averages use inner whole OS sample intervals (~14 s) and complete-window mean frame time; estimates are not per-frame critical paths.',
                      'Four switchback triplets and two AAA controls do not establish statistical confidence intervals.']}
    (OUT/'analysis.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(comparisons,ensure_ascii=False,indent=2))
    for r in runs:
        print(r['name'],'mean',round(r['FrameTime_mean'],3),'p95',round(r['FrameTime_p95'],3),
              'GT CPU/offCPU',round(r['gt_cpu_ms_per_frame_estimate'],3),round(r['gt_offcpu_ms_per_frame_estimate'],3),
              'conditions',round(r['StateTree.Conditions']), 'parallel_excess_batches',r['avoidance_parallel_excess_batches'])


if __name__=='__main__':
    main()
