"""Read-only attribution of this existing PIE's native network capture."""
from collections import Counter, defaultdict
import csv
import hashlib
import json
from pathlib import Path
import re
import statistics
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).parent
sys.path.insert(0, str(ROOT / 'Scripts'))
from analyze_commander_network_profile import read_profile

def stats(values):
    values = sorted(values)
    def q(p):
        return values[round((len(values)-1)*p)] if values else None
    return dict(count=len(values), mean=statistics.mean(values) if values else None,
                p50=q(.5), p95=q(.95), p99=q(.99), maximum=max(values) if values else None)

def rpc_group(name):
    if name == 'ClientReceiveEncodedPoseBlock': return 'mass_pose'
    if name == 'MulticastGroundProjectileSnapshot': return 'ground_projectile_snapshots'
    if name in ('MulticastReliableStates', 'MulticastShots'): return 'combat_states_and_shots'
    if name == 'ServerAcknowledgePoseBlocks': return 'pose_ack'
    return 'other_rpc'

def prop_group(actor):
    if actor == 'GuLiPlacedBuilding': return 'building_properties'
    if actor == 'GuLiSoldierStateReplicator': return 'soldier_roster'
    return 'other_properties'

header, addresses, events, frames, tokens = read_profile(OUT / 'breakdown-network.nprof')
capture = json.loads((OUT / 'breakdown-capture.json').read_text())
start_frame = capture['meta']['start_frame']
assert frames == sorted(set(frames))
assert len(frames) == capture['meta']['end_frame'] - start_frame + 1
frame_number = {t:start_frame+i for i,t in enumerate(frames)}
# Last frame is terminated part way through by the Slate callback. Exclude it.
begin, end = frames[0], frames[-1]
duration = end-begin
connection_data = [dict(groups=Counter(), rpcs=defaultdict(Counter), properties=defaultdict(Counter),
                        by_frame=defaultdict(Counter), initial_flags=Counter(),
                        socket=0, accounted=0, packets=0) for _ in addresses]
for event in events:
    if event['time'] is None or not begin <= event['time'] < end:
        continue
    c = connection_data[event['connection']]
    f = c['by_frame'][frame_number[event['time']]]
    t = event['token']
    if t == 1:
        # IPv4/UDP: engine IpConnection defaults to UDP_HEADER_SIZE (28).
        # Cross-check this reconstruction with both connection cumulative counters below.
        wire_bytes = event['bytes'] + 28
        budget_bytes = (event['packet_bits'] + event['bunch_bits'] + event['ack_bits'] + 8)//8 + 28
        c['socket'] += wire_bytes
        c['accounted'] += budget_bytes
        c['packets'] += 1
        f['wire_bytes'] += wire_bytes
        f['accounted_bytes'] += budget_bytes
        f['packets'] += 1
    elif t == 3:
        name = event['function']
        bits = event['header_bits'] + event['parameter_bits'] + event['footer_bits']
        group = rpc_group(name)
        c['groups'][group] += bits
        c['rpcs'][name].update(bits=bits, calls=1, parameter_bits=event['parameter_bits'])
        f[group] += bits/8
        f[name+'_calls'] += 1
    elif t == 5:
        group = prop_group(event['actor'])
        c['groups'][group] += event['bits']
        c['properties'][event['actor']+'.'+event['property']].update(bits=event['bits'], calls=1)
        f[group] += event['bits']/8
    elif t == 4:
        c['initial_flags'][event['actor']+'/'+str(event['flags'])] += 1

log = (OUT/'breakdown-network.log').read_text()
drops = Counter(re.findall(r'Network saturated, not calling (\S+)',log))
rows = []
report = dict(header=header, engine_seconds=[begin,end], seconds=duration, frames=len(frames)-1,
              fps=(len(frames)-1)/duration, native_token_counts=tokens, skipped_rpcs=dict(drops),
              connection_budget_bytes_s=250000, runtime_max_net_tick_rate=120,
              inferred_credit_per_frame_bytes=16666/8, connections=[])
