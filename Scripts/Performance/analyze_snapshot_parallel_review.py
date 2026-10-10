"""Summarize paired CPU captures and passive per-connection network time series."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Artifacts/PerformanceOptimization20261010'


def median(values):
    return statistics.median(values) if values else None


def summarize_run(path):
    r = json.loads(path.read_text(encoding='utf-8'))
    profiles = json.loads((path.parent / 'native-capture.json').read_text(encoding='utf-8'))
    counters = {}
    for world in profiles:
        for key, value in world['counters'].items():
            key = key.rsplit('/', 1)[-1]
            c = counters.setdefault(key, {'total': 0, 'calls': 0, 'per_engine_frame': 0})
            c['total'] += value['total']
            c['calls'] += value['calls']
            c['per_engine_frame'] += value['mean_per_engine_frame']
    network = []
    for world in profiles:
        samples = world['network_samples']
        if len(samples) < 2:
            continue
        first, last = samples[0], samples[-1]
        duration = last['wall_seconds'] - first['wall_seconds']
        nw = {'world': world['world'], 'net_mode': first['net_mode'], 'seconds': duration,
              'samples': len(samples), 'connections': []}
        if first.get('visuals') and last.get('visuals'):
            gauges = {'muzzle_active', 'flight_data_active'}
            nw['visual_totals'] = {k:last['visuals'][k]-v for k,v in first['visuals'].items()
                                   if k not in gauges and k in last['visuals']}
            nw['visual_active_end'] = {k:last['visuals'][k] for k in gauges if k in last['visuals']}
        for name in ['created', 'ended', 'received_bytes', 'received_events', 'received_bootstrap', 'received_stale_epoch']:
            nw[name + '_per_second'] = (last.get('flights', {}).get(name, 0) - first.get('flights', {}).get(name, 0)) / duration
        f0, f1 = first.get('flights', {}), last.get('flights', {})
        nw['bootstrap_received_before_capture'] = f0.get('received_bootstrap',0)
        for name in ['produced_record_bytes','received_record_bytes','received_batches','byte_accounting_failures']:
            if name in f0 and name in f1:
                nw[name + '_per_second'] = (f1[name]-f0[name])/duration
        ages = f1.get('age_samples', 0) - f0.get('age_samples', 0)
        if ages:
            nw['received_age_mean_ms'] = (f1['age_sum_ms'] - f0['age_sum_ms']) / ages
            bounds = [0,16,33,50,100,200,500,1000,2000,5000,10000,'>10000']
            histogram = [b-a for a,b in zip(f0['age_histogram'], f1['age_histogram'])]
            cumulative = 0
            for i,n in enumerate(histogram):
                cumulative += n
                if cumulative >= ages*.95:
                    nw['received_age_p95_upper_ms'] = bounds[i]
                    break
        offsets = [s['pie_clock_estimate_error_ms'] for s in samples if 'pie_clock_estimate_error_ms' in s]
        if offsets:
            nw['clock_error_median_ms'] = median(offsets)
            nw['clock_error_max_abs_ms'] = max(abs(x) for x in offsets)
        for c0 in first['connections']:
            name = c0['connection']
            series = [(s, c) for s in samples for c in s['connections'] if c['connection'] == name]
            if len(series) < 2:
                continue
            c1 = series[-1][1]
            conn = {'connection': name, 'player_guid': c0.get('player_guid'),
                    'budget_Bps': c0['budget_bytes_per_second'],
                    'budget_min_Bps': min(c['budget_bytes_per_second'] for s,c in series),
                    'budget_max_Bps': max(c['budget_bytes_per_second'] for s,c in series),
                    'observed_samples': len(series)}
            valid = [c for s,c in series if c['rates_valid']]
            weights = sum(c['interval_seconds'] for c in valid)
            for field in ['receive_bytes_per_second','send_bytes_per_second','receive_packets_per_second',
                          'send_packets_per_second','receive_lost_per_second','send_lost_per_second']:
                conn[field] = sum(c[field] * c['interval_seconds'] for c in valid) / weights if weights else None
            conn['unacked_bunches_max'] = max(c['unacked_reliable_bunches'] for s,c in series)
            conn['bandwidth_debt_bits_max'] = max(c['bandwidth_debt_bits'] for s,c in series)
            conn['rtt_median_ms'] = median([c['rtt_ms'] for s,c in series if 'rtt_ms' in c])
            p0,p1 = c0.get('flight_peer'),c1.get('flight_peer')
            if p0 and p1 and p0['channel_serial'] == p1['channel_serial']:
                peer = {key+'_per_second': (p1[key]-p0[key])/duration for key in
                        ['enqueued_events','bootstrap_events','sent_events','sent_bytes','sent_batches',
                         'flush_calls','budget_deferrals','batch_limit_hits']}
                peer['bootstrap_enqueued_before_capture'] = p0['bootstrap_events']
                for key in ['enqueued_record_bytes','bootstrap_record_bytes','sent_record_bytes']:
                    if key in p0 and key in p1:
                        peer[key+'_per_second'] = (p1[key]-p0[key])/duration
                peer.update(queue_start=p0['queued_events'],queue_end=p1['queued_events'],
                            queue_growth_per_second=(p1['queued_events']-p0['queued_events'])/duration,
                            head_age_start_ms=p0['head_event_age_ms'],head_age_end_ms=p1['head_event_age_ms'])
                peer['conservation_error'] = p1['queued_events']-p0['queued_events'] - (
                    p1['enqueued_events']-p0['enqueued_events'] + p1['bootstrap_events']-p0['bootstrap_events'] - p1['sent_events']+p0['sent_events'])
                peer['mean_records_per_batch'] = (p1['sent_events']-p0['sent_events']) / max(1,p1['sent_batches']-p0['sent_batches'])
                peer['mean_batches_per_flush'] = (p1['sent_batches']-p0['sent_batches']) / max(1,p1['flush_calls']-p0['flush_calls'])
                peer['batch_cap_fraction'] = (p1['batch_limit_hits']-p0['batch_limit_hits']) / max(1,p1['flush_calls']-p0['flush_calls'])
                peer['budget_blocked_fraction'] = (p1['budget_deferrals']-p0['budget_deferrals']) / max(1,p1['flush_calls']-p0['flush_calls'])
                peer['head_age_max_ms'] = max(c['flight_peer']['head_event_age_ms'] for s,c in series)
                peer['absolute_event_cap_per_second'] = peer['flush_calls_per_second'] * 8 * 16
                peer['absolute_payload_cap_Bps'] = peer['flush_calls_per_second'] * 8 * 1000
                peer['observed_size_event_cap_per_second'] = peer['flush_calls_per_second'] * 8 * peer['mean_records_per_batch']
                conn['flight_peer'] = peer
            u0,u1 = c0.get('unit_stream'),c1.get('unit_stream')
            if u0 and u1 and (u0['generation'],u0['receive_generation']) == (u1['generation'],u1['receive_generation']):
                conn['unit_stream'] = {key+'_per_second': (u1[key]-u0[key])/duration for key in
                                       ['state_bytes','pose_bytes','received_state_bytes','received_pose_bytes',
                                        'budget_deferrals','window_deferrals','pose_merges']}
                conn['unit_stream']['oldest_state_wait_end_ms'] = u1['oldest_state_wait_ms']
                conn['unit_stream']['oldest_pose_wait_end_ms'] = u1['oldest_pose_wait_ms']
            nw['connections'].append(conn)
        network.append(nw)
    normalized = {}
    for label,time_key,count_key in [
            ('schedule_ms_per_1000_conditions','StateTree.ScheduleMs','StateTree.Conditions'),
            ('ui_paint_ms_per_1000_commands','SceneUI.PaintMs','SceneUI.Commands'),
            ('avoidance_ms_per_1000_solves','Avoidance.TotalMs','Avoidance.Solves')]:
        count = counters.get(count_key,{}).get('total',0)
        if count:normalized[label] = counters.get(time_key,{}).get('total',0)*1000/count
    visibility = {}
    count = r['setup']['population']['requested']
    for world in profiles:
        values = [v['mean_per_engine_frame'] for k,v in world['counters'].items()
                  if k.rsplit('/',1)[-1] == 'Units.Visible']
        if values:visibility[world['world']] = sum(values)/count
    return {'name':path.parent.name,'result':r,'counters':counters,'network':network,
            'visible_by_world':visibility,'load_normalized':normalized}


def target(run, candidate):
    key = {'group':'StateTree.ScheduleMs','snapshot':'StateTree.ScheduleMs',
           'ui':'SceneUI.PaintMs','ui_balanced':'SceneUI.PaintMs','avoidance':'Avoidance.TotalMs'}.get(candidate)
    return run['counters'].get(key,{}).get('per_engine_frame',0) if key else run['result']['csv']['GameThreadTime']['mean']


def analyze():
    runs = [summarize_run(p) for p in sorted((OUT/'Paired').glob('*/result.json'))]
    pairs, decisions = [], {}
    for scene in ['dense200','stress']:
        for candidate in ['group','snapshot','ui','ui_balanced','avoidance','combined','selected']:
            phase=candidate if candidate in ['combined','selected'] else 'single'
            selected=[r for r in runs if r['name'].startswith(phase+'-') and
                      r['result']['scene']==scene and r['result']['candidate']==candidate]
            # New telemetry and weighted geometry use a fresh same-build A/A series.
            aa_prefix = 'aa_final-' if any(r['result'].get('build_manifest_sha256') for r in selected) else 'aa-'
            aa = [r for r in runs if r['name'].startswith(aa_prefix) and r['result']['scene']==scene]
            aa_pairs=[]
            for number in [1,2,3]:
                group=[r for r in aa if r['result']['round']==number]
                if len(group)==2:aa_pairs.append(group)
            paired=[]
            for number in [1,2,3]:
                group={r['result']['variant']:r for r in selected if r['result']['round']==number}
                if 'baseline' not in group or candidate not in group:continue
                a,b=group['baseline'],group[candidate]
                v={'scene':scene,'candidate':candidate,'round':number,
                   'baseline_target_ms':target(a,candidate),'candidate_target_ms':target(b,candidate),
                   'target_saved_ms':target(a,candidate)-target(b,candidate),
                   'baseline_frame_ms':a['result']['csv']['FrameTime']['mean'],
                   'candidate_frame_ms':b['result']['csv']['FrameTime']['mean'],
                   'baseline_frame_p95_ms':a['result']['csv']['FrameTime']['p95'],
                   'candidate_frame_p95_ms':b['result']['csv']['FrameTime']['p95'],
                   'baseline_gt_p95_ms':a['result']['csv']['GameThreadTime']['p95'],
                   'candidate_gt_p95_ms':b['result']['csv']['GameThreadTime']['p95'],
                   'capture_errors':len(a['result'].get('capture_errors',[]))+len(b['result'].get('capture_errors',[])),
                   'baseline_load_normalized':a['load_normalized'],
                   'candidate_load_normalized':b['load_normalized'],
                   'paths':[a['name'],b['name']]}
                paired.append(v);pairs.append(v)
            noise = median([abs(target(a,candidate)-target(b,candidate)) for a,b in aa_pairs])
            p95_noise = max([abs(a['result']['csv']['GameThreadTime']['p95']-b['result']['csv']['GameThreadTime']['p95']) for a,b in aa_pairs],default=0)
            frame_p95_noise = max([abs(a['result']['csv']['FrameTime']['p95']-b['result']['csv']['FrameTime']['p95']) for a,b in aa_pairs],default=0)
            decision={'pairs':len(paired),'aa_pairs':len(aa_pairs),'target_noise_ms':noise,'gt_p95_noise_ms':p95_noise,
                      'aa_series':aa_prefix,
                      'frame_p95_noise_ms':frame_p95_noise,
                      'target_saved_median_ms':median([p['target_saved_ms'] for p in paired]),'accepted':False}
            decision['all_pairs_target_improved'] = len(paired)==3 and all(p['target_saved_ms']>0 for p in paired)
            if len(paired)==3 and len(aa_pairs)==3:
                decision['accepted']=all(p['target_saved_ms']>=-noise and p['capture_errors']==0 for p in paired) and \
                    decision['target_saved_median_ms']>noise and \
                    all(p['candidate_gt_p95_ms']-p['baseline_gt_p95_ms']<=p95_noise and
                        p['candidate_frame_p95_ms']-p['baseline_frame_p95_ms']<=frame_p95_noise for p in paired)
            decisions[f'{scene}/{candidate}']=decision
    view_pairs=[]
    for scene in ['dense200','stress']:
        for view in ['full','mixed','offscreen','asymmetric']:
            for candidate in ['ui_balanced','combined','selected','snapshot']:
                selected=[r for r in runs if r['name'].startswith('views-') and
                          r['result']['scene']==scene and r['result']['view']==view and
                          r['result']['candidate']==candidate and r['result'].get('view_eligible',True)]
                for number in [1,2,3]:
                    group={r['result']['variant']:r for r in selected if r['result']['round']==number}
                    if 'baseline' not in group or candidate not in group:continue
                    a,b=group['baseline'],group[candidate]
                    view_pairs.append({'scene':scene,'view':view,'candidate':candidate,'round':number,
                        'target_saved_ms':target(a,candidate)-target(b,candidate),
                        'baseline_visible_fractions':a['result']['visible_fractions'],
                        'candidate_visible_fractions':b['result']['visible_fractions'],
                        'baseline_visible_by_world':a['visible_by_world'],
                        'candidate_visible_by_world':b['visible_by_world'],
                        'frame_p95_delta_ms':b['result']['csv']['FrameTime']['p95']-a['result']['csv']['FrameTime']['p95'],
                        'gt_p95_delta_ms':b['result']['csv']['GameThreadTime']['p95']-a['result']['csv']['GameThreadTime']['p95'],
                        'capture_errors':len(a['result'].get('capture_errors',[]))+len(b['result'].get('capture_errors',[])),
                        'paths':[a['name'],b['name']]})
    output={'runs':runs,'pairs':pairs,'decisions':decisions,'view_pairs':view_pairs,
            'screening_rule':'Three paired median target savings must exceed median absolute A/A target fluctuation; no single target regression larger than that fluctuation; every GT and frame P95 regression within maximum A/A P95 fluctuation; zero capture errors. All-pairs-positive is reported separately. Final adoption additionally requires functional checks, view checks and load review.',
            'caution':'Smoke enables expensive shadow verification and is not a performance comparison. Application bytes are subsets of UE connection bytes. Age p95 is a histogram upper bound.'}
    (OUT/'analysis.json').write_text(json.dumps(output,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'runs':len(runs),'decisions':decisions},ensure_ascii=False,indent=2))
    return output


if __name__=='__main__':
    analyze()
