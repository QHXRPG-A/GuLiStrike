"""Read existing MoveStress artifacts using the original 30 s / 1140-unit gate."""
import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parents[1] / 'Scripts'))
from analyze_commander_move_stress import percentile, stats


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


def numeric(value):
    try:
        float(value)
        return True
    except (ValueError, TypeError):
        return False


def engine_rows(path):
    # UE appends its complete column schema and metadata at the end of a capture.
    # Metadata can contain numeric values, so test actual FrameTime, not task counts.
    with path.open(encoding='utf-8-sig', newline='') as stream:
        data = list(csv.reader(stream))
    header = next(row for row in reversed(data) if row and row[0] == 'EVENTS')
    frame_index = header.index('FrameTime')
    return [dict(zip(header, row)) for row in data[1:]
            if len(row) > frame_index and numeric(row[frame_index]) and not row[0].startswith('[')]


result = []
for run in sorted((ROOT / 'MoveStress').glob('detour-fixed*-n1200-c*')):
    roles = ['server', 'client1'] + (['client2'] if run.name.endswith('c2') else [])
    if not all((run / role / 'summary.json').is_file() for role in roles):
        continue
    summaries = {role: read(run / role / 'summary.json') for role in roles}
    assert all(not s['error'] for s in summaries.values()), run.name
    begin = max(s['capture_start_monotonic_seconds'] for s in summaries.values()) + 5
    end = begin + 30
    assert all(s['capture_start_monotonic_seconds'] + s['measured_seconds'] >= end for s in summaries.values())
    (run / 'comparison-window.json').write_text(json.dumps({
        'window_monotonic_seconds': [begin, end], 'roles': roles, 'capacity_override': 2048}, indent=2), encoding='utf-8')
    for role, summary in summaries.items():
        selected = [r for r in rows(run / role / 'frames.csv')
                    if begin <= summary['capture_start_monotonic_seconds'] + float(r['seconds']) < end]
        moving = [float(r['observed_moving']) for r in selected]
        frame = stats([float(r['frame_ms']) for r in selected])
        item = {'run': run.name, 'role': role, 'window_monotonic_seconds': [begin, end],
                'frame_ms': frame, 'alive_min': min(int(r['alive']) for r in selected),
                'moving_p05': percentile(moving, .05), 'zero_moving_fraction': sum(x == 0 for x in moving)/len(moving),
                'load_pass': all(int(r['alive']) == 1200 for r in selected) and percentile(moving, .05) >= 1140,
                'pose_age_ms': stats([float(r['pose_age_ms']) for r in selected])}
        if role == 'server':
            item.update({key: summary[key] for key in ['simulation_steps', 'dropped_steps', 'shuttle_rounds']})
            engine = engine_rows(run / role / 'engine.csv')
            item['navigation_full_capture'] = {key: stats([float(r[key]) for r in engine])
                for key in engine[0] if key and ('Navigation' in key or 'NavTasks' in key)}
        else:
            item['frame_budget_pass'] = frame['p95'] <= (16.67 if run.name.endswith('c1') else 33.33)
        result.append(item)
        print(json.dumps({key: item[key] for key in ('run', 'role', 'moving_p05', 'zero_moving_fraction', 'load_pass', 'frame_ms')}, ensure_ascii=False))
(ROOT / 'movement-results.json').write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
