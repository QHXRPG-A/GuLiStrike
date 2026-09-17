"""Current-source CPU comparison, preserving load failures and inclusive scope boundaries."""
import csv
import json
import math
import sys
from collections import Counter, defaultdict
from pathlib import Path
from statistics import median

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parents[1] / 'Scripts'))
from analyze_commander_move_stress import stats, percentile, read_engine

def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))

result = []
for run in sorted(ROOT.glob('retest*-n1200-c*')):
    if not (run / 'comparison-window.json').exists():
        continue
    begin, end = read(run / 'comparison-window.json')['window_monotonic_seconds']
    processes = rows(run / 'processes.csv')
    for role in ('client1', 'client2'):
        directory = run / role
        if not (directory / 'summary.json').exists(): continue
        summary = read(directory / 'summary.json')
        selected = [r for r in rows(directory / 'frames.csv')
                    if begin <= summary['capture_start_monotonic_seconds'] + float(r['seconds']) < end]
        moving = [float(r['observed_moving']) for r in selected]
        frame = stats([float(r['frame_ms']) for r in selected])
        scopes = {}
        out = directory / 'insights'
        if (out / 'timers-gamethread.csv').exists():
            # UE's thread-filtered exporter also returns GPU-only rows; remove exact GPU duplicates.
            gpu = Counter(tuple(sorted(r.items())) for r in rows(out / 'timers-gpu.csv'))
            for r in rows(out / 'timers-gamethread.csv'):
                key = tuple(sorted(r.items()))
                if gpu[key]: gpu[key] -= 1; continue
                name = r['Name']
                if any(p in name for p in ('GuLiCommander', 'GuLiPose_', 'Recreate', 'DeferredRenderUpdates',
                                          'BatchUpdateInstancesTransforms', 'MassDeferredCommand_Execute', 'FEngineLoop::Tick')):
                    count, incl, excl = int(r['Count']), float(r['Incl'])*1000, float(r['Excl'])*1000
                    scopes[name] = {'count': count, 'hz': count/(end-begin), 'inclusive_ms': incl,
                                    'exclusive_ms': excl, 'inclusive_ms_per_frame': incl/frame['n'],
                                    'mean_ms_per_call': incl/count if count else 0}
        proc = [r for r in processes if r['Role'] == role and begin <= float(r['QpcSeconds'])+16777216 < end]
        engine = read_engine(directory / 'engine.csv')
        memory = {k: v['max'] for k,v in engine.items() if 'CacheBytes' in k}
        item = {'run': run.name, 'phase': run.name.split('-')[0], 'clients': int(run.name[-1]), 'role': role,
                'window_monotonic_seconds': [begin,end], 'frame_ms': frame,
                'alive_min': min(float(r['alive']) for r in selected),
                'moving': stats(moving), 'moving_p05': percentile(moving,.05),
                'load_pass': min(float(r['alive']) for r in selected)==1200 and percentile(moving,.05)>=1140,
                'frame_budget_pass': frame['p95'] <= (16.67 if run.name.endswith('c1') else 33.33),
                'scopes': scopes, 'explicit_cache_high_water_bytes': memory,
                'engine_full_capture': {k: engine.get(k) for k in ('GameThreadTime','RenderThreadTime','GPUTime','View/Speed')},
                'process_samples': len(proc),
                'process': {key: stats([float(row[key]) for row in proc]) for key in
                            ('CpuCoreEquivalents','WorkingSetMiB','PrivateMiB')} if proc else {}}
        result.append(item)

(ROOT/'comparison.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
columns = ['run','role','mean_ms','p95_ms','p99_ms','moving_p05','load_pass','frame_budget_pass']
with (ROOT/'frames-comparison.csv').open('w',newline='',encoding='utf-8-sig') as stream:
    writer=csv.DictWriter(stream,fieldnames=columns);writer.writeheader()
    for r in result:
        writer.writerow({'run':r['run'],'role':r['role'],'mean_ms':r['frame_ms']['mean'],
                         'p95_ms':r['frame_ms']['p95'],'p99_ms':r['frame_ms']['p99'],
                         **{k:r[k] for k in columns[5:]}})

groups=defaultdict(list)
for r in result: groups[(r['phase'],r['clients'],r['role'])].append(r)
for group,data in sorted(groups.items()):
    print(group, 'rounds',len(data), 'mean/P95/P99',
          [round(median(r['frame_ms'][k] for r in data),3) for k in ('mean','p95','p99')],
          'load',all(r['load_pass'] for r in data))
