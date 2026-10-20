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

HYBRID_SCOPE_VERSION = 5


def predictive_scope(name):
    # Mass names its processor root without the underscore used by our child
    # scopes. Include its own collection/apply cost as well as those children.
    return name.startswith(('GuLiCommander_Predictive', 'GuLiCommanderPredictiveAvoidanceProcessor'))


def preserve_previous_analysis(out):
    previous = out / 'summary.json'
    if not previous.is_file():
        return
    version = json.loads(previous.read_text(encoding='utf-8'))['hybrid_target'].get('scope_version', 1)
    if version == HYBRID_SCOPE_VERSION:
        return
    for name in ('summary', 'frames'):
        source = out / (name + '.json')
        target = out / f'{name}.hybrid-scope-v{version}.json'
        if source.is_file():
            if target.exists() and target.read_bytes() != source.read_bytes():
                raise ValueError('Refusing to overwrite retained analysis: ' + str(target))
            if not target.exists():
                target.write_bytes(source.read_bytes())


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
                    'GuLiHybrid_ControlPointSnapshot') or predictive_scope(name):value=0
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
    hybrid_complete=collections.defaultdict(float)
    world_update_complete=collections.defaultdict(float)
    slate_layout_draw=collections.defaultdict(float)
    slate_game_ui=collections.defaultdict(float)
    slate_editor_only=collections.defaultdict(float)
    slate_game_window=collections.defaultdict(float)
    slate_game_paint_calls=collections.defaultdict(int)
    slate_platform_input=collections.defaultdict(float)
    slate_local_player=collections.defaultdict(float)
    def finish():
        node=stack.pop();own=max(0.,node['own']);t=timers[node['name']]
        t[0]+=own;t[1]+=node['duration'];t[2]+=1;families[node['category']][index]+=own
        if node['rank']==0 or node['gameplay_delivery']:simulation_complete[index]+=own
        if node['hybrid_delivery']:hybrid_complete[index]+=own
        if node['world_delivery']:world_update_complete[index]+=own
        if node['slate_layout_draw']:slate_layout_draw[index]+=own
        if node['slate_game_ui']:slate_game_ui[index]+=own
        if node['slate_editor_only'] and not node['slate_game_window']:slate_editor_only[index]+=own
        if node['slate_game_window']:slate_game_window[index]+=own
        if node['name']=='GuLiSlate_GameWindow_Paint':slate_game_paint_calls[index]+=1
        if node['slate_platform_input']:slate_platform_input[index]+=own
        if node['slate_local_player']:slate_local_player[index]+=own
        if stack:stack[-1]['own']-=node['duration']
    with events.open(encoding='utf-8-sig',newline='') as stream:
        reader=csv.reader(stream);header=next(reader)
        name_column,depth_column,start_column,end_column,duration_column=(header.index(k)
            for k in ('TimerName','Depth','StartTime','EndTime','Duration'))
        # Traces contain millions of events. Avoid constructing a temporary
        # dictionary per CSV row; preserve the same ancestry and time arithmetic.
        for row in reader:
            n=row[name_column];depth=int(row[depth_column]);start=float(row[start_column]);stop=float(row[end_column]);d=float(row[duration_column])*1000
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
            world=n in ('GuLiHybrid_WorldTick','GuLiHybrid_MainLogicalBatch','GuLiHybrid_MainGameplayApply',
                         'GuLiHybrid_MainReceipt','GuLiCommander_MovementApply','GuLiCommander_SoldierCombat',
                         'GuLiHybrid_ControlPointSnapshot') or bool(stack and stack[-1]['world_delivery'])
            # The acceptance scope includes pre-existing transport dispatch too.
            # Keeping it outside WorldTick must not hide Hybrid GT CPU work.
            hybrid=world or n.startswith(('GuLiHybrid_','GuLiSim_')) or predictive_scope(n) or n=='GuLiCommander_ManualAvoidanceRefresh' or bool(stack and stack[-1]['hybrid_delivery'])
            slate=n in ('Slate::Tick (Time and Widgets)','Slate::DrawWindows','Slate::Prepass','Slate::DrawWindow') or n.startswith('GuLiSlate_') or bool(stack and stack[-1]['slate_layout_draw'])
            editor_only=n in ('GuLiSlate_EditorOnly_Prepass','GuLiSlate_EditorOnly_Paint') or bool(stack and stack[-1]['slate_editor_only'])
            game_window=n in ('GuLiSlate_GameWindow_Prepass','GuLiSlate_GameWindow_Paint') or bool(stack and stack[-1]['slate_game_window'])
            # Global invalidation repaints dirty leaves directly, bypassing the
            # slow-path Game UI wrapper. Keep those project scopes visible.
            game_ui=n in ('Paint: Game UI','GuLiSceneUI_Paint','GuLiCommanderMiniMap_TerrainPaint','GuLiCommanderMiniMap_DynamicPaint') or bool(stack and stack[-1]['slate_game_ui'])
            platform=n=='Slate::Tick (Platform and Input)' or bool(stack and stack[-1]['slate_platform_input'])
            local_player=n=='ProcessLocalPlayerSlateOperations' or bool(stack and stack[-1]['slate_local_player'])
            stack.append({'name':n,'depth':depth,'duration':d,'own':d,'category':categories[current],'rank':current,
                          'gameplay_delivery':delivery,'hybrid_delivery':hybrid,'world_delivery':world,
                          'slate_layout_draw':slate,'slate_game_ui':game_ui,'slate_platform_input':platform,'slate_local_player':local_player,
                          'slate_editor_only':editor_only,'slate_game_window':game_window})
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
    with events.open('rb') as stream:event_hash=hashlib.file_digest(stream,'sha256').hexdigest()
    result={'capture':capture,'frames':n,'native_frames':profile['engine_frames'],'window':[begin,end],
            'qpc_mapping':clock,'game_thread':total,'exclusive_groups':groups,'sum_error_ms':error,
            'native_metrics':native,'queue_and_worker':profile['sample_metrics'],
            'gt_timers':[{'name':k,'exclusive_ms_frame':v[0]/n,'inclusive_ms_frame':v[1]/n,'calls':v[2],
                          'calls_per_second':v[2]/(frames[-1]['end']-frames[0]['start']),
                          'inclusive_ms_call':v[1]/v[2]} for k,v in sorted(timers.items(),key=lambda p:p[1][0],reverse=True)],
            'all_thread_hotspots':[r for r in rows if r['Name'].startswith(('GuLiSim_','GuLiHybrid_','GuLiSceneUI_','GuLiCommanderHUD_','GuLiCommander_'))],
            'queue_trend':queue_trend(profile),
            'worker_calculations':worker,'worker_thread_ids':sorted(worker_threads),'worker_calculation_timers_on_gt':worker_in_gt,
            'event_csv_sha256':event_hash,
            'method':'Complete native GT engine frames inside start/end QPC. Each exclusive event assigned once by actual trace ancestry. All-thread statistics are separate CPU totals; do not add parent/child totals.'}
    result['simulation_target']={'limit_mean_ms':1.,'actual':distribution([simulation_complete[i] for i in range(n)]),
        'scope':'Original simulation category plus logical-batch dispatch and gameplay-result application ancestry, counted once. Original category is retained unchanged.'}
    result['simulation_target']['met']=result['simulation_target']['actual']['mean_ms']<1.
    hybrid_values=[hybrid_complete[i] for i in range(n)]
    result['hybrid_target']={'limit_mean_ms':5.,'actual':distribution(hybrid_values),
        'cpu_ms_per_second':sum(hybrid_values)/(frames[-1]['end']-frames[0]['start']),
        'scope':'All Hybrid/GuLiSim GT scopes, including transport dispatch, plus moved movement/combat and the complete predictive avoidance processor root; union of trace ancestry. Worker CPU is separate.',
        'scope_version':HYBRID_SCOPE_VERSION}
    result['hybrid_target']['met']=result['hybrid_target']['actual']['mean_ms']<=5.
    steps=next((t['calls'] for t in result['gt_timers'] if t['name']=='GuLiSim_Shared/CommanderMass'),0)
    result['hybrid_target']['logical_steps']=steps
    result['hybrid_target']['ms_per_logical_step']=sum(hybrid_values)/steps if steps else None
    samples=profile.get('network_samples',[])
    clock_check={'available':False,'nominal_hz':60.,'maintained':False}
    if len(samples)>=2 and all('wall_seconds' in sample and 'world_seconds' in sample
            and 'logical_tick' in sample.get('simulation_census',{}) for sample in (samples[0],samples[-1])):
        first,last=samples[0],samples[-1]
        elapsed_clock=last['wall_seconds']-first['wall_seconds']
        ticks=last['simulation_census']['logical_tick']-first['simulation_census']['logical_tick']
        hz=ticks/elapsed_clock if elapsed_clock>0 else 0
        world_seconds=last['world_seconds']-first['world_seconds']
        world_hz=ticks/world_seconds if world_seconds>0 else 0
        clock_check.update(available=True,wall_seconds=elapsed_clock,logical_ticks=ticks,actual_hz=hz,
                           world_seconds=world_seconds,world_hz=world_hz,
                           wall_world_gap_seconds=elapsed_clock-world_seconds,
                           game_time_rate_maintained=abs(ticks-world_seconds*60)<=max(6,world_seconds*.6),
                           maintained=abs(ticks-elapsed_clock*60)<=max(6,elapsed_clock*.6),
                           tolerance='1% or six ticks for asynchronous boundary readback; not a changed gameplay rate',
                           interpretation='World time distinguishes dropped simulation time from an engine pause or clamped long frame. Real-time acceptance still uses wall time; a world-time pass does not erase a hitch.')
    result['simulation_clock']=clock_check
    result['hybrid_target']['met'] &= clock_check['maintained']
    result['world_update_only']={'actual':distribution([world_update_complete[i] for i in range(n)]),
        'scope':'Historical scope: WorldTick and moved logical/receipt/gameplay applications, excluding pre-existing outer transport. Diagnostic only; not the complete Hybrid acceptance cost.'}
    slate_values=[slate_layout_draw[i] for i in range(n)]
    elapsed=frames[-1]['end']-frames[0]['start']
    result['slate_full_pie']={'actual':distribution(slate_values),
        'scope':'GT Time and Widgets / DrawWindows / Prepass / DrawWindow ancestry union, plus explicitly attributed window work outside those scopes. Includes every window and shared work in this process.',
        'cpu_ms_per_second':sum(slate_values)/elapsed}
    scope_file=root/'slate-window-scope.json'
    scope=json.loads(scope_file.read_text(encoding='utf-8-sig')) if scope_file.exists() else None
    result['slate_target']=slate_client_scope(slate_values,[slate_editor_only[i] for i in range(n)],
        [slate_game_window[i] for i in range(n)],elapsed,scope,
        [slate_game_paint_calls[i] for i in range(n)])
    result['slate_target'].update({
        'game_ui_paint_subset':distribution([slate_game_ui[i] for i in range(n)]),
        'platform_input_separate':distribution([slate_platform_input[i] for i in range(n)]),
        'local_player_operations_separate':distribution([slate_local_player[i] for i in range(n)])})
    result['workload']=workload_activity(profile)
    if capture.get('high_load'):
        from hybrid_workload import sampled_window_load
        result['workload']['sampled_window_load']=sampled_window_load(profile)
        result['hybrid_target']['timing_met']=result['hybrid_target']['met']
        result['hybrid_target']['met'] &= result['workload']['sampled_window_load']['maintained']
    preserve_previous_analysis(out)
    (out/'frames.json').write_text(json.dumps({'frames':frames,'families':{k:[v.get(i,0.) for i in range(n)] for k,v in families.items()},
        'simulation_complete':[simulation_complete[i] for i in range(n)],'hybrid_complete':hybrid_values,
        'world_update_only':[world_update_complete[i] for i in range(n)],'slate_layout_draw':slate_values,
        'slate_editor_only':[slate_editor_only[i] for i in range(n)],
        'slate_game_window':[slate_game_window[i] for i in range(n)],
        'slate_game_paint_calls':[slate_game_paint_calls[i] for i in range(n)]},separators=(',',':')),encoding='utf-8')
    (out/'summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    if discard:
        assert events.resolve().parent==out.resolve() and events.name=='game-thread-events.csv'
        events.unlink()
    print(json.dumps({'directory':root.name,'frames':n,'game_thread':total,'groups':{k:v['mean_ms'] for k,v in groups.items()}},ensure_ascii=False),flush=True)
    return result


def slate_client_scope(full,editor_only,game_window,seconds,metadata,paint_calls):
    """Conservative complete UI bound; missing ownership cannot become a zero-cost pass."""
    assert len(full)==len(editor_only)==len(game_window) and full and seconds>0
    residual=[total-editor for total,editor in zip(full,editor_only)]
    assert all(editor>=0 and game>=0 and bound+1e-6>=game
               for editor,game,bound in zip(editor_only,game_window,residual)), 'Invalid/nested Slate subtraction'
    game_rows=[row for row in (metadata or {}).get('windows',[]) if row.get('kind')=='game']
    covered=bool(metadata and metadata.get('scope_version')==2 and metadata.get('complete') and metadata.get('valid')
        and metadata.get('invalid_pairs')==0 and metadata.get('binding_changes')==0
        and len(game_rows)==1 and not game_rows[0].get('excluded_editor_only')
        and len(paint_calls)==len(full) and all(c>=1 for c in paint_calls))
    return {'scope_version':2,'limit_mean_ms':2.5,'coverage_valid':covered,
        'metric_kind':'conservative_client_upper_bound',
        'actual':distribution(residual) if covered else None,
        'cpu_ms_per_second':sum(residual)/seconds if covered else None,
        'timing_met':sum(residual)/len(residual)<2.5 if covered else None,
        'attributed_game_window':distribution(game_window) if covered else None,
        'unassigned_and_shared_upper_bound':distribution([max(0,b-g) for b,g in zip(residual,game_window)]) if covered else None,
        'excluded_editor_windows':distribution(editor_only) if covered else None,
        'coverage_report':metadata,
        'scope':'Complete game-client Slate layout/draw upper bound: full PIE union minus proven editor-only window root prepass/PaintWindow intervals. All unassigned/shared work stays charged to the client, including batching, cursors, popups and observer overhead. The attributed game window alone and the legacy paint subset are insufficient. Platform input is reported separately.',
        'coverage_note':'Native window identity and per-frame game paint coverage verified.' if covered else
            'Complete game-client layout/draw attribution unavailable or invalid; do not compare the legacy paint subset with 2.5 ms.'}


def workload_activity(profile):
    samples=profile['network_samples'];first=samples[0];last=samples[-1]
    seconds=last['wall_seconds']-first['wall_seconds']
    activity={}
    for field in ('request_kind_rule','receipt_kind_status'):
        a=first['async_transactions'].get(field,{});b=last['async_transactions'].get(field,{})
        activity[field]={k:{'count':b.get(k,0)-a.get(k,0),'per_second':(b.get(k,0)-a.get(k,0))/seconds}
                         for k in a.keys()|b.keys() if b.get(k,0)!=a.get(k,0)}
    snapshots=[s['simulation_census'] for s in samples if 'simulation_census' in s]
    activity['simulation_census_samples']=snapshots
    activity['task_activity_samples']=[{'wall_seconds':s['wall_seconds'],'by_definition':s['task_activity']}
                                       for s in samples if 'task_activity' in s]
    if snapshots:
        names=set().union(*(s['by_definition'] for s in snapshots))
        activity['local_live_simulation_range']={name:{'min':min(s['by_definition'].get(name,{}).get('local_simulation',0) for s in snapshots),
            'max':max(s['by_definition'].get(name,{}).get('local_simulation',0) for s in snapshots)} for name in sorted(names)}
        activity['directory_records']={'start':snapshots[0]['directory_records'],'end':snapshots[-1]['directory_records']}
        activity['retired_target_records']={'start':snapshots[0]['retired_target_records'],'end':snapshots[-1]['retired_target_records']}
    return activity


CORE_QUEUE_KEYS=tuple('core_'+key for key in (
    'pending','controlled_intents','retry','active','waiting_result','retained','deferred_state',
    'waiting_result_authority','waiting_result_authority_bytes','publication_messages','publication_bytes',
    'command_outbox','incoming_commands','freeze_requested'))


def queue_trend(profile):
    """Report raw and five-second-bin occupancy, without hiding transient peaks.

    Sustained-growth is a diagnostic: every complete bin grows and the final
    bin exceeds the first. It is not a substitute for the 45-minute check.
    """
    samples=profile['network_samples'];start=samples[0]['wall_seconds']
    covered=all(key in sample['async_transactions'] for sample in samples for key in CORE_QUEUE_KEYS)
    result={'core_queue_coverage_complete':covered}
    additional=CORE_QUEUE_KEYS if covered else ()
    for key in ('input_bytes','output_admitted_bytes','result_reserved_bytes','control_bytes')+additional:
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
    direct=a.get('displacement_timing') in (
        'authoritative_mass_write_and_next_tick_observation_v2',
        'authoritative_mass_write_and_ordered_next_tick_observation_v3')
    expected=a.get('response_probe_count',48)
    assert expected in (2,48) and b.get('response_probe_count',48)==expected, 'Response probe recipe mismatch'
    names=['command_displacement_latency','command_final_receipt_latency_ms']
    if direct:names+=['command_authoritative_application_latency']
    for name in names:
        if name!='command_final_receipt_latency_ms':
            field='applied_ms' if name=='command_authoritative_application_latency' else 'displacement_ms'
            av=[x[field] for x in a['command_displacement_latency']];bv=[x[field] for x in b['command_displacement_latency']]
            identity=lambda work:sorted((x['command'],x['soldier']) for x in work['command_displacement_latency'])
            assert identity(a)==identity(b), 'Response samples have different command/unit identities'
            assert len(av)==len(bv)==expected and len(set(identity(a)))==expected and min(av+bv)>=0, 'Missing or repeated response probes'
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
    p.add_argument('--objective',choices=('simulation','hybrid'),default='simulation',help='hybrid: complete GT world update <=5 ms, strict P95 and one-frame response')
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
    if a.objective=='hybrid' and a.response_budget_ms is not None:p.error('Hybrid acceptance retains the approved one-frame allowance')
    if a.paired_report:paired_report(a.paired_report,a.response_budget_ms,a.objective)


def paired_report(root,response_budget_ms=None,objective='simulation'):
    root=root.resolve()
    load=lambda path:json.loads(path.read_text(encoding='utf-8'))
    reviewed=root/'paired-reviewed.json'
    plan=load(reviewed if reviewed.exists() else root/'paired.json')
    scenarios=plan.get('scenarios',['static','interaction'])
    pairs_required=plan.get('pair_count',3)
    regression_600=plan.get('purpose')=='600-regression'
    quick=plan.get('quick_comparison',False)
    assert pairs_required==(1 if regression_600 or quick else 3)
    assert plan['complete'] and len(plan['windows'])==2*pairs_required*len(scenarios)
    if objective=='hybrid' and not regression_600:assert plan.get('high_load'), 'The 600-unit regression cannot qualify the high-load 5 ms target'
    frozen_builds=plan.get('schema')=='hybrid-frozen-pairs/v1'
    assert not frozen_builds or objective=='hybrid', 'Frozen replacement acceptance requires --objective hybrid'
    samples={};contexts={};identities=[]
    for window in plan['windows']:
        adopted=window.get('adopted_directory',window['directory'])
        directory=root/adopted;r=load(directory/'analysis/summary.json')
        assert r['capture']['valid'] and window['valid']
        if objective=='hybrid':
            assert r['hybrid_target'].get('scope_version')==HYBRID_SCOPE_VERSION, 'Reanalyze retained traces including the predictive avoidance processor root'
        assert r['capture']['mask']==plan['baseline_mask' if window['variant']=='A' else 'enabled_mask'], 'Paired variant mask differs'
        if plan.get('workers') is not None:assert r['capture']['workers']==plan['workers'], 'Paired worker count differs'
        r['frames_data']=load(directory/'analysis/frames.json')
        r['profile']=load(directory/'profile-after.json')
        r['before_profile']=load(directory/'profile-before.json')
        r['directory']=adopted
        key=(window['scenario'],window['pair'],window['variant']);samples[key]=r
        contexts[key]=load(directory/'context-before.json')
        identities.append(r['capture']['identity'])
        if frozen_builds:
            expected=plan['builds'][window['variant']]
            assert r['capture']['identity']==expected['identity'], 'Window does not use its frozen build'
            assert r['capture']['sync_identity']['implementation']==expected['implementation'], 'Loaded implementation differs'
            assert r['capture']['logic_sync_verified'], 'Logic client implementation was not verified'
    if frozen_builds:
        a,b=(plan['builds'][v] for v in ('A','B'))
        assert a['identity']['game_dll']!=b['identity']['game_dll'], 'Same DLL cannot represent deleted old/new paths'
        assert a['implementation']=='hybrid-sync-selectable-v1' and a['mask']==31
        assert b['implementation']=='hybrid-sync-unique-v2' and b['mask']==31
        assert a['config']==b['config'], 'Frozen configuration differs'
        assert a.get('engine') and a['engine']==b.get('engine'), 'Frozen source engine files differ'
        assert a.get('coordinator_runtime') and a['coordinator_runtime']==b.get('coordinator_runtime'), 'Frozen Coordinator dependencies differ'
        assert all(a['identity'][k]==b['identity'][k] for k in ('map','manifest','coordinator')), 'Frozen common inputs differ'
    else:
        assert all(x==identities[0] for x in identities), 'Frozen identity differs'
    result={'identity':identities[0],'enabled_mask':plan['enabled_mask'],'workers':plan.get('workers'),
        'response_policy':{'kind':'one_control_mean_frame' if response_budget_ms is None else 'explicit_extra_milliseconds','extra_ms':response_budget_ms},
        'scenarios':{},'all_thresholds_passed':True}
    for scenario in scenarios:
        reference=contexts[(scenario,1,'A')]
        checks=[]
        for pair in range(1,pairs_required+1):
            for variant in ('A','B'):
                context=contexts[(scenario,pair,variant)];w=context['worlds'][0];ref=reference['worlds'][0]
                checks.append({'window':samples[(scenario,pair,variant)]['directory'],
                    'one_world':len(context['worlds'])==1,'actor_classes_equal':w['classes']==ref['classes'],
                    'quality_equal':context['cvars']==reference['cvars'],
                    'viewport_equal':w['controllers'][0]['viewport']==ref['controllers'][0]['viewport'],
                    'camera_equal':all(abs(a-b)<.01 for field in ('camera_location','camera_rotation')
                        for a,b in zip(w['controllers'][0][field],ref['controllers'][0][field]))})
                if objective=='hybrid':
                    checks[-1]['simulation_clock_60hz']=samples[(scenario,pair,variant)].get('simulation_clock',{}).get('maintained',False)
                    from hybrid_workload import comparable_census,sampled_window_load,history_scale_comparison
                    own=samples[(scenario,pair,variant)]['profile']['hotspot_workload'].get('simulation_census_start')
                    control=samples[(scenario,pair,'A')]['profile']['hotspot_workload'].get('simulation_census_start')
                    checks[-1]['simulation_authority_equal']=comparable_census(control,own)
                    exact_units=frozen_builds and not regression_600 and scenario!='combat'
                    checks[-1]['full_window_local_load']=sampled_window_load(samples[(scenario,pair,variant)]['profile'],exact_units)['maintained']
                    if frozen_builds:
                        history=history_scale_comparison(control,own)
                        samples[(scenario,pair,variant)]['history_comparison']=history
                        checks[-1]['history_scale_matched']=history['matched']
                    if regression_600:
                        from hybrid_workload import simulation_shape
                        baseline_shape=simulation_shape(control)
                        observations=samples[(scenario,pair,variant)]['profile']['network_samples']
                        checks[-1]['regression_600_local_load']=bool(observations) and sum(
                            baseline_shape.get(name,{}).get('local_simulation',0) for name in ('unit/1','unit/2'))==300 and all(
                            simulation_shape(row['simulation_census'])==baseline_shape for row in observations)
        variants={}
        for variant in ('A','B'):
            chosen=[samples[(scenario,pair,variant)] for pair in range(1,pairs_required+1)]
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
            if all('hybrid_complete' in r['frames_data'] for r in chosen):
                values=[v for r in chosen for v in r['frames_data']['hybrid_complete']]
                complete=distribution(values)
                maintained=all(sampled_window_load(r['profile'])['maintained'] for r in chosen) if objective=='hybrid' else True
                variants[variant]['hybrid_target']={'actual':complete,'limit_mean_ms':5.,'met':complete['mean_ms']<=5. and maintained,
                    'timing_met':complete['mean_ms']<=5.,'full_window_local_load':maintained,
                    'cpu_ms_per_second':sum(values)/elapsed,'ms_per_logical_step':sum(values)/steps}
        pairs=[]
        for pair in range(1,pairs_required+1):
            ar=samples[(scenario,pair,'A')];br=samples[(scenario,pair,'B')]
            a=ar['game_thread'];b=br['game_thread']
            pairs.append({'pair':pair,'A':a,'B':b,'improvement_percent':(1-b['mean_ms']/a['mean_ms'])*100,
                          'mean_improves':b['mean_ms']<a['mean_ms'],
                          'p95_within_limit':b['p95_ms']-a['p95_ms']<=max(.5,a['p95_ms']*.03)})
            if scenario=='interaction' or objective=='hybrid':
                pairs[-1]['response']=response_comparison(ar['profile']['hotspot_workload'],br['profile']['hotspot_workload'],a['mean_ms'],response_budget_ms)
                pairs[-1]['response_within_limit']=all(v['within_response_budget'] for v in pairs[-1]['response'].values() if v['acceptance'])
            else:pairs[-1]['response_within_limit']=True
            if objective=='hybrid':
                ha=ar['hybrid_target']['actual'];hb=br['hybrid_target']['actual']
                pairs[-1]['hybrid']={'A':ha,'B':hb,'mean_target_met':hb['mean_ms']<=5. and sampled_window_load(br['profile'])['maintained'],
                    'full_window_local_load':sampled_window_load(br['profile'],frozen_builds and not regression_600 and scenario!='combat'),
                    'mean_improves':hb['mean_ms']<ha['mean_ms'], 'p95_not_worse':hb['p95_ms']<=ha['p95_ms']}
                pairs[-1]['mean_improves'] &= pairs[-1]['hybrid']['mean_improves']
                pairs[-1]['p95_within_limit']=b['p95_ms']<=a['p95_ms'] and hb['p95_ms']<=ha['p95_ms']
            if frozen_builds:
                pairs[-1]['initial_history']=br['history_comparison']
        a=variants['A']['game_thread'];b=variants['B']['game_thread'];gain=(1-b['mean_ms']/a['mean_ms'])*100
        # Only the final build comparison has the approved replacement policy:
        # combat casualties stay visible but do not turn a correctly matched
        # real firefight into a static full-population test. Initial authority,
        # types and native combat inputs are still mandatory and exact.
        diagnostic_fields={'window'}
        if frozen_builds and (scenario=='combat' or regression_600):diagnostic_fields.add('full_window_local_load')
        comparable=all(all(v for k,v in c.items() if k not in diagnostic_fields) for c in checks)
        if frozen_builds and scenario=='combat':
            recipes=[samples[(scenario,pair,variant)]['profile']['hotspot_workload']['frozen_recipe']['combat_inputs']
                     for pair in range(1,pairs_required+1) for variant in ('A','B')]
            comparable &= all(recipe==recipes[0] for recipe in recipes)
        queue_checks={samples[(scenario,pair,variant)]['directory']:queue_trend(samples[(scenario,pair,variant)]['profile'])
                      for pair in range(1,pairs_required+1) for variant in ('A','B')}
        no_continuous_growth=not any(v.get('strictly_growing',False) for q in queue_checks.values() for v in q.values() if isinstance(v,dict))
        queue_coverage=not frozen_builds or all(q['core_queue_coverage_complete'] for q in queue_checks.values())
        passed=comparable and gain>=3 and no_continuous_growth and queue_coverage and all(p['mean_improves'] and p['p95_within_limit'] and p['response_within_limit'] for p in pairs)
        result['scenarios'][scenario]={'variants':variants,'pairs':pairs,'improvement_percent':gain,
            'comparability_checks':checks,'queue_checks':queue_checks,'thresholds_passed':passed}
        result['all_thresholds_passed'] &= passed
    result['simulation_target_met']=all(v['variants']['B'].get('simulation_target',{}).get('met',False) for v in result['scenarios'].values())
    result['all_objectives_met']=result['all_thresholds_passed'] and result['simulation_target_met']
    result['threshold_scope']='all_thresholds_passed covers paired frame gain, P95, response and short-window queue checks. The separate <1 ms simulation objective is included only in all_objectives_met.'
    result['scope']='Paired optimization evidence, not full G/F, player or formal performance acceptance. The static bulk scene includes two single-unit response probes; combat reserves 48 local probes outside the engagement. Normal autonomous gameplay remains active. Worker percentiles remain per-window, not averaged into a pooled percentile.'
    report_name='summary.json' if response_budget_ms is None else f'summary-response-{response_budget_ms:g}ms.json'
    if objective=='hybrid':
        result['hybrid_target_met']=all(v['variants']['B'].get('hybrid_target',{}).get('met',False)
            and all(p['hybrid']['mean_target_met'] and p['hybrid']['p95_not_worse'] for p in v['pairs']) for v in result['scenarios'].values())
        result['all_objectives_met']=result['all_thresholds_passed'] and result['hybrid_target_met']
        result['threshold_scope']='Complete Hybrid GT <=5 ms in every B window; Hybrid and whole-frame P95 do not worsen. Authority/life/type census, one-frame response and queue checks must pass. Long-soak and gameplay correctness are separate required evidence.'
        report_name='summary-hybrid.json'
    if frozen_builds:
        result['frozen_builds']=plan['builds']
        result['replacement_performance_passed']=result['all_thresholds_passed']
        result['threshold_scope']='Replacement performance requires matched frozen builds and inputs, improved cost, non-worsened P95, one frozen control-frame response allowance and no unreviewed sustained queue growth. Combat losses remain recorded. The 5 ms goal is separate; correctness and 45-minute continuous business remain required.'
        if quick:
            result['threshold_scope']='User-requested short frozen-build comparison; long soak and three repeats are retired. Report matching inputs, census, cost, P95, response and observed queues. The 5 ms goal is separate. Mismatched history or activity remains an explicit comparison limitation.'
            result['quick_comparison']=True
    (root/report_name).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'thresholds_passed':result['all_thresholds_passed'],'scenarios':{k:{'improvement_percent':v['improvement_percent'],'passed':v['thresholds_passed']} for k,v in result['scenarios'].items()}},ensure_ascii=False),flush=True)
    return result

if __name__=='__main__':main()
