"""Analyze native hotspot windows after capture; complete GT frames and exclusive families.

Uses the project's existing Insights exporter and QPC mapping. Never run alongside
measurement. Full event CSV is reproducible from the retained trace and is removed
only with --discard-events, after its hash and frame summary have been written.
"""
from __future__ import annotations
import argparse, collections, csv, hashlib, json, math
from concurrent.futures import ProcessPoolExecutor
from itertools import repeat
from pathlib import Path
import export_review_traces as export
from export_commander_pose_traces import trace_clock


def distribution(values):
    v=sorted(values)
    if not v:return {'n':0,'mean_ms':0,'p95_ms':0,'peak_ms':0}
    return {'n':len(v),'mean_ms':sum(v)/len(v),'p95_ms':v[math.ceil(len(v)*.95)-1],'peak_ms':v[-1]}


def analyze(root, insights, discard=False):
    root=root.resolve();long_window=(root/'profile.json').is_file()
    capture=json.loads(((root.parent if long_window else root)/'capture.json').read_text())
    if not capture.get('valid'):raise ValueError('Refusing an invalid capture: '+str(root))
    profile=json.loads((root/('profile.json' if long_window else 'profile-after.json')).read_text())
    assert profile['capture_valid'], 'Native counter overflow'
    clock=trace_clock(root/'session.utrace');frequency=profile['qpc_frequency']
    if long_window:
        # Generic native capture has its own begin/end samples. The completed
        # initial 600-unit hotspot timestamps must not be reused for the soak.
        start_qpc=profile['network_samples'][0]['qpc'];end_qpc=profile['network_samples'][-1]['qpc']
    else:
        work=profile['hotspot_workload'];start_qpc=work['start_qpc'];end_qpc=work['end_qpc']
    begin=start_qpc/frequency-clock['qpc_origin_seconds']
    end=end_qpc/frequency-clock['qpc_origin_seconds']
    out=root/'analysis';out.mkdir(exist_ok=True)
    export.INSIGHTS=insights
    events=out/'game-thread-events.csv';statistics=out/'all-thread-timers.csv';worker_events=out/'worker-calculations.csv'
    commands=[]
    if not events.exists():commands.append(f'TimingInsights.ExportTimingEvents "{events.as_posix()}" -threads=GameThread -startTime={begin:.9f} -endTime={end:.9f} -columns=ThreadId,TimerId,TimerName,StartTime,EndTime,Duration,Depth')
    if not statistics.exists():commands.append(f'TimingInsights.ExportTimerStatistics "{statistics.as_posix()}" -startTime={begin:.9f} -endTime={end:.9f} -sortBy=TotalInclusiveTime')
    if not worker_events.exists():
        names='GuLiCommander_WorkerMovement,GuLiCommander_WorkerPredictiveAvoidance,GuLiHybrid_WorkerControlPointCounts,GuLiHybrid_WorkerMassPoseEncode,GuLiHybrid_WorkerPoseSendConfirmation,GuLiHybrid_WorkerGameplayCompute,GuLiHybrid_WorkerReadDelta'
        commands.append(f'TimingInsights.ExportTimingEvents "{worker_events.as_posix()}" -threads=GuLiTransactions,GuLiGameplay-0,GuLiGameplay-1,GuLiGameplay-2,GuLiGameplay-3 -timers={names} -startTime={begin:.9f} -endTime={end:.9f} -columns=ThreadId,TimerName,StartTime,EndTime,Duration,Depth')
    if commands:export.analyze(root,commands,out)
    frame_ms=[];frames=[];stack=[];index=-1;active=False
    categories=['simulation','HUD','Slate_other','render_sync_wait',
                'main_apply_excluding_simulation_HUD','old_synchronous_business_excluding_HUD',
                'other_Hybrid','presentation_other','engine_and_other']
    ranks={}
    def rank(name):
        if name in ranks:return ranks[name]
        if name in ('GuLiHybrid_AdvanceSimulation','GuLiHybrid_MainSimulation','GuLiHybrid_MainSharedSimulation',
                    'GuLiCommander_MovementApply','GuLiCommander_SoldierCombat','GuLiCommander_ManualAvoidanceRefresh',
                    'GuLiHybrid_ControlPointSnapshot') or name.startswith('GuLiCommander_Predictive'):value=0
        elif name in ('WBP_CommanderHUD_C','HandleResourcePrivateStateChanged') or name.startswith(('GuLiCommanderHUD_','UGuLiCommanderHUDWidget')):value=1
        elif name.startswith(('Slate','FSlate','SWidget')) or name in ('DrawWindows','DrawWindow_RenderThread'):value=2
        elif name in ('GameThreadWaitForTask','FFrameEndSync::Sync'):value=3
        elif name=='GuLiHybrid_MainApplyBatch':value=4
        elif name in ('GuLiHybrid_PollBusinessWork','GuLiHybrid_AdvanceBusiness'):value=5
        elif name.startswith('GuLiHybrid_'):value=6
        elif name.startswith(('GuLiCommanderPresentation','GuLiSceneUI_','GuLiPose_','GuLiImpact_','GuLiMuzzle_','GuLiFlight_')):value=7
        else:value=8
        ranks[name]=value
        return value
    timers=collections.defaultdict(lambda:[0.,0.,0]);families=collections.defaultdict(lambda:collections.defaultdict(float))
    simulation_complete=collections.defaultdict(float)
    def finish():
        node=stack.pop();own=max(0.,node['own']);t=timers[node['name']]
        t[0]+=own;t[1]+=node['duration'];t[2]+=1;families[node['category']][index]+=own
        if node['rank']==0 or node['gameplay_delivery']:simulation_complete[index]+=own
        if stack:stack[-1]['own']-=node['duration']
    with events.open(encoding='utf-8-sig',newline='') as stream:
        for row in csv.DictReader(stream):
            n=row['TimerName'];depth=int(row['Depth']);start=float(row['StartTime']);stop=float(row['EndTime']);d=float(row['Duration'])*1000
            if not math.isfinite(d):continue
            if n=='FEngineLoop::Tick':
                while stack:finish()
                active=start>=begin and stop<=end
                if active:index+=1;frame_ms.append(d);frames.append({'start':start,'end':stop,'ms':d})
            if not active:continue
            while stack and stack[-1]['depth']>=depth:finish()
            # Minimum ancestry rank is equivalent to the ordered any(ancestor)
            # classification, without repeatedly scanning the entire stack.
            current=min(rank(n),stack[-1]['rank'] if stack else 8)
            delivery=n in ('GuLiHybrid_MainLogicalBatch','GuLiHybrid_MainGameplayApply') or bool(stack and stack[-1]['gameplay_delivery'])
            stack.append({'name':n,'depth':depth,'duration':d,'own':d,'category':categories[current],'rank':current,'gameplay_delivery':delivery})
        while stack:finish()
    n=len(frames)
    if long_window:assert n>0 and frames[-1]['end']-frames[0]['start']>(end-begin)-.5
    else:assert n>100 and frames[-1]['end']-frames[0]['start']>29.5
    groups={k:distribution([v.get(i,0.) for i in range(n)]) for k,v in families.items()}
    total=distribution(frame_ms);error=sum(x['mean_ms'] for x in groups.values())-total['mean_ms']
    assert abs(error)<.001, ('Family double-counting or incomplete nesting',error)
    native={k:{kk:vv for kk,vv in v.items() if kk!='timed_frames'} for k,v in profile['counters'].items()}
    rows=list(export.rows(statistics))
    worker_calls=collections.defaultdict(list);worker_threads=set()
    for row in export.rows(worker_events):
        worker_threads.add(row['ThreadId']);worker_calls[row['TimerName']].append(float(row['Duration'])*1000)
    worker={k:{'per_call':distribution(v),'cpu_ms_per_second':sum(v)/(end-begin),'calls_per_second':len(v)/(end-begin)} for k,v in worker_calls.items()}
    worker_in_gt=[k for k in timers if k in worker_calls and timers[k][2]]
    result={'capture':capture,'frames':n,'native_frames':profile['engine_frames'],'window':[begin,end],
            'qpc_mapping':clock,'game_thread':total,'exclusive_groups':groups,'sum_error_ms':error,
            'native_metrics':native,'queue_and_worker':profile['sample_metrics'],
            'gt_timers':[{'name':k,'exclusive_ms_frame':v[0]/n,'inclusive_ms_frame':v[1]/n,'calls':v[2],
                          'calls_per_second':v[2]/(frames[-1]['end']-frames[0]['start']),
                          'inclusive_ms_call':v[1]/v[2]} for k,v in sorted(timers.items(),key=lambda p:p[1][0],reverse=True)],
            'all_thread_hotspots':[r for r in rows if r['Name'].startswith(('GuLiSim_','GuLiHybrid_','GuLiSceneUI_','GuLiCommanderHUD_','GuLiCommander_'))],
            'queue_trend':queue_trend(profile),
            'worker_calculations':worker,'worker_thread_ids':sorted(worker_threads),'worker_calculation_timers_on_gt':worker_in_gt,
            'event_csv_sha256':hashlib.file_digest(events.open('rb'),'sha256').hexdigest(),
            'method':'Complete native GT engine frames inside start/end QPC. Each exclusive event assigned once by actual trace ancestry. All-thread statistics are separate CPU totals; do not add parent/child totals.'}
    result['simulation_target']={'limit_mean_ms':1.,'actual':distribution([simulation_complete[i] for i in range(n)]),
        'scope':'Original simulation category plus logical-batch dispatch and gameplay-result application ancestry, counted once. Original category is retained unchanged.'}
    result['simulation_target']['met']=result['simulation_target']['actual']['mean_ms']<1.
    (out/'frames.json').write_text(json.dumps({'frames':frames,'families':{k:[v.get(i,0.) for i in range(n)] for k,v in families.items()},
        'simulation_complete':[simulation_complete[i] for i in range(n)]},separators=(',',':')),encoding='utf-8')
    (out/'summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    if discard:
        assert events.resolve().parent==out.resolve() and events.name=='game-thread-events.csv'
        events.unlink()
    print(json.dumps({'directory':root.name,'frames':n,'game_thread':total,'groups':{k:v['mean_ms'] for k,v in groups.items()}},ensure_ascii=False),flush=True)
    return result


def queue_trend(profile):
    """Report raw and five-second-bin occupancy, without hiding transient peaks.

    Sustained-growth is a diagnostic: every complete bin grows and the final
    bin exceeds the first. It is not a substitute for the 45-minute check.
    """
    samples=profile['network_samples'];start=samples[0]['wall_seconds']
    result={}
    for key in ('input_bytes','output_admitted_bytes','result_reserved_bytes','control_bytes'):
        bins=collections.defaultdict(list)
        values=[]
        for sample in samples:
            value=sample['async_transactions'][key];values.append(value)
            bins[int((sample['wall_seconds']-start)/5)].append(value)
        means=[sum(v)/len(v) for _,v in sorted(bins.items()) if len(v)>=3]
        result[key]={'first':values[0],'last':values[-1],'peak':max(values),
            'five_second_means':means,'strictly_growing':len(means)>=4 and all(b>a for a,b in zip(means,means[1:]))}
    for key in ('worker_ms','main_apply_ms','simulation_ms','logical_steps','inputs','applications'):
        a=samples[0]['async_transactions'];b=samples[-1]['async_transactions']
        seconds=samples[-1]['wall_seconds']-start
        result[key+'_per_second']=(b[key]-a[key])/seconds
    result['hud_full_object_copies_delta']=samples[-1]['async_transactions']['hud_full_object_copies']-samples[0]['async_transactions']['hud_full_object_copies']
    return result


def response_comparison(a,b,frame_ms,response_budget_ms=None):
    """Compare original command identity samples and both mean/tail delays."""
    result={}
    allowance=frame_ms if response_budget_ms is None else response_budget_ms
    if not math.isfinite(allowance) or allowance<0:raise ValueError('Response allowance must be finite and non-negative')
    assert a.get('displacement_timing')==b.get('displacement_timing'), 'Cannot compare different displacement timestamp methods'
    direct=a.get('displacement_timing')=='authoritative_mass_write_and_next_tick_observation_v2'
    names=['command_displacement_latency','command_final_receipt_latency_ms']
    if direct:names+=['command_authoritative_application_latency']
    for name in names:
        if name!='command_final_receipt_latency_ms':
            field='applied_ms' if name=='command_authoritative_application_latency' else 'displacement_ms'
            av=[x[field] for x in a['command_displacement_latency']];bv=[x[field] for x in b['command_displacement_latency']]
            identity=lambda work:sorted((x['command'],x['soldier']) for x in work['command_displacement_latency'])
            assert identity(a)==identity(b), 'Response samples have different command/unit identities'
            assert len(av)==len(bv)==48 and min(av+bv)>=0
            if direct:assert all(x['displacement_ms']>=x['applied_ms']>=0 for w in (a,b) for x in w['command_displacement_latency'])
        else:
            assert set(a[name])==set(b[name]) and len(a[name])==2
            av=list(a[name].values());bv=list(b[name].values())
        ad=distribution(av);bd=distribution(bv)
        delta={k:bd[k]-ad[k] for k in ('mean_ms','p95_ms','peak_ms')}
        result[name]={'A':ad,'B':bd,'added_ms':delta,
            'one_frame_allowance_ms':frame_ms,'within_one_frame':all(v<=frame_ms for v in delta.values()),
            'response_allowance_ms':allowance,'within_response_budget':all(v<=allowance for v in delta.values()),
            'acceptance':not(direct and name=='command_displacement_latency')}
        if direct and name=='command_displacement_latency':
            result[name]['note']='Next-Tick observation retained for diagnosis. The authoritative Mass write timestamp is used for the actual-displacement acceptance limit.'
            result[name]['observation_wait']={k:distribution([x['observation_wait_ms'] for x in w['command_displacement_latency']]) for k,w in (('A',a),('B',b))}
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directories',nargs='*',type=Path)
    p.add_argument('--insights',type=Path,default=Path('D:/UnrealEngine-5.7/Engine/Binaries/Win64/UnrealInsights.exe'))
    p.add_argument('--discard-events',action='store_true')
    p.add_argument('--workers',type=int,choices=range(1,5),default=1,help='Offline processes only; never run during capture')
    p.add_argument('--paired-report',type=Path,help='Summarize a complete paired root after all of its traces have been analyzed')
    p.add_argument('--response-budget-ms',type=float,help='Explicit user-approved extra response allowance; writes a separate report and preserves the original one-frame result')
    a=p.parse_args()
    if not a.directories and not a.paired_report:p.error('Provide capture directories or --paired-report')
    if a.response_budget_ms is not None and (not a.paired_report or not math.isfinite(a.response_budget_ms) or a.response_budget_ms<0):
        p.error('--response-budget-ms requires --paired-report and a finite non-negative allowance')
    if a.workers==1:
        for path in a.directories:analyze(path,a.insights,a.discard_events)
    else:
        with ProcessPoolExecutor(max_workers=a.workers) as pool:
            for _ in pool.map(analyze,a.directories,repeat(a.insights),repeat(a.discard_events)):pass
    if a.paired_report:paired_report(a.paired_report,a.response_budget_ms)


def paired_report(root,response_budget_ms=None):
    root=root.resolve()
    load=lambda path:json.loads(path.read_text(encoding='utf-8'))
    reviewed=root/'paired-reviewed.json'
    plan=load(reviewed if reviewed.exists() else root/'paired.json');assert plan['complete'] and len(plan['windows'])==12
    samples={};contexts={};identities=[]
    for window in plan['windows']:
        adopted=window.get('adopted_directory',window['directory'])
        directory=root/adopted;r=load(directory/'analysis/summary.json')
        assert r['capture']['valid'] and window['valid']
        assert r['capture']['mask']==plan['baseline_mask' if window['variant']=='A' else 'enabled_mask'], 'Paired variant mask differs'
        if plan.get('workers') is not None:assert r['capture']['workers']==plan['workers'], 'Paired worker count differs'
        r['frames_data']=load(directory/'analysis/frames.json')
        r['profile']=load(directory/'profile-after.json')
        r['before_profile']=load(directory/'profile-before.json')
        r['directory']=adopted
        key=(window['scenario'],window['pair'],window['variant']);samples[key]=r
        contexts[key]=load(directory/'context-before.json')
        identities.append(r['capture']['identity'])
    assert all(x==identities[0] for x in identities), 'Frozen identity differs'
    result={'identity':identities[0],'enabled_mask':plan['enabled_mask'],'workers':plan.get('workers'),
        'response_policy':{'kind':'one_control_mean_frame' if response_budget_ms is None else 'explicit_extra_milliseconds','extra_ms':response_budget_ms},
        'scenarios':{},'all_thresholds_passed':True}
    for scenario in ('static','interaction'):
        reference=contexts[(scenario,1,'A')]
        checks=[]
        for pair in range(1,4):
            for variant in ('A','B'):
                context=contexts[(scenario,pair,variant)];w=context['worlds'][0];ref=reference['worlds'][0]
                checks.append({'window':samples[(scenario,pair,variant)]['directory'],
                    'one_world':len(context['worlds'])==1,'actor_classes_equal':w['classes']==ref['classes'],
                    'quality_equal':context['cvars']==reference['cvars'],
                    'viewport_equal':w['controllers'][0]['viewport']==ref['controllers'][0]['viewport'],
                    'camera_equal':all(abs(a-b)<.01 for field in ('camera_location','camera_rotation')
                        for a,b in zip(w['controllers'][0][field],ref['controllers'][0][field]))})
        variants={}
        for variant in ('A','B'):
            chosen=[samples[(scenario,pair,variant)] for pair in range(1,4)]
            frames=[f['ms'] for r in chosen for f in r['frames_data']['frames']]
            group_names=set().union(*(r['frames_data']['families'] for r in chosen))
            groups={key:distribution([v for r in chosen for v in r['frames_data']['families'].get(key,[0.]*r['frames'])]) for key in group_names}
            elapsed=sum(r['frames_data']['frames'][-1]['end']-r['frames_data']['frames'][0]['start'] for r in chosen)
            steps=sum(next(t['calls'] for t in r['gt_timers'] if t['name']=='GuLiSim_Shared/CommanderMass') for r in chosen)
            simulation_cpu=sum(r['exclusive_groups']['simulation']['mean_ms']*r['frames'] for r in chosen)
            deltas={key:sum(r['profile']['hotspot_cumulative'][key]-r['before_profile']['hotspot_cumulative'][key] for r in chosen)
                    for key in chosen[0]['profile']['hotspot_cumulative']}
            queue={}
            for key in chosen[0]['queue_and_worker']:
                values=[r['queue_and_worker'][key] for r in chosen]
                queue[key]={'mean':sum(v['mean']*v['samples'] for v in values)/sum(v['samples'] for v in values),
                            'p95_per_window':[v['p95'] for v in values],'peak':max(v['peak'] for v in values)}
            variants[variant]={'game_thread':distribution(frames),'groups':groups,'queue_and_worker':queue,
                'logical_steps':steps,'steps_per_second':steps/elapsed,'simulation_ms_per_step':simulation_cpu/steps,
                'simulation_cpu_ms_per_second':simulation_cpu/elapsed,'hotspot_counter_deltas':deltas,
                'windows':[{'directory':r['directory'],'game_thread':r['game_thread']} for r in chosen]}
            if all('simulation_complete' in r['frames_data'] for r in chosen):
                complete=distribution([v for r in chosen for v in r['frames_data']['simulation_complete']])
                variants[variant]['simulation_target']={'actual':complete,'limit_mean_ms':1.,'met':complete['mean_ms']<1.}
        pairs=[]
        for pair in range(1,4):
            ar=samples[(scenario,pair,'A')];br=samples[(scenario,pair,'B')]
            a=ar['game_thread'];b=br['game_thread']
            pairs.append({'pair':pair,'A':a,'B':b,'improvement_percent':(1-b['mean_ms']/a['mean_ms'])*100,
                          'mean_improves':b['mean_ms']<a['mean_ms'],
                          'p95_within_limit':b['p95_ms']-a['p95_ms']<=max(.5,a['p95_ms']*.03)})
            if scenario=='interaction':
                pairs[-1]['response']=response_comparison(ar['profile']['hotspot_workload'],br['profile']['hotspot_workload'],a['mean_ms'],response_budget_ms)
                pairs[-1]['response_within_limit']=all(v['within_response_budget'] for v in pairs[-1]['response'].values() if v['acceptance'])
            else:pairs[-1]['response_within_limit']=True
        a=variants['A']['game_thread'];b=variants['B']['game_thread'];gain=(1-b['mean_ms']/a['mean_ms'])*100
        comparable=all(all(v for k,v in c.items() if k!='window') for c in checks)
        queue_checks={samples[(scenario,pair,variant)]['directory']:queue_trend(samples[(scenario,pair,variant)]['profile'])
                      for pair in range(1,4) for variant in ('A','B')}
        no_continuous_growth=not any(v.get('strictly_growing',False) for q in queue_checks.values() for v in q.values() if isinstance(v,dict))
        passed=comparable and gain>=3 and no_continuous_growth and all(p['mean_improves'] and p['p95_within_limit'] and p['response_within_limit'] for p in pairs)
        result['scenarios'][scenario]={'variants':variants,'pairs':pairs,'improvement_percent':gain,
            'comparability_checks':checks,'queue_checks':queue_checks,'thresholds_passed':passed}
        result['all_thresholds_passed'] &= passed
    result['simulation_target_met']=all(v['variants']['B'].get('simulation_target',{}).get('met',False) for v in result['scenarios'].values())
    result['all_objectives_met']=result['all_thresholds_passed'] and result['simulation_target_met']
    result['threshold_scope']='all_thresholds_passed covers paired frame gain, P95, response and short-window queue checks. The separate <1 ms simulation objective is included only in all_objectives_met.'
    result['scope']='Paired optimization evidence, not full G/F, player or formal performance acceptance. Static means no player input; normal autonomous gameplay remains active. Worker percentiles remain per-window, not averaged into a pooled percentile.'
    report_name='summary.json' if response_budget_ms is None else f'summary-response-{response_budget_ms:g}ms.json'
    (root/report_name).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'thresholds_passed':result['all_thresholds_passed'],'scenarios':{k:{'improvement_percent':v['improvement_percent'],'passed':v['thresholds_passed']} for k,v in result['scenarios'].items()}},ensure_ascii=False),flush=True)
    return result

if __name__=='__main__':main()
