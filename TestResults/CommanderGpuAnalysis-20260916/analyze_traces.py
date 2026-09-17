"""Distinguish CPU waits and each GPU queue; do not sum parallel queue scopes."""
import csv
import json
import sys
from bisect import bisect_left
from collections import defaultdict
from pathlib import Path

OUT = Path(__file__).resolve().parent
BASELINE = OUT.parent / 'CommanderClientCpu-Retest-20260916'
sys.path.insert(0, str(OUT.parents[1] / 'Scripts'))
from analyze_commander_move_stress import stats


def read_rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


def interval(row):
    return float(row['StartTime']), float(row['EndTime'])


def merge_intervals(values):
    merged = []
    for begin, end in sorted(values):
        if merged and begin <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((begin, end))
    return merged


result = {'scope': 'first single-client and first dual-client client1 traces, original common 30 s windows',
          'caveats': ['These are GPU event elapsed spans, not isolated shader busy time.',
                      'Graphics and compute queues overlap and include scheduling/dependency effects.',
                      'Per-call means from these two traces are not the nine-run CSV medians.'], 'runs': []}
for run in ('retest-1-n1200-c1', 'retest-1-n1200-c2'):
    source = BASELINE / run / 'client1/insights'
    threads = {r['Id']: r for r in read_rows(source / 'threads.csv')}
    begin, end = json.loads((source / 'clock.json').read_text(encoding='utf-8'))['trace_window_seconds']
    rows = read_rows(OUT / run / 'wait-events.csv')
    cpu = [r for r in rows if threads.get(r['ThreadId'], {}).get('Group') != 'GPU']
    visibility = [r for r in cpu if r['TimerName'] == 'WaitForVisibilityTasks']
    query = [r for r in cpu if r['TimerName'] == 'GPUBound_WaitingForGPUForOcclusionQueries_SeeGPUTrack']
    assert visibility and query
    query_intervals = merge_intervals((max(begin, a), min(end, b)) for a, b in map(interval, query) if b > begin and a < end)
    starts = [a for a, _ in query_intervals]
    overlap, visible_total = 0.0, 0.0
    for row in visibility:
        a, b = interval(row)
        a, b = max(a, begin), min(b, end)
        visible_total += b - a
        index = max(0, bisect_left(starts, a) - 1)
        while index < len(query_intervals) and query_intervals[index][0] < b:
            qa, qb = query_intervals[index]
            overlap += max(0.0, min(b, qb) - max(a, qa))
            index += 1
    gpu = defaultdict(list)
    timer_ids = defaultdict(set)
    for row in read_rows(OUT / run / 'gpu-events.csv'):
        thread = threads.get(row['ThreadId'], {})
        if thread.get('Group') == 'GPU':
            key = (thread['Name'], row['TimerName'])
            gpu[key].append(float(row['Duration']) * 1000)
            timer_ids[key].add(row['TimerId'])
    detail = read_rows(OUT / run / 'detail-events.csv')
    examples = [r for r in detail if r['TimerName'] == 'GPUBound_WaitingForGPUForOcclusionQueries_SeeGPUTrack'
                and threads.get(r['ThreadId'], {}).get('Group') != 'GPU']
    example = examples[len(examples) // 2]
    a, b = interval(example)
    children = [{'name': r['TimerName'], 'depth': int(r['Depth']), 'duration_ms': float(r['Duration']) * 1000}
                for r in detail if r['ThreadId'] == example['ThreadId'] and a <= float(r['StartTime']) and float(r['EndTime']) <= b]
    item = {'run': run, 'trace_window_seconds': [begin, end],
            'visibility_wait_ms_per_call': stats([float(r['Duration']) * 1000 for r in visibility]),
            'gpu_occlusion_query_wait_ms_per_call': stats([float(r['Duration']) * 1000 for r in query]),
            'visibility_wait_time_overlapping_gpu_query_wait_fraction': overlap / visible_total,
            'example_query_wait': {'thread': threads[example['ThreadId']]['Name'], 'start': a, 'end': b, 'nested_scopes': children},
            'gpu_queue_scopes': [{'queue': k[0], 'scope': k[1], 'timer_ids': sorted(timer_ids[k]),
                                   'elapsed_ms_per_call': stats(v)} for k, v in sorted(gpu.items())]}
    result['runs'].append(item)
    print(json.dumps({'run': run, 'visibility_wait': item['visibility_wait_ms_per_call'],
                       'query_wait': item['gpu_occlusion_query_wait_ms_per_call'], 'overlap_fraction': overlap / visible_total,
                       'async_scopes': [x for x in item['gpu_queue_scopes'] if x['queue'] == 'GPU0-Compute0'
                                        and x['scope'] in ('LumenScreenProbeGather', 'ContrastAdaptiveShading')]}, indent=2))
(OUT / 'trace-findings.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
