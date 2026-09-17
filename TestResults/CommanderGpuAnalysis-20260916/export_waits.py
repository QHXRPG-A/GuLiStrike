"""Export selected CPU wait events from two existing traces, with no new capture."""
import json
import subprocess
import sys
from pathlib import Path

OUT = Path(__file__).resolve().parent
BASELINE = OUT.parent / 'CommanderClientCpu-Retest-20260916'
INSIGHTS = Path('C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealInsights.exe')
for run_name in ('retest-1-n1200-c1', 'retest-1-n1200-c2'):
    source = BASELINE / run_name / 'client1'
    dest = OUT / run_name
    dest.mkdir(exist_ok=True)
    clock = json.loads((source / 'insights/clock.json').read_text(encoding='utf-8'))
    start, end = clock['trace_window_seconds']
    window = f'-startTime={start:.9f} -endTime={end:.9f}'
    columns = '-columns=ThreadId,TimerId,TimerName,StartTime,EndTime,Duration,Depth'
    commands = [f'TimingInsights.ExportTimingEvents {dest / "wait-events.csv"} -timers=WaitForVisibilityTasks,GPUBound_*,*Occlusion*,SceneVisibility_* {window} {columns}',
                f'TimingInsights.ExportTimingEvents {dest / "detail-events.csv"} -startTime={start + 10:.9f} -endTime={start + 11:.9f} {columns}']
    if '--gpu-only' in sys.argv:
        commands = [f'TimingInsights.ExportTimingEvents {dest / "gpu-events.csv"} -threads=GPU0* -timers=Frame,LumenScreenProbeGather,ContrastAdaptiveShading,PrepareImageBasedVRS,TemporalSuperResolution*,LumenReflections,ShadowDepths,BasePass,PrePass*,RenderVirtualShadowMaps,Nanite* {window} {columns}']
    suffix = '-gpu' if '--gpu-only' in sys.argv else ''
    command_file = dest / f'export-commands{suffix}.txt'
    command_file.write_text('\n'.join(commands) + '\n', encoding='utf-8')
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    subprocess.run([str(INSIGHTS), '-OpenTraceFile=' + str(source / 'process.utrace'), '-NoUI', '-AutoQuit', '-Unattended',
                    '-ExecOnAnalysisCompleteCmd=@=' + str(command_file), '-abslog=' + str(dest / f'export{suffix}.log')],
                   check=True, startupinfo=startup)
    print(json.dumps({'run': run_name, 'files': {p.name: p.stat().st_size for p in dest.glob('*.csv')}}), flush=True)