for index, (address, data) in enumerate(zip(addresses,connection_data)):
    accounted = data['accounted']
    categories = dict(data['groups'])
    categories['protocol_and_unattributed'] = accounted*8-sum(categories.values())
    assert categories['protocol_and_unattributed']>=0
    def details(values):
        return [dict(name=k, calls=v['calls'], bytes=v['bits']/8, bytes_s=v['bits']/8/duration,
                     share_percent=v['bits']/8/accounted*100)
                for k,v in sorted(values.items(),key=lambda kv:-kv[1]['bits'])]
    fs = []
    for frame in range(start_frame, capture['meta']['end_frame']):
        f = data['by_frame'][frame]
        row = dict(frame=frame, connection=index, engine_seconds=frames[frame-start_frame], **f)
        rows.append(row)
        fs.append(f)
    largest = sorted((r for r in rows if r['connection']==index),key=lambda r:-r.get('accounted_bytes',0))[:10]
    presence = Counter()
    for f in fs:
        pose=bool(f['mass_pose']); projectile=bool(f['ground_projectile_snapshots'])
        building=bool(f['building_properties']); combat=bool(f['combat_states_and_shots'])
        presence.update(pose=pose, projectile=projectile, building=building, combat=combat,
            pose_and_projectile=pose and projectile, pose_and_building=pose and building,
            pose_and_combat=pose and combat, all_four=pose and projectile and building and combat)
    report['connections'].append(dict(id=index,address=address,packets=data['packets'],
        budget_accounted_bytes=accounted,bytes_s=accounted/duration,
        socket_plus_ipv4_udp_bytes=data['socket'],wire_bytes_s=data['socket']/duration,
        categories=[dict(name=k,bytes=v/8,bytes_s=v/8/duration,share_percent=v/8/accounted*100)
                    for k,v in sorted(categories.items(),key=lambda kv:-kv[1])],
        rpcs=details(data['rpcs']),properties=details(data['properties']),
        frame_bytes=stats([f['accounted_bytes'] for f in fs]),
        frame_overlap=dict(presence), largest_frames=largest,
        initial_flag_counts=dict(data['initial_flags']),
        frames_above_one_tick_credit=sum(f['accounted_bytes']>16666/8 for f in fs)))

counter_start=json.loads((OUT/'breakdown-counters-start.json').read_text())
counter_end=json.loads((OUT/'breakdown-counters-end.json').read_text())
counter_duration=counter_end[0]['time']-counter_start[0]['time']
report['cumulative_cross_check']=dict(seconds=counter_duration,connections=[])
for s,e in zip(counter_start[0]['connections'],counter_end[0]['connections']):
    report['cumulative_cross_check']['connections'].append(dict(name=s['name'],
        bytes=e['out_total_bytes']-s['out_total_bytes'],
        bytes_s=(e['out_total_bytes']-s['out_total_bytes'])/counter_duration,
        queued_bits_start=s['queued_bits'],queued_bits_end=e['queued_bits'],
        packet_loss_delta=e['out_lost']-s['out_lost']))
report['notes']=[
    'Passive capture of existing PIE; no movement, selections, actor changes, network emulation, bandwidth setting change or restart.',
    'Profile is process-wide. Destination 17777 combines both client uplinks; destinations 64714/64715 are independent server downlinks.',
    'RPC totals include content/field headers. Property totals include serialized values. Container/socket layers are not added again.',
    'IPv4/UDP overhead 28 bytes derives from engine IpConnection default. Cumulative counters are retained for near-window cross-check.',
    'Only first 1240 complete native frames are analyzed; last partial frame excluded. Frame-index mapping checked against begin/end GFrameCounter.',
    'Replication attribute totals measure serialization; retransmissions/unattributed protocol remain residual. Per-frame RPC serialization and socket flush can have boundary skew.',
    'This window has no saturation. Following read-only census at 14:48:53: alive=177 active=0; do not claim these were the counts for the entire earlier window.',
    '120 MaxNetTickRate and queued_bits lower cap -33332 support 16666 bits of credit per tick when time is clamped to 1/120 second. This is a source/runtime inference, not a direct DesiredTickRate instrument.',
]
report['sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (
    OUT/'breakdown-network.nprof',OUT/'breakdown-network.log',OUT/'breakdown-engine.csv')}
(OUT/'breakdown-analysis.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
columns=['frame','connection','engine_seconds']+sorted(set().union(*(r.keys() for r in rows))-{'frame','connection','engine_seconds'})
with (OUT/'breakdown-frames.csv').open('w',encoding='utf-8',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=columns);writer.writeheader();writer.writerows(rows)
print(json.dumps({k:v for k,v in report.items() if k not in ('connections','sha256','notes')},indent=2))
for c in report['connections']:
    print(json.dumps({k:c[k] for k in ('id','address','bytes_s','categories','frame_bytes','frame_overlap','frames_above_one_tick_credit')},indent=2))
