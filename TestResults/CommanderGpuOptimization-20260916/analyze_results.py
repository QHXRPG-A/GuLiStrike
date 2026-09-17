"""Analyze only the approved A/B/C captures; reuse existing load/frame gates."""
import csv
import json
import sys
from collections import defaultdict
from pathlib import Path
from statistics import median

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parents[1] / 'Scripts'))
from analyze_commander_move_stress import percentile, stats, read_engine


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


def engine_rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        data = list(csv.reader(stream))
    header = next(r for r in reversed(data) if r and r[0] == 'EVENTS')
    index = header.index('FrameTime')
    result = []
    for row in data:
        try: float(row[index])
        except (ValueError, IndexError): continue
        result.append(dict(zip(header, row)))
    return result


results, servers = [], []
for run in sorted(ROOT.glob('[ABC]-*-n1200-c*')):
    roles = ['server', 'client1'] + (['client2'] if run.name.endswith('c2') else [])
    if not all((run / role / 'summary.json').exists() for role in roles):
        continue
    summaries = {role: read(run / role / 'summary.json') for role in roles}
    assert all(not s['error'] for s in summaries.values()), run
    begin = max(s['capture_start_monotonic_seconds'] for s in summaries.values()) + 5
    end = begin + 30
    assert all(s['capture_start_monotonic_seconds'] + s['measured_seconds'] >= end for s in summaries.values())
    (run / 'comparison-window.json').write_text(json.dumps({'window_monotonic_seconds': [begin, end], 'roles': roles}, indent=2))
    for role, summary in summaries.items():
        all_frames = rows(run / role / 'frames.csv')
        indices = [i for i, r in enumerate(all_frames)
                   if begin <= summary['capture_start_monotonic_seconds'] + float(r['seconds']) < end]
        selected = [all_frames[i] for i in indices]
        moving = [float(r['observed_moving']) for r in selected]
        frame = stats([float(r['frame_ms']) for r in selected])
        item = {'profile': run.name[0], 'run': run.name, 'clients': int(run.name[-1]), 'role': role,
                'frame_ms_30s': frame, 'moving_p05': percentile(moving, .05),
                'alive_min': min(int(r['alive']) for r in selected),
                'load_pass': all(int(r['alive']) == 1200 for r in selected) and percentile(moving, .05) >= 1140}
        if role == 'server':
            item['dropped_steps'] = summary['dropped_steps']
            servers.append(item)
            continue
        item['frame_budget_pass'] = frame['p95'] <= (16.67 if item['clients'] == 1 else 33.33)
        engine = read_engine(run / role / 'engine.csv')
        item['engine_full_capture'] = {k: v for k, v in engine.items()
            if k.startswith(('GPU/', 'Exclusive/RenderThread/', 'RenderThreadIdle/'))
            or k in ('GPUTime', 'FrameTime', 'GameThreadTime', 'RenderThreadTime', 'RHIThreadTime')}
        raw = engine_rows(run / role / 'engine.csv')
        assert len(raw) == len(all_frames), (run.name, role, len(raw), len(all_frames))
        # Both files start in the same MoveStress capture tick and record one row per frame.
        # FrameTime and ticker frame_ms use different within-frame boundaries. Check alignment.
        errors = {str(offset): median(abs(float(raw[i]['FrameTime']) - float(all_frames[i + offset]['frame_ms']))
            for i in indices if 0 <= i + offset < len(all_frames)) for offset in range(-2, 3)}
        assert errors['0'] == min(errors.values()), (run.name, role, errors)
        item['csv_alignment'] = {'engine_rows': len(raw), 'ticker_rows': len(all_frames), 'median_abs_frame_difference_ms_by_offset': errors}
        item['engine_common_30s'] = {k: stats([float(raw[i][k]) for i in indices]) for k in item['engine_full_capture']}
        results.append(item)

groups = defaultdict(list)
for row in results:
    groups[f"{row['profile']}-{row['clients']}c-{row['role']}"] .append(row)
grouped = {}
for group, data in groups.items():
    keys = sorted(set().union(*(r['engine_full_capture'].keys() for r in data)))
    grouped[group] = {
        'rounds': len(data),
        'frame_p95_ms_30s': median(r['frame_ms_30s']['p95'] for r in data),
        'frame_mean_ms_30s': median(r['frame_ms_30s']['mean'] for r in data),
        'gpu_mean_ms_full': median(r['engine_full_capture']['GPUTime']['mean'] for r in data),
        'gpu_mean_ms_30s': median(r['engine_common_30s']['GPUTime']['mean'] for r in data),
        'moving_p05_min': min(r['moving_p05'] for r in data),
        'all_load_and_budget_pass': all(r['load_pass'] and r['frame_budget_pass'] for r in data),
        'median_engine_means': {k: median(r['engine_full_capture'][k]['mean'] for r in data if k in r['engine_full_capture']) for k in keys},
        'median_engine_means_30s': {k: median(r['engine_common_30s'][k]['mean'] for r in data if k in r['engine_common_30s']) for k in keys}}
decision = []
for suffix in ['1c-client1', '2c-client1', '2c-client2']:
    if not all(f'{p}-{suffix}' in grouped and grouped[f'{p}-{suffix}']['rounds'] == 3 for p in 'ABC'):
        continue
    b, c = (grouped[f'{p}-{suffix}'] for p in 'BC')
    improvement = (1 - c['gpu_mean_ms_30s'] / b['gpu_mean_ms_30s']) * 100
    regression = (c['frame_p95_ms_30s'] / b['frame_p95_ms_30s'] - 1) * 100
    decision.append({'group': suffix, 'gpu_improvement_percent_common_30s': improvement,
                     'gpu_improvement_percent_full_csv': (1 - c['gpu_mean_ms_full'] / b['gpu_mean_ms_full']) * 100,
                     'p95_regression_percent_30s': regression, 'pass': improvement >= 3 and regression <= 3})
report = {'scope': 'Common 30s: GPU/CPU CSV rows aligned with ticker frames, equal counts and zero-offset minimum frame-time error verified. Full ~40s also retained. Native GPU Frame trace spans include idle and are not GPUTime.',
          'groups': grouped, 'runs': results, 'servers': servers, 'vrs_decision': decision,
          'vrs_off_selected': len(decision) == 3 and all(x['pass'] for x in decision),
          'gates': {'complete_27_clients': len(results) == 27,
                    'all_load_pass': all(r['load_pass'] for r in results + servers),
                    'all_frame_budgets_pass': all(r['frame_budget_pass'] for r in results),
                    'server_zero_dropped_steps': all(r['dropped_steps'] == 0 for r in servers)}}
(ROOT / 'comparison.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({k: {x: v for x, v in r.items() if not x.startswith('median_engine')} for k, r in grouped.items()}, indent=2))
print(json.dumps({'gates': report['gates'], 'vrs_decision': decision}, indent=2))
