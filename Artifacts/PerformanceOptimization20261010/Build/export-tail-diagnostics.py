"""Offline tail attribution; diagnostic scopes are inclusive and not additive."""
import csv, json, math, pathlib, sys, concurrent.futures
ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'Scripts'))
from Performance import export_review_traces as trace
OUT = ROOT / 'Artifacts/PerformanceOptimization20261010'
NAMES = ['FEngineLoop::Tick', 'Tick_Engine', 'UWorld_Tick', 'GuLiSceneUI_Paint',
         'Slate::Prepass', 'Slate::DrawWindows', 'ProcessLocalPlayerSlateOperations',
         'GuLiCommanderMassStateTreeProcessor_0', 'GuLiCommanderPredictiveAvoidanceProcessor_1',
         'GuLiCommander_PredictiveAvoidanceCandidateQuery', 'ProcessUntilTasksComplete',
         'WinPumpMessages', 'FEngineLoop_PumpMessages', 'GuLiCombatEffects_Presentation',
         'GuLiMuzzles_Lifecycle', 'GameNetDriver', 'UpdateCoreCsvStats_BeginFrame']

def export(directory):
    summary = json.loads((directory / 'insights-windowed/summary.json').read_text())
    begin, end = summary['trace_interval_seconds']
    dst = directory / 'tail-diagnostics'; dst.mkdir(exist_ok=True)
    events = dst / 'events.csv'
    if not events.exists():
        trace.analyze(directory, [f'TimingInsights.ExportTimingEvents "{events.as_posix()}" -threads=GameThread -timers="{",".join(NAMES)}" -columns=ThreadId,TimerId,TimerName,StartTime,EndTime,Duration,Depth -startTime={begin:.9f} -endTime={end:.9f}'], dst)
    rows = trace.rows(events)
    frames = sorted([r for r in rows if r['TimerName'] == 'FEngineLoop::Tick'
                     and float(r['StartTime']) >= begin and float(r['EndTime']) <= end], key=lambda r: float(r['StartTime']))
    scopes = [r for r in rows if r['TimerName'] != 'FEngineLoop::Tick']
    by_frame = []; index = 0
    scopes.sort(key=lambda r: float(r['StartTime']))
    for f in frames:
        lo, hi = float(f['StartTime']), float(f['EndTime'])
        sums = {name: 0.0 for name in NAMES[1:]}
        while index < len(scopes) and float(scopes[index]['StartTime']) < lo: index += 1
        j = index
        while j < len(scopes) and float(scopes[j]['StartTime']) < hi:
            row = scopes[j]
            if float(row['EndTime']) <= hi + 0.00002:
                sums[row['TimerName']] += float(row['Duration']) * 1000
            j += 1
        by_frame.append({'start': lo, 'duration_ms': float(f['Duration']) * 1000, **sums})
        index = j
    slow = sorted(by_frame, key=lambda r:r['duration_ms'], reverse=True)[:max(1, math.ceil(len(by_frame) * .05))]
    result = {'run': directory.name, 'frames': len(frames), 'tail_count': len(slow),
              'scope': 'Inclusive scope sums per complete engine frame. Parent and child overlap; do not add them.',
              'mean': {k: sum(r[k] for r in by_frame)/len(by_frame) for k in ['duration_ms'] + NAMES[1:]},
              'tail_mean': {k: sum(r[k] for r in slow)/len(slow) for k in ['duration_ms'] + NAMES[1:]},
              'slowest': slow[:5]}
    (dst / 'summary.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    if directory.name == 'selected-stress-onscreen-selected-r2-selected':
        a, b = 1092.3, 1095.5
        detail = dst/'stall-events.csv'
        if not detail.exists():
            trace.analyze(directory, [f'TimingInsights.ExportTimingEvents "{detail.as_posix()}" -threads=GameThread -columns=ThreadId,TimerId,TimerName,StartTime,EndTime,Duration,Depth -startTime={a} -endTime={b}'], dst)
    print(json.dumps({'done': directory.name, 'frames':len(frames), 'tail':len(slow)}), flush=True)
    return result

directories = sorted(p.parent.parent for p in (OUT/'Paired').glob('combined-dense200-*/insights-windowed/summary.json'))
directories.append(OUT/'Paired/selected-stress-onscreen-selected-r2-selected')
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool: results = list(pool.map(export, directories))
(OUT/'tail-diagnostics.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
