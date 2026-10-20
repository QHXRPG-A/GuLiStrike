"""Report native whole-soak queues separately from sparse complete-GT traces."""
from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path

from analyze_hotspot_capture import workload_activity, HYBRID_SCOPE_VERSION, CORE_QUEUE_KEYS
from hybrid_workload import validate_minimum, combat_inputs


def occupancy(samples, key, field='async_transactions'):
    start=samples[0]['wall_seconds'];buckets=collections.defaultdict(list)
    points=[(s['wall_seconds']-start,s[field][key]) for s in samples]
    for seconds,value in points:buckets[int(seconds/60)].append(value)
    minutes=[{'minute':minute,'mean':sum(values)/len(values),'peak':max(values)}
             for minute,values in sorted(buckets.items()) if len(values)>=40]
    run=longest=0
    for a,b in zip(minutes,minutes[1:]):
        run=run+1 if b['mean']>a['mean'] else 0;longest=max(longest,run)
    tail=[value for seconds,value in points if seconds>=points[-1][0]-300]
    head=[value for seconds,value in points if seconds<=300]
    xm=sum(t for t,_ in points)/len(points);ym=sum(v for _,v in points)/len(points)
    xx=sum((t-xm)**2 for t,_ in points);yy=sum((v-ym)**2 for _,v in points)
    xy=sum((t-xm)*(v-ym) for t,v in points)
    growth_assessable=len(minutes)>=6
    return {'first':points[0][1],'last':points[-1][1],'peak':max(v for _,v in points),
            'first_five_minute_mean':sum(head)/len(head),'last_five_minute_mean':sum(tail)/len(tail),
            'linear_slope_per_second':xy/xx if xx else 0,
            'linear_r_squared':xy*xy/(xx*yy) if xx and yy else 0,
            'minute_bins':minutes,'longest_increasing_minute_run':longest,
            'five_minute_growth_assessable':growth_assessable,
            'five_minute_continuous_growth_observed':longest>=5 if growth_assessable else None}


def observation_report(data, full_load):
    samples=data['network_samples']
    if len(samples)<2:raise ValueError('Native observation has too few samples')
    duration=samples[-1]['wall_seconds']-samples[0]['wall_seconds']
    if not data['complete'] or data['dropped_samples'] or data['sampled_seconds']<data['requested_seconds']:
        raise ValueError('Native observation incomplete or overflowed')
    for row in samples:
        if not row['simulation_census']['ready']:raise ValueError('World lost readiness')
        if full_load:validate_minimum(row['simulation_census'])
    gaps=[b['wall_seconds']-a['wall_seconds'] for a,b in zip(samples,samples[1:])]
    activity=workload_activity(data)
    activity.pop('simulation_census_samples',None);activity.pop('task_activity_samples',None)
    queues={key:occupancy(samples,key) for key in ('input_bytes','output_admitted_bytes','result_reserved_bytes',
        'control_bytes','queued_input_bytes','gameplay_admitted_bytes','oldest_input_ms','oldest_control_ms')}
    core_coverage=all(key in row['async_transactions'] for row in samples for key in CORE_QUEUE_KEYS)
    if core_coverage: queues.update({key:occupancy(samples,key) for key in CORE_QUEUE_KEYS})
    first=samples[0]['async_transactions'];last=samples[-1]['async_transactions']
    rates={key:(last[key]-first[key])/duration for key in ('logical_steps','simulation_ms','inputs','applications')}
    growth_assessable=all(q['five_minute_growth_assessable'] for q in queues.values())
    return {'samples':len(samples),'seconds':duration,'max_sampling_gap_seconds':max(gaps),
            'full_local_load_verified':full_load,'activity':activity,'queues':queues,'rates_per_second':rates,
            'core_queue_coverage_complete':core_coverage,
            'native_simulation_cost_scope':'simulation_ms is ordered logical batches only; use complete-GT trace samples for the 5 ms target.',
            'rejections_delta':last['input_rejected']-first['input_rejected'],
            'result_backpressure_delta':last['result_backpressure']-first['result_backpressure'],
            'continuous_growth_assessable':growth_assessable,
            'continuous_growth_observed':any(q['five_minute_continuous_growth_observed'] for q in queues.values())
                if growth_assessable else None}


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('root',type=Path);args=parser.parse_args()
    root=args.root.resolve()
    read=lambda path:json.loads(path.read_text(encoding='utf-8-sig'))
    pair=read(root/'long-paired.json')
    result={'paired_preparation_complete':pair['complete'],'variants':{},
            'scope':'45 full-load minutes plus aged combat. Complete Hybrid frame/step/second costs come from retained trace samples; normal combat casualties are not a 5 ms target pass.',
            'queue_diagnostic':'A flagged run is at least five successive increases in complete one-minute occupancy bins. Fewer than six usable bins is unassessable (null), including the 60-second aged combat phase. Raw 1 Hz samples, minute bins and head/tail means remain available for review.'}
    for variant in ('A','B'):
        directory=root/variant;summary=read(directory/'long-summary.json')
        if not summary['complete'] or summary['elapsed_seconds']<2700 or not summary['aged_combat']['complete']:
            raise ValueError('The required 45-minute load and aged combat were not completed')
        row={'full_load':observation_report(read(directory/'long-observation.json'),True),
             'aged_combat':observation_report(read(directory/'aged-combat-observation.json'),False),
             'trace_samples':[],'aged_combat_trace_samples':[]}
        samples=[('trace_samples',s) for s in summary['samples'] if s['trace']]
        samples += [('aged_combat_trace_samples',s) for s in summary['aged_combat']['samples']]
        for category,sample in samples:
            path=directory/sample['directory']/'analysis'/'summary.json'
            if not path.is_file():
                row[category].append({'directory':sample['directory'],'age_seconds':sample['age_seconds'],'analyzed':False});continue
            trace=read(path)
            if trace['hybrid_target'].get('scope_version')!=HYBRID_SCOPE_VERSION:
                raise ValueError('Reanalyze the retained trace including the predictive avoidance processor root')
            row[category].append({'directory':sample['directory'],'age_seconds':sample['age_seconds'],'analyzed':True,
                'game_thread':trace['game_thread'],'hybrid_target':trace['hybrid_target']})
        result['variants'][variant]=row
    a=read(root/'A'/'aged-combat-preparation.json');b=read(root/'B'/'aged-combat-preparation.json')
    result['aged_combat_inputs_equal']=combat_inputs(a)==combat_inputs(b)
    result['all_full_load_trace_samples_analyzed']=all(len(v['trace_samples'])==3 and all(s['analyzed'] for s in v['trace_samples'])
        for v in result['variants'].values())
    result['all_aged_combat_trace_samples_analyzed']=all(len(v['aged_combat_trace_samples'])==3
        and all(s['analyzed'] for s in v['aged_combat_trace_samples']) for v in result['variants'].values())
    result['candidate_five_ms_at_sampled_ages']=result['all_full_load_trace_samples_analyzed'] and all(
        s['hybrid_target']['met'] for s in result['variants']['B']['trace_samples'])
    path=root/'summary-hybrid-soak.json';path.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'output':str(path),'paired':result['paired_preparation_complete'],
        'aged_combat_inputs_equal':result['aged_combat_inputs_equal'],
        'candidate_five_ms_at_sampled_ages':result['candidate_five_ms_at_sampled_ages']}))


if __name__=='__main__':main()
