"""Summarize captured PIE observations; does not run or drive tests."""
from pathlib import Path
import json,re,math,collections
root=Path(__file__).parent
samples=[];events=[]
for line in (root/'runtime-slots.jsonl').read_text(encoding='utf-8').splitlines():
 try:r=json.loads(line)
 except json.JSONDecodeError:continue
 if 'authority' in r:samples.append(r)
 if 'event' in r:events.append(r['event'])
logs=[]
for line in (root/'editor-3.log').read_text(encoding='utf-8',errors='replace').splitlines():
 if 'MassMoveLatency ' not in line:continue
 logs.append(dict(re.findall(r'([\w]+)=([^\s]+)',line)))
out={'clock_scope':'Single editor PID, separate PIE worlds; all latency clocks in this capture share FPlatformTime. Green timing is render submission, not screen presentation.','commands':[]}
for event in events:
 if event['action']!='move':continue
 cmd=event['command'];ids=set(event['selected']);rows=[r for r in logs if int(r.get('command',0))==cmd]
 batches={r['batch'] for r in rows if r['stage']=='commit'}
 owners={r['player'] for r in rows if r['stage']=='input'}
 rows += [r for r in logs if r['stage'] in ('send','client-apply','render-submit') and r['batch'] in batches and r['player'] in owners]
 first=next((float(r['clock']) for r in rows if r.get('stage')=='input'),None)
 timing={}
 for stage in sorted(set(r['stage'] for r in rows)):
  xs=[r for r in rows if r['stage']==stage]
  times=[(float(r['clock'])-first)*1000 for r in xs] if first else []
  timing[stage]={'first_ms':round(min(times),3) if times else None,'last_ms':round(max(times),3) if times else None,'events':len(xs),'members':sum(int(r.get('members',0)) for r in xs)}
 slots={};pitches=set();changed=[];last=None;counts=set();assigned_at=None
 for sample in samples:
  seen=[s for s in sample['authority']['soldiers'] if s['id'] in ids and s['command']==cmd]
  if not seen:continue
  last=sample
  for s in seen:
   if s['slot_pitch']>0:pitches.add(s['slot_pitch']);counts.add(s['slot_members'])
   if s['has_dock']:
    p=tuple(s['movement_target']);prior=slots.get(s['id'])
    if prior is not None and math.dist(prior,p)>.01:changed.append(s['id'])
    slots[s['id']]=p
  if assigned_at is None and len(slots)==len(ids):assigned_at=sample['t']-event['t']
 points=list(slots.values())
 gap=min((math.dist(p[:2],q[:2]) for i,p in enumerate(points) for q in points[i+1:]),default=None)
 final=[s for s in last['authority']['soldiers'] if s['id'] in ids and s['command']==cmd] if last else []
 within=sum(s['id'] in slots and math.dist(s['position'][:2],slots[s['id']][:2])<=20.1 for s in final)
 out['commands'].append({'command':cmd,'selected':len(ids),'types':event['selected_types'],'timing':timing,'assigned':len(slots),'unique_slots':len(set(points)),'pitch_cm':sorted(pitches),'command_member_counts':sorted(counts),'minimum_target_gap_cm':gap,'assignment_changes':sorted(set(changed)),'all_slots_observed_after_seconds':assigned_at,'last_observed_t':last['t'] if last else None,'members_within_20cm_at_last_observation':within,'still_moving_at_last_observation':sum(bool(s['order']) for s in final),'remaining_distances':{str(s['id']):round(math.dist(s['position'][:2],slots[s['id']][:2]),2) for s in final if s['id'] in slots and math.dist(s['position'][:2],slots[s['id']][:2])>20.1}})
out['sample_count']=len(samples)
out['maximums']={k:max(s['authority'].get(k,0) for s in samples) for k in ['positions','paths','commit_members','planning_ms','commit_ms','shared_cache_bytes','recovery_pending','reservations']}
out['maximums'].update({k:max(s['client'].get(k,0) for s in samples) for k in ['invalid_lines','endpoint_mismatches','line_queue','dirty','pending_batches']})
out['final_client']={k:samples[-1]['client'].get(k) for k in ['lines','line_queue','invalid_lines','endpoint_mismatches','selected','ready']}
out['last_t']=samples[-1]['t']
(root/'runtime-slots-summary.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in out.items() if k not in ('commands','final_client')},ensure_ascii=False))
for c in out['commands']:
 print(json.dumps({k:v for k,v in c.items() if k not in ('timing','remaining_distances')},ensure_ascii=False))
 print({k:v for k,v in c['timing'].items() if k in ('receive','commit','direction','displacement','line-render','render-submit','apply')})
