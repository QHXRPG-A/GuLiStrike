"""Summarize existing capture files; never connects to or enumerates a live World."""
from __future__ import annotations
import csv
import json
from pathlib import Path
import statistics

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Artifacts/MassAvoidance20261011'

def mean(values):return statistics.fmean(values) if values else None
def counter(world,name,field='mean_per_engine_frame'):
    return sum(v[field] for k,v in world['counters'].items() if k.endswith(name))
def delta_rate(rows,key,seconds=None):
    if len(rows)<2:return None
    dt=seconds if seconds is not None else rows[-1]['wall_seconds']-rows[0]['wall_seconds']
    return (rows[-1].get(key,0)-rows[0].get(key,0))/dt if dt>0 else None

def network(world):
    samples=world['network_samples']
    result={'world':world['world'],'net_mode':samples[0]['net_mode'] if samples else None,'samples':len(samples),'connections':[]}
    if not samples:return result
    dt=samples[-1]['wall_seconds']-samples[0]['wall_seconds']
    sim=samples[-1]['world_seconds']-samples[0]['world_seconds']
    result.update(wall_span_seconds=dt,world_span_seconds=sim)
    f0,f1=samples[0]['flights'],samples[-1]['flights']
    for key in ['created','ended','produced_record_bytes','received_events','received_record_bytes']:
        result[key+'_per_wall_second']=(f1[key]-f0[key])/dt if dt>0 else None
        result[key+'_per_world_second']=(f1[key]-f0[key])/sim if sim>0 else None
    if f1['age_samples']>f0['age_samples']:
        result['received_age_mean_ms']=(f1['age_sum_ms']-f0['age_sum_ms'])/(f1['age_samples']-f0['age_samples'])
    errors=[s['pie_clock_estimate_error_ms'] for s in samples if 'pie_clock_estimate_error_ms' in s]
    result['clock_estimate_error_mean_ms']=mean(errors)
    keys=set(c['connection'] for s in samples for c in s['connections'])
    for key in sorted(keys):
        rows=[dict(c,wall_seconds=s['wall_seconds']) for s in samples for c in s['connections'] if c['connection']==key]
        valid=[c for c in rows if c['rates_valid']]
        total=sum(c['interval_seconds'] for c in valid)
        r={'connection':key,'budget_bytes_per_second':rows[-1]['budget_bytes_per_second']}
        for field in ['receive_bytes_per_second','send_bytes_per_second','receive_packets_per_second','send_packets_per_second','receive_lost_per_second','send_lost_per_second','rtt_ms']:
            r[field]=sum(c[field]*c['interval_seconds'] for c in valid)/total if total else None
        for field in ['bandwidth_debt_bits','unacked_reliable_bunches','send_buffer_bits']:
            r[field+'_max']=max(c[field] for c in rows)
        r['budget_fraction_mean']=r['send_bytes_per_second']/r['budget_bytes_per_second'] if r['send_bytes_per_second'] is not None else None
        if 'flight_peer' in rows[0]:
            before,after=rows[0]['flight_peer'],rows[-1]['flight_peer']
            span=rows[-1]['wall_seconds']-rows[0]['wall_seconds']
            r['flight_queue_start']=before['queued_events'];r['flight_queue_end']=after['queued_events']
            r['flight_queue_growth_per_second']=(after['queued_events']-before['queued_events'])/span
            r['flight_head_age_end_ms']=after['head_event_age_ms']
            for field in ['enqueued_events','sent_events','sent_bytes','sent_record_bytes','sent_batches','flush_calls','budget_deferrals','batch_limit_hits']:
                r['flight_'+field+'_per_second']=(after[field]-before[field])/span
            r['flight_batches_per_flush']=(after['sent_batches']-before['sent_batches'])/max(1,after['flush_calls']-before['flush_calls'])
        before,after=rows[0]['unit_stream'],rows[-1]['unit_stream']
        for field in ['state_bytes','pose_bytes','budget_deferrals','window_deferrals','pose_merges','received_state_bytes','received_pose_bytes']:
            r['unit_'+field+'_per_second']=(after[field]-before[field])/dt if dt>0 else None
        for field in ['pending_states','pending_poses','inflight_batches','oldest_state_wait_ms','oldest_pose_wait_ms']:
            r['unit_'+field+'_end']=after[field]
        result['connections'].append(r)
    return result

