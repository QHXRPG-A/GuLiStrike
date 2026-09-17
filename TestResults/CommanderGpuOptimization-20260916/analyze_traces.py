"""Summarize native GPU queues separately from CPU waits in each common 30s window."""
import csv
import json
import sys
from collections import defaultdict
from pathlib import Path
from statistics import median

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parents[1] / 'Scripts'))
from analyze_commander_move_stress import stats


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


runs = []
for run in sorted(ROOT.glob('[ABC]-*-n1200-c*')):
    for role in ['client1', 'client2']:
        out = run / role / 'insights'
        if not (out / 'gpu-events.csv').exists(): continue
        threads = {r['Id']: r['Name'] for r in rows(out / 'threads.csv')}
        start, end = read(out / 'clock.json')['trace_window_seconds']
        scopes = defaultdict(list)
        for file in ['gpu-events.csv', 'wait-events.csv']:
            for event in rows(out / file):
                thread = threads.get(event['ThreadId'], event['ThreadId'])
                if file == 'gpu-events.csv' and not thread.startswith('GPU0-'): continue
                if float(event['StartTime']) < start or float(event['EndTime']) > end: continue
                scopes[thread + '/' + event['TimerName']].append(float(event['Duration']) * 1000)
        item = {'run': run.name, 'profile': run.name[0], 'clients': int(run.name[-1]), 'role': role,
                'scopes_ms': {key: stats(values) for key, values in scopes.items()}}
        runs.append(item)
groups = defaultdict(list)
for run in runs: groups[f"{run['profile']}-{run['clients']}c-{run['role']}"] .append(run)
summary = {}
for key, entries in groups.items():
    names = sorted(set().union(*(r['scopes_ms'] for r in entries)))
    summary[key] = {'rounds': len(entries), 'scopes_ms': {name: {
        'rounds_present': sum(name in r['scopes_ms'] for r in entries),
        'median_event_mean_ms': median(r['scopes_ms'][name]['mean'] for r in entries if name in r['scopes_ms']),
        'count_range': [min(r['scopes_ms'].get(name, {}).get('n', 0) for r in entries), max(r['scopes_ms'].get(name, {}).get('n', 0) for r in entries)]}
        for name in names}}
checks = {'optimized_no_tsr': all(not any('TemporalSuperResolution' in k for k in r['scopes_ms']) for r in runs if r['profile'] in 'BC'),
          'optimized_no_volumetric_fog': all(not any('VolumetricFog' in k for k in r['scopes_ms']) for r in runs if r['profile'] in 'BC'),
          'C_no_CAS': all(not any('ContrastAdaptiveShading' in k for k in r['scopes_ms']) for r in runs if r['profile'] == 'C')}
report = {'scope': 'Native GPU0 queues and CPU waits in the common 30s window, only complete events. Spans overlap; never add queue durations.',
          'groups': summary, 'runs': runs, 'event_checks': checks,
          'native_frame_warning': 'Frame scope includes queue idle and is not the CSV GPUTime; VRS decisions use aligned CSV GPUTime.'}
(ROOT / 'trace-comparison.json').write_text(json.dumps(report, indent=2))
print(json.dumps({k: {n: v['median_event_mean_ms'] for n, v in row['scopes_ms'].items() if n.endswith('/Frame') or any(s in n for s in ['ContrastAdaptiveShading','LumenScreenProbeGather','WaitForVisibilityTasks'])} for k, row in summary.items()}, indent=2))
print(json.dumps({'event_checks': checks}, indent=2))
