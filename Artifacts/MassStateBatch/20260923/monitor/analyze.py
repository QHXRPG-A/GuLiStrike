import csv, json, math, re, shutil
from pathlib import Path
from datetime import datetime, timezone
from collections import Counter, defaultdict

root = Path(__file__).parent
capture = json.loads((root / 'capture.json').read_text(encoding='utf-8-sig'))
meta = capture['meta']
start, end = meta['wall_start'], meta['wall_end']
def stats(a):
    a = sorted(a)
    if not a: return {'n': 0}
    return {'n': len(a), 'mean': sum(a)/len(a), 'p50': a[(len(a)-1)//2],
            'p95': a[int((len(a)-1)*.95)], 'p99': a[int((len(a)-1)*.99)], 'max': a[-1]}
lines = (root.parent / 'editor-reloaded.log').read_text(encoding='utf-8-sig').splitlines()
records, window, traces, errors = [], [], [], []
for line in lines:
    m = re.match(r'^\[(\d{4}\.\d\d\.\d\d-\d\d\.\d\d\.\d\d:\d+)\]', line)
    if not m: continue
    t = datetime.strptime(m[1], '%Y.%m.%d-%H.%M.%S:%f').replace(tzinfo=timezone.utc).timestamp()
    if start <= t <= end+1:
        window.append(line)
        if 'PredictionTrace stopped:' in line and 'saved=1' in line:
            traces.append(Path(line.split('csv=',1)[1]))
        if any(s in line for s in ('Error:', 'Network saturated', 'batch rejected', 'receive window exceeded')):
            errors.append(line)
    if 'MassStream' not in line: continue
    row = dict(re.findall(r'(\w+)=([^ ]+)', line))
    row.update(time=t, receive='MassStreamRX' in line)
    for k,v in list(row.items()):
        if isinstance(v,str):
            try: row[k]=float(v) if '.' in v else int(v)
            except ValueError: pass
    records.append(row)
(root / 'window.log').write_text('\n'.join(window)+'\n',encoding='utf-8')
(root / 'stream-records.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
frames=capture['frames']
frame_dt=[b['elapsed']-a['elapsed'] for a,b in zip(frames,frames[1:])]
report={'meta':meta, 'window_seconds':end-start,
        'frame_ms':stats([v*1000 for v in frame_dt]), 'fps':len(frame_dt)/sum(frame_dt),
        'foreground_frames':sum(f['editor_process_foreground'] for f in frames), 'frames':len(frames),
        'sampling_overhead_ms':stats([f['capture_ms'] for f in frames]),
        'server_connections':{}, 'roster_checks':{}, 'traces':[], 'errors':errors}
for owner in sorted({r['owner'] for r in records if not r['receive']}):
    rows=[r for r in records if not r['receive'] and r['owner']==owner and start<=r['time']<=end]
    before=[r for r in records if not r['receive'] and r['owner']==owner and r['time']<start]
    baseline=before[-1] if before else rows[0]
    last=rows[-1]
    dt=last['time']-baseline['time']
    differences={k:last[k]-baseline[k] for k in ('stateBytes','poseBytes','observedBytes','chargedBytes','stateMerges','poseMerges','budgetDeferred','windowDeferred')}
    report['server_connections'][owner]={'seconds':dt, 'delta':differences,
        'bytes_per_second':{k:round(differences[k]/dt,1) for k in ('stateBytes','poseBytes','observedBytes','chargedBytes')},
        'maxima':{k:max(r[k] for r in rows) for k in ('stateQueue','poseQueue','stateAgeMs','poseAgeMs','inflight')},
        'last_queue':{'state':last['stateQueue'],'pose':last['poseQueue']},
        'cumulative_max_sample_gap_ms_start':baseline['maxSampleGapMs'],
        'cumulative_max_sample_gap_ms_end':last['maxSampleGapMs'],
        'observed_seconds_without_state_backlog':sum(r['stateQueue']==0 for r in rows),'observations':len(rows)}
rx=[r for r in records if r['receive'] and start<=r['time']<=end]
report['receivers']={'observations':len(rx),'ready_false':sum(not r['ready'] for r in rx),
                     'pending_batches_max':max(r['pendingBatches'] for r in rx),
                     'min_roster':min(r['roster'] for r in rx),'max_roster':max(r['roster'] for r in rx)}
for stage in ('initial','final'):
    worlds=capture[stage]
    authority={s['id']:s for s in worlds[0]['states']}
    result={'authority_count':len(authority),'active_orders':sum(bool(s['order']) for s in authority.values()),'clients':[]}
    for world in worlds[1:]:
        states={s['id']:s for s in world['states']}
        common=authority.keys()&states.keys()
        result['clients'].append({'world':world['world'],'count':len(states),
            'missing_ids':sorted(authority.keys()-states.keys()),'extra_ids':sorted(states.keys()-authority.keys()),
            'different_states':sum(states[i]!=authority[i] for i in common)})
    report['roster_checks'][stage]=result
def distance(a,b,key): return math.sqrt(sum((a[key+'_'+axis]-b[key+'_'+axis])**2 for axis in 'xyz'))
for index,path in enumerate(traces,1):
    dest=root/('client{}-trace.csv'.format(index));shutil.copy2(path,dest)
    rows=list(csv.DictReader(path.open(encoding='utf-8-sig')))
    for r in rows:
        for k in r:
            if k!='event': r[k]=float(r[k])
    f=[r for r in rows if r['event']=='frame'];s=[r for r in rows if r['event']=='sample_arrival']
    gaps=[{'at':b['local_seconds'],'receipt_ms':(b['local_seconds']-a['local_seconds'])*1000,
           'source_ms':(b['sample_server_seconds']-a['sample_server_seconds'])*1000,
           'distance_cm':distance(a,b,'sample'), 'reported_cm_s':math.sqrt(sum(b['sample_velocity_'+v]**2 for v in 'xyz'))}
          for a,b in zip(s,s[1:])]
    motion=[]
    for a,b in zip(f,f[1:]):
        dt=b['local_seconds']-a['local_seconds']
        if dt<=0:continue
        motion.append({'at':b['local_seconds'],'dt':dt,'speed':distance(a,b,'presented')/dt,
                       'history_speed':distance(a,b,'authoritative')/dt,'error_cm':math.sqrt(sum((b['presented_'+v]-b['authoritative_'+v])**2 for v in 'xyz'))})
    report['traces'].append({'client_index_by_stop_order':index,'source':str(path),'soldier':int(rows[0]['soldier_id']),
        'events':dict(Counter(r['event'] for r in rows)), 'receipt_gap_ms':stats([g['receipt_ms'] for g in gaps]),
        'source_gap_ms':stats([g['source_ms'] for g in gaps]),'display_speed':stats([m['speed'] for m in motion]),
        'display_error_cm':stats([m['error_cm'] for m in motion]),
        'frames_over_1p5x':sum(m['speed']>1080 for m in motion),'frames_at_3x':sum(m['speed']>=2155 for m in motion),
        'frames_over_3x':sum(m['speed']>2170 for m in motion),'moving_frames':sum(m['speed']>50 for m in motion),
        'longest_gaps':sorted(gaps,key=lambda g:g['receipt_ms'],reverse=True)[:5],
        'largest_errors':sorted(motion,key=lambda m:m['error_cm'],reverse=True)[:3]})
(root/'analysis.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False,indent=2))
