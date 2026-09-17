"""Reuses existing UE Insights exporter; exports clients sequentially."""
import sys,json
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'Scripts'))
from export_commander_pose_traces import export_role
root=Path(__file__).resolve().parent
# Insights is a read-only trace analyzer. Native builds and runtime tests use source UE only.
insights=Path('C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealInsights.exe')
for run in sorted(root.glob(sys.argv[1] if len(sys.argv)>1 else '*-capacity-*')):
    roles=[r for r in ('server','client1','client2') if (run/r/'summary.json').exists()]
    summaries=[json.loads((run/r/'summary.json').read_text(encoding='utf-8-sig')) for r in roles]
    begin=max(s['capture_start_monotonic_seconds'] for s in summaries)+5
    end=begin+30
    assert all(s['capture_start_monotonic_seconds']+s['measured_seconds']>=end for s in summaries)
    (run/'comparison-window.json').write_text(json.dumps({'window_monotonic_seconds':[begin,end], 'roles':roles, 'capacity_override':2048},indent=2),encoding='utf-8')
    for role in roles:
        if role=='server': continue
        if (run/role/'insights/timers-gamethread.csv').exists(): continue
        export_role(run,role,insights)