def read_window(path):
    r=json.loads(path.read_text(encoding='utf-8'))
    worlds=json.loads((path.parent/'native-capture.json').read_text(encoding='utf-8'))
    server=next(w for w in worlds if '/UEDPIE_0_' in w['world'])
    q=counter(server,'Avoidance.ActualQueries','total')
    # Pre-helper smoke files are invalid and excluded from the formal matrix.
    if not q:q=counter(server,'Avoidance.Solves','total')
    metrics={k:counter(server,'Avoidance.'+k) for k in ['TotalMs','QueryMs','SolveMs','GridMs','SoftMs','SharedGeometryMs','CachePrepareMs']}
    work={k:counter(server,'Avoidance.'+k,'total') for k in ['ActualQueries','BucketVisits','ExactCandidates','Consumed','TrendEvaluations','QueryTrendEvaluations','CacheHits','CacheInvalidations','CacheExpirations','CacheGuardVisits','SharedIndexBuilds','SharedIndexAccesses','SharedIndexReuses','SoftCandidatePairs','SoftOverlapPairs']}
    metrics['CombinedAvoidanceMs']=metrics['TotalMs']+metrics['SoftMs']
    metrics['query_us_per_query']=1000*counter(server,'Avoidance.QueryMs','total')/q if q else None
    work['visited_per_query']=work['BucketVisits']/q if q else None
    work['trends_per_query']=work['TrendEvaluations']/q if q else None
    work['consumed_per_query']=work['Consumed']/q if q else None
    before_worlds=json.loads((path.parent/'counters-before.json').read_text(encoding='utf-8'))['worlds']
    after_worlds=json.loads((path.parent/'counters-after.json').read_text(encoding='utf-8'))['worlds']
    before=next(w['native_population'] for w in before_worlds if 'native_population' in w)
    after=next(w['native_population'] for w in after_worlds if 'native_population' in w)
    client_drops=[]
    for b in before_worlds:
        if 'effects' not in b:continue
        a=next(w for w in after_worlds if w['world']==b['world'])
        client_drops.append({'world':b['world'],'dropped_shots':a['effects']['dropped_shots']-b['effects']['dropped_shots']})
    return {'run':path.parent.name,'scene':r['scene'],'candidate':r['candidate'],'variant':r['variant'],'round':r['round'],
            'csv':r['csv'],'metrics':metrics,'work':work,'network':[network(w) for w in worlds],
            'moving_before':before['moving'],'moving_after':after['moving'],'moved':r['moving_units_displaced'],
            'alive_before':r['alive_before'],'alive_after':r['alive_after'],'feedback':r['feedback'],'client_shot_drops':client_drops,
            'capture_errors':len(r['capture_errors']),'setup_errors':len(r['runtime_errors'])-len(r['capture_errors']),
            'visible_fractions':r['visible_fractions'],'build':r['build_manifest_sha256'],
            'presentation_eligible':r['presentation_eligible'],'directory':str(path.parent.relative_to(ROOT))}

def main():
    rows=[read_window(p) for p in sorted((OUT/'Paired').glob('*/result.json')) if not p.parent.name.startswith('smoke-')]
    groups=[]
    for scene in ['dense200','stress']:
        aa=[r for r in rows if r['scene']==scene and r['candidate']=='same-version']
        if len(aa)!=2:continue
        noise=abs(aa[1]['csv']['FrameTime']['p95']-aa[0]['csv']['FrameTime']['p95'])
        cpu_noise=abs(aa[1]['metrics']['CombinedAvoidanceMs']-aa[0]['metrics']['CombinedAvoidanceMs'])
        for candidate in ['avoidance','turn','combined']:
            subset=[r for r in rows if r['scene']==scene and r['candidate']==candidate]
            pairs=[]
            for n in [1,2,3]:
                old=next((r for r in subset if r['round']==n and r['variant']=='baseline'),None)
                new=next((r for r in subset if r['round']==n and r['variant']==candidate),None)
                if not old or not new:continue
                pairs.append({'round':n,'baseline':old['run'],'optimized':new['run'],
                              'cpu_saved_ms':old['metrics']['CombinedAvoidanceMs']-new['metrics']['CombinedAvoidanceMs'],
                              'p95_delta_ms':new['csv']['FrameTime']['p95']-old['csv']['FrameTime']['p95'],
                              'p95_pass':new['csv']['FrameTime']['p95']-old['csv']['FrameTime']['p95']<=noise+1.e-6,
                              'capture_errors':old['capture_errors']+new['capture_errors']})
            groups.append({'scene':scene,'candidate':candidate,'aa_frame_p95_noise_ms':noise,'aa_avoidance_noise_ms':cpu_noise,
                           'pairs':pairs,'complete':len(pairs)==3,
                           'cpu_savings_mean_ms':mean([p['cpu_saved_ms'] for p in pairs]),
                           'p95_pass':len(pairs)==3 and all(p['p95_pass'] for p in pairs),
                           'cpu_pass':len(pairs)==3 and (candidate=='turn' or mean([p['cpu_saved_ms'] for p in pairs])>cpu_noise)})
    report={'scope':'Same-process dedicated server and two 1280x720 clients; no independent client FPS inference',
            'bytes_per_kb':1000,'network_basis':'Per-connection UE rates; application payload reported separately; endpoints never added together',
            'windows':rows,'groups':groups}
    (OUT/'analysis.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'windows':len(rows),'groups':groups},ensure_ascii=True,indent=2))

if __name__=='__main__':main()
