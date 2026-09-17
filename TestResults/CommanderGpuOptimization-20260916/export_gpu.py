"""Read existing traces with Insights; never run concurrently with a capture."""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parents[1] / 'Scripts'))
from export_commander_pose_traces import trace_clock
INSIGHTS = Path('C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealInsights.exe')

for run in sorted(ROOT.glob(sys.argv[1] if len(sys.argv) > 1 else '[ABC]-*-n1200-c*')):
    comparison = json.loads((run / 'comparison-window.json').read_text())
    begin, end = comparison['window_monotonic_seconds']
    for role in comparison['roles']:
        if role == 'server': continue
        source = run / role
        out = source / 'insights'
        out.mkdir(exist_ok=True)
        if (out / 'gpu-events.csv').exists() and (out / 'wait-events.csv').exists(): continue
        clock = trace_clock(source / 'process.utrace')
        start, finish = begin - clock['monotonic_origin_seconds'], end - clock['monotonic_origin_seconds']
        clock['trace_window_seconds'] = [start, finish]
        (out / 'clock.json').write_text(json.dumps(clock, indent=2))
        window = f'-startTime={start:.9f} -endTime={finish:.9f}'
        columns = '-columns=ThreadId,TimerId,TimerName,StartTime,EndTime,Duration,Depth'
        commands = [f'TimingInsights.ExportThreads {out / "threads.csv"}',
            f'TimingInsights.ExportTimingEvents {out / "gpu-events.csv"} -threads=GPU0* -timers=Frame,LumenScreenProbeGather,ContrastAdaptiveShading,PrepareImageBasedVRS,TemporalSuperResolution*,VolumetricFog*,LumenReflections,ShadowDepths,BasePass,PrePass*,RenderVirtualShadowMaps,Nanite* {window} {columns}',
            f'TimingInsights.ExportTimingEvents {out / "wait-events.csv"} -timers=WaitForVisibilityTasks,GPUBound_* {window} {columns}']
        command_file = out / 'export-commands.txt'
        command_file.write_text('\n'.join(commands) + '\n')
        startup = subprocess.STARTUPINFO(); startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow = 0
        subprocess.run([str(INSIGHTS), '-OpenTraceFile=' + str(source / 'process.utrace'), '-NoUI', '-AutoQuit', '-Unattended',
            '-ExecOnAnalysisCompleteCmd=@=' + str(command_file), '-abslog=' + str(out / 'export.log')], check=True, startupinfo=startup)
        assert (out / 'gpu-events.csv').exists() and (out / 'wait-events.csv').exists(), out
        print(f'{run.name}/{role}: exported', flush=True)
