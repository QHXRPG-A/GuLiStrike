"""Correlate native RPC/property bytes, CSV saturation and engine queue arithmetic."""
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path
import re
import sys

OUT=Path(__file__).parent
sys.path.insert(0,str(OUT.parents[2]/'Scripts'))
from analyze_commander_network_profile import read_profile

header,addresses,events,frames,tokens=read_profile(OUT/'loaded-network.nprof')
meta=json.loads((OUT/'loaded-capture.json').read_text())['meta']
first=meta['start_frame']
indices={t:first+i for i,t in enumerate(frames)}
assert len(frames)==meta['end_frame']-first+1
properties=defaultdict(Counter);rpcs=defaultdict(Counter);bytes_out=Counter();pose_calls=Counter()
for e in events:
    assert e['time'] is not None
    key=(indices[e['time']],e['connection'])
    if e['token']==5:properties[key][e['actor']+'.'+e['property']]+=e['bits']/8
    elif e['token']==3:
        rpcs[key][e['function']]+=(e['header_bits']+e['parameter_bits']+e['footer_bits'])/8
        if e['function']=='ClientReceiveEncodedPoseBlock':pose_calls[key]+=1
    elif e['token']==1:
        bytes_out[key]+=(e['packet_bits']+e['bunch_bits']+e['ack_bits']+8)//8+28

frame=first;last=first%1000;skips=defaultdict(Counter);skip_log=[]
for line in (OUT/'loaded-network.log').read_text().splitlines():
    match=re.match(r'\[([^]]+)\]\[\s*(\d+)\]',line)
    if not match:continue
    modulo=int(match[2]);frame+=(modulo-last)%1000;last=modulo
    if 'Network saturated, not calling' in line:
        client=int(re.search(r'Controller_(\d+)',line)[1])
        skips[frame][client]+=1
        skip_log.append(dict(frame=frame,client=client,text=line))
with (OUT/'loaded-engine.csv').open(encoding='utf-8-sig',newline='') as f:csv_rows=list(csv.reader(f))
columns=csv_rows[0]; csv_data=csv_rows[1:-2]
assert len(csv_data)==len(frames)
column=lambda name:columns.index('GuLiCommanderPoseDispatch/'+name)
csv_skips={first+i:int(float(row[column('SaturatedAtAttempt')])) for i,row in enumerate(csv_data) if float(row[column('SaturatedAtAttempt')])}
assert csv_skips=={f:sum(v.values()) for f,v in skips.items()}
attempts=sum(int(float(row[column('AttemptedChunks')])) for row in csv_data)
assert attempts==sum(pose_calls.values())+len(skip_log)

start=json.loads((OUT/'loaded-counters-start.json').read_text())[0]
end=json.loads((OUT/'loaded-counters-end.json').read_text())[0]
result=dict(start_frame=first,end_frame=meta['end_frame'],seconds=end['time']-start['time'],
    attempted_pose_blocks=attempts,sent_pose_blocks=sum(pose_calls.values()),
    skipped_pose_blocks=len(skip_log),engine_frames_with_skips=len(skips),
    csv_log_frames_match=True,ipv4_udp_overhead_bytes=28,
    credit_per_frame_bits=16666,maximum_accumulated_credit_bits=33332,connections=[],
    skipped_events=skip_log)
for i,(a,b) in enumerate(zip(start['connections'],end['connections'])):
    queue=a['queued_bits'];q={};total=0
    for f in range(first,meta['end_frame']+1):
        key=(f,i);before=queue
        total+=bytes_out[key]
        queue=max(-33332,queue+int(bytes_out[key])*8-16666)
        q[f]=dict(before_bits=before,end_bits=queue)
    assert total==b['out_total_bytes']-a['out_total_bytes']
    assert queue==b['queued_bits']
    failed=[]
    for f,counts in skips.items():
        if not counts[i]:continue
        key=(f-1,i)
        failed.append(dict(drop_frame=f,skipped=counts[i],previous_frame=f-1,
            previous_frame_bytes=bytes_out[key],previous_frame_pose_calls=pose_calls[key],
            previous_frame_properties=dict(properties[key].most_common()),
            previous_frame_rpc_bytes=dict(rpcs[key]),
            reconstructed_start_queued_bytes=q[f]['before_bits']/8,
            current_frame_sent_pose_blocks=pose_calls[(f,i)]))
    result['connections'].append(dict(connection=a['name'],address=addresses[i],
        total_budget_accounted_bytes=total,counter_bytes_match_exactly=True,
        reconstructed_end_queued_bits=queue,observed_end_queued_bits=b['queued_bits'],
        start_queued_bits=a['queued_bits'],queue_arithmetic_matches=True,
        network_packet_loss_delta=b['out_lost']-a['out_lost'],drop_frames=failed))
result['scope']=[
    'Actor/RPC/content/socket layers are nested; attribution does not sum their bytes twice.',
    'Queue reconstruction uses native packet sizes and UE source Tick formula. Only begin/end queue counters were directly read; intervening values are reconstructed.',
    'Full 630-frame native stream accounts for BOTH cumulative connection byte deltas exactly and reconstructs both final queued-bit counters exactly.',
    'The separate rate summary excludes the final frame to have a complete timestamp interval. Do not compare its 629-frame totals directly with the full-counter totals.',
    'All 47 logged skipped pose RPCs align exactly with the seven CSV saturation frames. Count of 8862 attempts equals 8815 sent blocks plus 47 skipped.',
    'This identifies the burst/drop path in the new PIE; it does not retroactively attribute the historical 470 ms sample gap.',
]
(OUT/'loaded-budget-proof.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k not in ('connections','skipped_events','scope')},indent=2))
print('All native-byte, queue and CSV/log alignment checks passed.')
