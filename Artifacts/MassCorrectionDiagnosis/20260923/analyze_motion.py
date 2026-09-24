import csv
import json
import math
import sys
from collections import Counter
from pathlib import Path

root = Path(__file__).parent
source = Path(sys.argv[1]) if len(sys.argv)>1 else Path('D:/UE5.7/test1/outputs/commander-selection-20260831/diagnostics/prediction-baseline-20260923-135915-40772.csv')
prefix=sys.argv[2] if len(sys.argv)>2 else 'motion'
rows = list(csv.DictReader(source.open(encoding='utf-8-sig')))
for r in rows:
    for k in r:
        if k != 'event':
            r[k] = float(r[k])

def stats(a):
    a = sorted(a)
    return {'n': len(a), 'min': a[0], 'mean': sum(a)/len(a), 'p50': a[len(a)//2],
            'p95': a[int((len(a)-1)*.95)], 'p99': a[int((len(a)-1)*.99)], 'max': a[-1]} if a else {}

def dist(a, b, key):
    return math.sqrt(sum((a[key + '_' + axis] - b[key + '_' + axis]) ** 2 for axis in 'xyz'))

frames = [r for r in rows if r['event'] == 'frame']
samples = [r for r in rows if r['event'] == 'sample_arrival']
pairs = []
latest = None
sample_index = 0
for a, b in zip(frames, frames[1:]):
    dt = b['local_seconds'] - a['local_seconds']
    if dt <= 0: continue
    while sample_index < len(samples) and samples[sample_index]['local_seconds'] <= b['local_seconds']:
        latest = samples[sample_index]
        sample_index += 1
    pairs.append({'t': b['local_seconds'], 'dt': dt, 'render_rate': (b['render_server_seconds'] - a['render_server_seconds'])/dt,
        'display_speed': dist(a,b,'presented')/dt, 'history_target_speed': dist(a,b,'authoritative')/dt,
        'display_error': math.sqrt(sum((b['presented_'+v]-b['authoritative_'+v])**2 for v in 'xyz')),
        'render_ahead_latest': b['render_server_seconds']-latest['sample_server_seconds'] if latest else None,
        'receipt_age': b['local_seconds']-latest['local_seconds'] if latest else None})

sample_pairs=[]
for a,b in zip(samples, samples[1:]):
    dt=b['sample_server_seconds']-a['sample_server_seconds']
    sample_pairs.append({'local_dt':b['local_seconds']-a['local_seconds'], 'server_dt':dt,
        'sequence_gap':b['sample_frame']-a['sample_frame'], 'implied_speed':dist(a,b,'sample')/dt if dt>0 else 0,
        'reported_speed':math.sqrt(sum(b['sample_velocity_'+v]**2 for v in 'xyz')),
        't': b['local_seconds']})
capture=json.loads((root/'motion-frames.json').read_text())['frames']
wall_deltas=[b['wall_seconds']-a['wall_seconds'] for a,b in zip(capture,capture[1:])]
report={'source':str(source), 'client':prefix,
    'events':dict(Counter(r['event'] for r in rows)), 'frame_ms':stats([r['dt']*1000 for r in pairs]),
    'world_time_fps':len(pairs)/sum(r['dt'] for r in pairs), 'frame_pairs':{k:stats([r[k] for r in pairs if r[k] is not None]) for k in pairs[0] if k!='t'},
    'sample_pairs':{k:stats([r[k] for r in sample_pairs]) for k in sample_pairs[0] if k!='t'},
    'display_over_1p5x_frames':sum(r['display_speed']>1080 for r in pairs),
    'display_at_3x_frames':sum(r['display_speed']>=2155 for r in pairs),
    'render_past_extrapolation_frames':sum(r['render_ahead_latest'] is not None and r['render_ahead_latest']>.1 for r in pairs),
    'hard_snaps': [r for r in rows if r['event']=='hard_snap'],
    'fastest': sorted(pairs,key=lambda r:r['display_speed'],reverse=True)[:20],
    'largest_sample_gaps': sorted(sample_pairs,key=lambda r:r['local_dt'],reverse=True)[:10]}
if prefix=='motion':
    report['wall_frame_ms']=stats([d*1000 for d in wall_deltas])
    report['wall_fps']=len(wall_deltas)/sum(wall_deltas)
    report['client']='UEDPIE_2; the first client output collided with this filename and was overwritten'
(root/(prefix+'-analysis.json')).write_text(json.dumps(report,indent=2),encoding='utf-8')
with (root/(prefix+'-derived.csv')).open('w', newline='',encoding='utf-8') as f:
    w=csv.DictWriter(f,fieldnames=list(pairs[0]));w.writeheader();w.writerows(pairs)
print(json.dumps(report,indent=2))
