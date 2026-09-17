"""Compare the authorized 1,200-unit pose runs on a shared QPC window."""
from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from pathlib import Path
from statistics import median

from analyze_commander_move_stress import stats, summarize
from analyze_commander_network_profile import read_profile

ROLES = ('server', 'client1', 'client2')


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


def numeric_rows(path):
    return [{k: float(v) for k, v in r.items()} for r in rows(path)]


def near(data, key, time):
    return min(data, key=lambda r: abs(r[key] - time))


def trace_statistics(directory):
    out = directory / 'insights'
    if not (out / 'events-project.csv').exists():
        return None
    begin, end = read_json(out / 'clock.json')['trace_window_seconds']
    threads = rows(out / 'threads.csv')
    game_ids = {r['Id'] for r in threads if r['Name'] == 'GameThread'}
    events = [r for r in rows(out / 'events-project.csv') if r['ThreadId'] in game_ids
              and begin <= float(r['StartTime']) and float(r['EndTime']) <= end]
    by_name = {}
    for event in events:
        by_name.setdefault(event['TimerName'], []).append(event)
    def durations(name):
        return [float(r['Duration']) * 1000 for r in by_name.get(name, [])]
    ticks = by_name.get('FEngineLoop::Tick', [])
    limits = by_name.get('FEngineLoop_UpdateTimeAndHandleMaxTickRate', [])
    active = []
    limit_index = 0
    for tick in ticks:
        start, finish = float(tick['StartTime']), float(tick['EndTime'])
        while limit_index < len(limits) and float(limits[limit_index]['StartTime']) < start:
            limit_index += 1
        wait = 0
        if limit_index < len(limits) and float(limits[limit_index]['EndTime']) <= finish:
            wait = float(limits[limit_index]['Duration'])
        active.append(max(0, float(tick['Duration']) - wait) * 1000)
    # UE's exporter also includes GPU rows under a CPU thread filter. Remove identical GPU-only rows.
    gpu = Counter(tuple(sorted(r.items())) for r in rows(out / 'timers-gpu.csv'))
    cpu = []
    for r in rows(out / 'timers-gamethread.csv'):
        key = tuple(sorted(r.items()))
        if gpu[key]: gpu[key] -= 1
        else: cpu.append(r)
    project = [r for r in cpu if 'GuLi' in r['Name']]
    project.sort(key=lambda r: float(r['Excl']), reverse=True)
    pose_scopes = {}
    for name in ('GuLiPose_Send', 'GuLiPose_Receive', 'GuLiPose_Ack', 'GuLiPose_Capture', 'GuLiPose_Wire',
                 'GuLiPose_Encode', 'GuLiPose_Decode', 'GuLiPose_Confirm'):
        values = durations(name)
        pose_scopes[name] = {**stats(values), 'ms_per_second': sum(values) / (end - begin)}
    return {
        'window_seconds': [begin, end], 'game_thread_active_ms': stats(active),
        'engine_tick_including_wait_ms': stats(durations('FEngineLoop::Tick')),
        'limiter_wait_ms': stats(durations('FEngineLoop_UpdateTimeAndHandleMaxTickRate')),
        'world_tick_ms': stats(durations('UWorld_Tick')),
        'pose_scopes_ms': pose_scopes,
        'project_exclusive_top': [{**r, 'exclusive_ms_per_second': float(r['Excl']) * 1000 / (end - begin)} for r in project[:12]],
        'definition': 'GameThread active wall time = FEngineLoop::Tick minus its frame-rate-limiter scope; other waits remain. CPU utilization is measured separately from process CPU time.',
    }


def outbound(directory, summary, begin, end):
    _, addresses, events, _, _ = read_profile(directory / 'network.nprof')
    offset = summary['capture_start_monotonic_seconds'] - summary['capture_start_engine_seconds']
    selected = [e for e in events if e['time'] is not None and begin <= e['time'] + offset < end]
    packets = [e for e in selected if e['token'] == 1]
    rpcs = {}
    for e in selected:
        if e['token'] == 3:
            r = rpcs.setdefault(e['function'], {'calls': 0, 'bits': 0, 'parameter_bits': 0})
            r['calls'] += 1
            r['bits'] += e['header_bits'] + e['parameter_bits'] + e['footer_bits']
            r['parameter_bits'] += e['parameter_bits']
    for r in rpcs.values():
        r['bytes_per_second'] = r['bits'] / 8 / (end - begin)
        r['parameter_bytes_per_second'] = r['parameter_bits'] / 8 / (end - begin)
    # PacketOverhead is recorded from each real connection, not assumed from profiler file size.
    overhead = {r['connection']: int(r['packet_overhead']) for r in rows(directory / 'network.csv')}
    return {
        'packet_count': len(packets), 'packets_per_second': len(packets) / (end - begin),
        'socket_plus_ip_udp_bytes': sum(e['bytes'] + overhead[addresses[e['connection']]] for e in packets),
        'connection_accounted_bytes': sum((e['packet_bits'] + e['bunch_bits'] + e['ack_bits'] + 8) // 8
                                          + overhead[addresses[e['connection']]] for e in packets),
        'rpcs': rpcs,
    }


