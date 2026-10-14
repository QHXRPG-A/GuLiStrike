"""Only runs after both gates. Raw timestamped observations share one QPC cut."""
from __future__ import annotations

import gzip
import json
import math
from pathlib import Path
import time

from runtime_evidence import read_json_shared
from windows_cpu_topology import sample_process
from windows_network_trace import qpc, frequency, publish


def distribution(values):
    if not values or any(not math.isfinite(v) or v<0 for v in values):
        raise RuntimeError('A required performance distribution is empty or invalid')
    ordered=sorted(values)
    return {'count':len(values),'mean':sum(values)/len(values),
        **{label:ordered[max(0,math.ceil(len(ordered)*fraction)-1)]
           for label,fraction in (('p50',.5),('p95',.95),('p99',.99),('max',1))}}


def run(plan,args,records,read_state,write_json,states):
    from run_hybrid_acceptance import verify_additional_flights,verify_player_simulation
    helper=Path(args.etw_helper or '')
    calibration_path=Path(args.etw_calibration or '')
    calibration=read_json_shared(calibration_path)
    if not calibration.get('passed') or not calibration.get('two_pid_attribution'):
        raise RuntimeError('Dual PID socket/ETW calibration did not pass')
    if time.time()-calibration_path.stat().st_mtime>12*3600:
        raise RuntimeError('ETW calibration is older than this twelve-hour acceptance session')
    if not (helper/'helper-ready.json').is_file() or (helper/'helper-exited.json').exists():
        raise RuntimeError('No active owned ETW helper')
    output=Path(plan['output']);endpoints=[(e,p) for e,p in records if e['sample']]
    if len(endpoints)!=2 or sum(e['rendered'] for e,p in endpoints)!=1:
        raise RuntimeError('Performance must select exactly backend and rendered PID')
    control={'schema':2,'run_id':plan['run_id'],'scenario':'performance','stage_id':'01-steady','stage_number':1,'phase':'steady'}
    def refresh():
        for e,p in records:
            if p.poll() is not None: raise RuntimeError(e['name']+' exited during performance')
            value=read_state(Path(e['directory'])/'state.json',plan['run_id'],p.pid)
            if value: states[e['name']]=value
        verify_additional_flights(plan,states)
        if any(v.get('capturing') for e,p in records if not e['sample'] for v in [states[e['name']]]):
            raise RuntimeError('A helper enabled performance capture')
    write_json(output/'control.json',control)
    warm=time.monotonic()+args.warmup
    initial_epochs={e['name']:states[e['name']].get('epoch') for e,p in records if e['participating'] and e['name']!='server'}
    while time.monotonic()<warm:
        refresh();time.sleep(.25)
    verify_player_simulation(plan,states)
    trace_id=plan['run_id']
    publish(helper/'request.json',{'operation':'start','id':trace_id,'pids':[p.pid for e,p in endpoints]})
    def helper_result(suffix,timeout=20):
        until=time.monotonic()+timeout
        while time.monotonic()<until:
            for operation in ('start','stop'):
                error=helper/(trace_id+'-'+operation+'-error.json')
                if error.exists(): raise RuntimeError('ETW helper: '+str(read_json_shared(error)))
            path=helper/(trace_id+'-'+suffix+'.json')
            if path.exists(): return read_json_shared(path)
            time.sleep(.05)
        raise RuntimeError('ETW helper did not produce '+suffix)
    helper_result('ready')
    control['phase']='capture';write_json(output/'control.json',control)
    until=time.monotonic()+10
    while time.monotonic()<until:
        refresh()
        if all(states[e['name']].get('capturing') for e,p in endpoints): break
        time.sleep(.1)
    else: raise RuntimeError('Both measured endpoints did not arm capture')
    freq=frequency();begin=qpc()+2*freq;end=begin+round(args.capture*freq)
    window={'begin_qpc':begin,'end_qpc':end,'qpc_frequency':freq,'pids':[p.pid for e,p in endpoints]}
    write_json(output/'window.json',window)
    process_samples=[];state_samples=[];next_state=0
    try:
        while qpc()<=end+freq:
            stamp=qpc()
            for e,p in endpoints:
                before=qpc();sample=sample_process(p.pid);after=qpc()
                process_samples.append({'role':e['name'],'qpc':(before+after)//2,'read_begin_qpc':before,'read_end_qpc':after,**sample})
            if stamp>=next_state:
                refresh();next_state=stamp+freq
                if any(states[name].get('epoch')!=epoch for name,epoch in initial_epochs.items()):
                    raise RuntimeError('The match identity changed during the steady performance window')
                if any(states[e['name']].get('paused') for e,p in endpoints if e['rendered']):
                    raise RuntimeError('A membership pause contaminated steady sampling')
                state_samples.append({'qpc':stamp,'states':{e['name']:states[e['name']] for e,p in endpoints}})
            time.sleep(.05)
    finally:
        control['phase']='finish_capture';write_json(output/'control.json',control)
        publish(helper/'request.json',{'operation':'stop','id':trace_id,**window})
        write_json(output/'process-samples.json',{'window':window,'samples':process_samples})
        write_json(output/'state-samples.json',{'window':window,'samples':state_samples})
    etw=helper_result('report')
    if not etw.get('valid'): raise RuntimeError('ETW capture lost events or is incomplete')
    with gzip.open(helper/etw['event_file'],'rt',encoding='utf-8') as stream:
        events=[json.loads(line) for line in stream]
    if {event['pid'] for event in events}!={p.pid for e,p in endpoints}:
        raise RuntimeError('Actual network events are missing a measured PID')
    with gzip.open(output/'network-events.jsonl.gz','wt',encoding='utf-8') as stream:
        for event in events: stream.write(json.dumps(event)+'\n')
    write_json(output/'network-report.json',etw)
    native={}
    deadline=time.monotonic()+15
    for e,p in endpoints:
        path=Path(e['directory'])/'performance.json'
        while not path.exists() and time.monotonic()<deadline: time.sleep(.1)
        native[e['name']]=read_json_shared(path)
        value=native[e['name']]
        if value.get('failure') or value.get('capture_valid') is not True or round(value.get('qpc_frequency',0))!=freq:
            raise RuntimeError('Native metrics overflowed, failed or use an incompatible clock: '+e['name'])
    rendered=next(e['name'] for e,p in endpoints if e['rendered']);client=native[rendered]
    metrics={}
    for key,counter in client['counters'].items():
        samples=[s for s in counter.get('timed_frames',[]) if begin<=s['qpc']<end]
        if samples: metrics[key]=distribution([s['value'] for s in samples])
    for key in ('frame_wall_ms','gpu_frame_ms'):
        if key not in metrics or metrics[key]['mean']<=0: raise RuntimeError('Required frame/GPU data missing: '+key)
    frames=[s for s in client['counters']['frame_wall_ms']['timed_frames'] if begin<=s['qpc']<end]
    if any(b['frame']!=a['frame']+1 for a,b in zip(frames,frames[1:])):
        raise RuntimeError('Per-frame capture has missing engine frames')
    coordinator={}
    for sample in native['server']['timed_measurements']:
        if begin<=sample['qpc']<end: coordinator.setdefault(sample['metric'],[]).append(sample['value'])
    if not coordinator.get('service_poll'): raise RuntimeError('Missing coordinator polling metric')
    database=native['server'].get('metrics',{}).get('database_metadata_commit')
    if database is None: raise RuntimeError('Missing database instrumentation')
    # No room changes may legitimately mean no database commits in this window.
    # Report zero operations explicitly instead of inventing a latency sample.
    # Keep cumulative socket counters and ETW event bytes separately: UE's
    # WebSocket send count is queue submission and is not actual TCP delivery.
    queues=[peer['oldest_queue_ms'] for row in client['network_samples'] if begin<=row['qpc']<end
            for peer in row.get('hybrid_transport',{}).get('peers',[])]
    if not queues: raise RuntimeError('Peer queue ages missing in the shared QPC window')
    byte_groups={}
    for event in events:
        key=f"{event['pid']}/{event['protocol']}/{event['direction']}"
        byte_groups[key]=byte_groups.get(key,0)+event['size']
    summary={'window':window,'client_metrics':metrics,'coordinator_metrics':{k:distribution(v) for k,v in coordinator.items()},
        'database_metadata_operations':len(coordinator.get('database_metadata_commit',[])),
        'queue_age_ms':distribution(queues),'actual_network_bytes':byte_groups,
        'byte_basis':'Calibrated ETW payload fields per PID/protocol/direction. Cumulative socket and application counters retained separately.',
        'scope':'Only the assigned backend and rendered client on this single-machine CPU topology; no total six-client extrapolation'}
    write_json(output/'performance-summary.json',summary)
    return {'status':'passed','measurement_valid':True,'summary':str(output/'performance-summary.json')}
