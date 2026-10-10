"""Offline adapter around the project's existing windowed Trace exporter."""
import concurrent.futures
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT/'Scripts'))
from Performance import export_review_traces as trace

OUT=ROOT/'Artifacts/PerformanceOptimization20261010'
trace.INSIGHTS=Path('D:/UnrealEngine-5.7/Engine/Binaries/Win64/UnrealInsights.exe')
if not trace.INSIGHTS.exists():
    trace.INSIGHTS=Path('C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealInsights.exe')
(OUT/'Build/insights-tool.json').write_text(json.dumps({'executable':str(trace.INSIGHTS),'scope':'Offline analysis only; the running game uses the source engine.'},indent=2),encoding='utf-8')
selected=[]
for p in sorted((OUT/'Paired').glob('*/result.json')):
    r=json.loads(p.read_text(encoding='utf-8'));name=p.parent.name
    if ((name.startswith(('combined-dense200-','selected-dense200-')))
        or name.startswith('selected-stress-')
        or (name.startswith('single-') and r['candidate']=='ui_balanced' and r['round']==1)):
        selected.append(p.parent)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    summaries=list(pool.map(trace.export,selected))
evidence=[]
for directory,summary in zip(selected,summaries):
    if not summary['cpu_eligible']:
        evidence.append(summary);continue
    names=('GuLiClientFlight', 'CommanderPresentation_PurePose', 'SceneUI_', 'Avoidance_', 'StateTree', 'FlightReplication')
    all_cpu=[]
    for row in trace.rows(directory/'insights-windowed/all-timers.csv'):
        if any(s in row['Name'] for s in names):
            all_cpu.append({'timer':row['Name'],'calls':int(row['Count']),
                            'inclusive_ms_per_frame':float(row['Incl'])*1000/summary['frames'],
                            'exclusive_ms_per_frame':float(row['Excl'])*1000/summary['frames']})
    evidence.append({'run':directory.name,'frames':summary['frames'],'cpu_eligible':True,
                     'all_cpu_project_timers':all_cpu,
                     'game_thread_timers':summary['timers'],
                     'note':'Project-specific names are CPU scopes. Inclusive child scopes overlap: never sum parent and child.'})
(OUT/'trace-evidence.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'exported':len(evidence),'eligible':sum(r['cpu_eligible'] for r in evidence)}),flush=True)