def analyze_run(run):
    summaries = {role: read_json(run / role / 'summary.json') for role in ROLES}
    assert all(not s['error'] for s in summaries.values()), run
    begin = max(s['capture_start_monotonic_seconds'] for s in summaries.values()) + 5
    end = begin + 30
    assert end <= min(s['capture_start_monotonic_seconds'] + s['measured_seconds'] for s in summaries.values())
    process_rows = rows(run / 'processes.csv')
    result = {'run': run.name, 'window_monotonic_seconds': [begin, end], 'seconds': end - begin, 'roles': {}}
    for role, s in summaries.items():
        directory = run / role
        frame_rows = numeric_rows(directory / 'frames.csv')
        selected = [r for r in frame_rows if begin <= s['capture_start_monotonic_seconds'] + r['seconds'] < end]
        fixed = [r['sim_ms'] / r['sim_steps'] for r in selected if r['sim_steps'] > 0]
        samples = numeric_rows(directory / 'pose-metrics.csv')
        a, b = near(samples, 'monotonic_seconds', begin), near(samples, 'monotonic_seconds', end)
        duration = b['monotonic_seconds'] - a['monotonic_seconds']
        selected_samples = [r for r in samples if begin <= r['monotonic_seconds'] < end]
        metric_deltas = []
        for prev, cur in zip(samples, samples[1:]):
            if begin <= prev['monotonic_seconds'] and cur['monotonic_seconds'] < end:
                metric_deltas.append({k: cur[k] - prev[k] for k in ('send_ms', 'receive_ms', 'ack_ms', 'capture_ms', 'wire_read_ms')})
        timing = {}
        for op in ('send', 'receive', 'ack', 'capture', 'wire_read'):
            calls = b[op + '_calls'] - a[op + '_calls']
            milliseconds = b[op + '_ms'] - a[op + '_ms']
            timing[op] = {'calls': calls, 'total_ms': milliseconds, 'ms_per_second': milliseconds / duration,
                          'mean_ms_per_call': milliseconds / calls if calls else 0,
                          'ms_per_world_frame': stats([r[op + '_ms'] for r in metric_deltas])}
        # WindowsPlatformTime::Seconds is QPC / frequency + 2^24. The two capture markers
        # are sampled at slightly different instants, so do not use their difference as a clock offset.
        qpc_offset = 16777216.0
        proc = [r for r in process_rows if r['Role'] == role and begin <= float(r['QpcSeconds']) + qpc_offset < end]
        connections = {}
        for r in rows(directory / 'network.csv'):
            connections.setdefault(r['connection'], []).append({k: float(v) for k, v in r.items() if k != 'connection'})
        network = {'in_bytes_per_second': 0, 'out_bytes_per_second': 0, 'connections': []}
        for name, data in connections.items():
            na, nb = near(data, 'monotonic_seconds', begin), near(data, 'monotonic_seconds', end)
            dt = nb['monotonic_seconds'] - na['monotonic_seconds']
            entry = {'connection': name, 'seconds': dt, 'boundary_skew_ms': [(na['monotonic_seconds'] - begin) * 1000,
                                                                          (nb['monotonic_seconds'] - end) * 1000]}
            for direction in ('in', 'out'):
                value = (nb[direction + '_total_bytes'] - na[direction + '_total_bytes']) / dt
                entry[direction + '_bytes_per_second'] = value
                network[direction + '_bytes_per_second'] += value
            network['connections'].append(entry)
        full = summarize(directory)
        profile = outbound(directory, s, begin, end)
        valid = (stats([r['observed_moving'] for r in selected])['p50'] == 1200
                 and min(r['alive'] for r in selected) == 1200
                 and stats([r['observed_moving'] for r in selected])['p95'] == 1200)
        # The lower-tail moving count, not the requested count, establishes the workload.
        moving = sorted(r['observed_moving'] for r in selected)
        moving_p05 = moving[max(0, int(len(moving) * .05) - 1)]
        valid = valid and moving_p05 >= 1140
        gaps = None
        if 'sample_gap_count' in b:
            count = int(b['sample_gap_count'] - a['sample_gap_count'])
            histogram = [int(b[f'gap_{i}'] - a[f'gap_{i}']) for i in range(1, 22)]
            def percentile_gap(percent):
                running = 0
                for i, n in enumerate(histogram, 1):
                    running += n
                    if count and running >= count * percent: return i * 100 if i <= 20 else None
                return None
            gaps = {'count': count, 'histogram_steps_1_to_20_then_overflow': histogram,
                    'mean_ms': (b['sample_gap_ticks'] - a['sample_gap_ticks']) / count * 100 if count else None,
                    'p50_ms': percentile_gap(.5), 'p95_ms': percentile_gap(.95)}
        result['roles'][role] = {
            'actual_moving_p05': moving_p05, 'valid_load': valid,
            'actual_moving': {**stats(moving), 'min': min(moving)},
            'effective_sim_hz': sum(r['sim_steps'] for r in selected) / (end - begin) if role == 'server' else None,
            'full_capture_sim_hz': full.get('effective_sim_hz'), 'dropped_steps': s.get('dropped_steps'),
            'accepted_pose_hz': full.get('accepted_pose_hz'),
            'decoded_samples_per_second': (b['decoded_samples'] - a['decoded_samples']) / duration,
            'mean_unit_pose_hz': (b['decoded_samples'] - a['decoded_samples']) / duration / 1200,
            'sample_intervals': gaps,
            'decode_failures': b['decode_failures'] - a['decode_failures'],
            'absolute_samples': b['absolute_samples'] - a['absolute_samples'],
            'delta_samples': b['delta_samples'] - a['delta_samples'],
            'frame_ms': stats([r['frame_ms'] for r in selected]), 'fixed_step_ms': stats(fixed),
            'pose_age_ms': stats([r['pose_age_ms'] for r in selected]),
            'timing': timing, 'network': network, 'outbound_profile': profile,
            'cpu_core_equivalents': stats([float(r['CpuCoreEquivalents']) for r in proc]),
            'working_set_mib': stats([float(r['WorkingSetMiB']) for r in proc]),
            'private_commit_mib': stats([float(r['PrivateMiB']) for r in proc]),
            'codec_cache_bytes': stats([r['cache_bytes'] for r in selected_samples]),
            'codec_cache_capture_peak_bytes': samples[-1]['peak_cache_bytes'],
            'camera_stationary': full.get('camera_stationary_in_capture'),
            'full_capture_engine': full['engine'],
            'trace': str(directory / 'process.utrace'),
            'trace_cpu': trace_statistics(directory),
        }
    result['valid_comparison_load'] = (all(r['valid_load'] for r in result['roles'].values())
                                       and result['roles']['server']['effective_sim_hz'] >= 9.9
                                       and result['roles']['server']['dropped_steps'] == 0
                                       and all(r['decode_failures'] == 0 for r in result['roles'].values())
                                       and all(result['roles'][r]['camera_stationary'] for r in ('client1', 'client2')))
    (run / 'comparison-window.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    return result


def comparison(base, optimized):
    assert len(base) == len(optimized) == 3
    result = {'baseline_runs': [r['run'] for r in base], 'optimized_runs': [r['run'] for r in optimized], 'roles': {}}
    fields = {
        'incoming_Bps': ('network', 'in_bytes_per_second'),
        'outgoing_Bps': ('network', 'out_bytes_per_second'),
        'cpu_mean_cores': ('cpu_core_equivalents', 'mean'), 'cpu_p95_cores': ('cpu_core_equivalents', 'p95'),
        'working_set_mean_MiB': ('working_set_mib', 'mean'), 'working_set_peak_MiB': ('working_set_mib', 'max'),
        'working_set_p95_MiB': ('working_set_mib', 'p95'),
        'private_mean_MiB': ('private_commit_mib', 'mean'), 'private_peak_MiB': ('private_commit_mib', 'max'),
        'private_p95_MiB': ('private_commit_mib', 'p95'),
        'world_frame_p95_ms': ('frame_ms', 'p95'), 'codec_cache_peak_bytes': ('codec_cache_bytes', 'max'),
    }
    for role in ROLES:
        output = {}
        for name, path in fields.items():
            def values(runs):
                return [r['roles'][role][path[0]][path[1]] for r in runs]
            a, b = median(values(base)), median(values(optimized))
            output[name] = {'baseline': a, 'optimized': b, 'difference': b - a,
                            'change_percent': (b / a - 1) * 100 if a else None,
                            'baseline_rounds': values(base), 'optimized_rounds': values(optimized)}
        result['roles'][role] = output
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--run', help='Analyze one completed run rather than all six.')
    args = parser.parse_args()
    if args.run:
        r = analyze_run(args.root / args.run)
        print(json.dumps({'run': r['run'], 'valid': r['valid_comparison_load'],
                          'down_Bps': r['roles']['client1']['network']['in_bytes_per_second']}, ensure_ascii=False))
        return
    base = [analyze_run(args.root / f'baseline-complete-{i}-n1200-c2') for i in range(1, 4)]
    optimized = [analyze_run(args.root / f'optimized-{i}-n1200-c2') for i in range(1, 4)]
    result = comparison(base, optimized)
    result['valid_comparison_load'] = all(r['valid_comparison_load'] for r in base + optimized)
    result['runs'] = base + optimized
    result['notes'] = ['Three-run medians; individual rounds retained.',
                       'CPU and process memory use 1-second QPC-aligned samples; buffers use per-world-frame samples.',
                       'Engine CSV counters marked full_capture cover the full capture, not the common 30-second window.',
                       'RPC attribution contains RPC headers but excludes enclosing IP/UDP and bunch containers.',
                       'Total benefits combine quantization changes and differential compression. Same-host relative comparison only.']
    (args.root / 'comparison.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'valid': result['valid_comparison_load'], 'roles': result['roles']}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
