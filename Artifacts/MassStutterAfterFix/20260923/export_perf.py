import json
import statistics
import subprocess
import sys
from pathlib import Path

base = Path('D:/UE5.7/test1')
root = base / 'Artifacts/MassStutterAfterFix/20260923'
sys.path.insert(0, str(base / 'Scripts'))
from export_commander_pose_traces import trace_clock

frames = json.loads((root / 'frames.json').read_text(encoding='utf-8'))
meta = frames['meta']
clock = trace_clock(root / 'current-session.utrace')
start = meta['qpc_begin'] - clock['qpc_origin_seconds'] + 1.0
end = meta['qpc_end'] - clock['qpc_origin_seconds'] - 1.0
clock['analysis_window'] = [start, end]
out = root / 'insights'
out.mkdir(exist_ok=True)
(out / 'clock.json').write_text(json.dumps(clock, indent=2), encoding='utf-8')
window = '-startTime={:.9f} -endTime={:.9f}'.format(start, end)
commands = ['TimingInsights.ExportThreads {}/threads.csv'.format(out.as_posix())]
for label, threads in (('gamethread', 'GameThread'), ('renderthread', 'Render*'), ('rhithread', 'RHIThread'), ('gpu', 'GPU*')):
    commands.append('TimingInsights.ExportTimerStatistics {}/timers-{}.csv -threads={} {} -sortBy=TotalInclusiveTime'.format(out.as_posix(), label, threads, window))
commands += [
    'TimingInsights.ExportTimingEvents {}/events.csv -threads=GameThread,Render*,RHIThread,GPU* {} -columns=ThreadId,ThreadName,TimerId,TimerName,StartTime,EndTime,Duration,Depth'.format(out.as_posix(), window),
    'TimingInsights.ExportTimerCallees {}/callees-gamethread.csv -threads=GameThread -timers=FEngineLoop::Tick {}'.format(out.as_posix(), window),
]
command_file = out / 'export-commands.txt'
command_file.write_text('\n'.join(commands) + '\n', encoding='utf-8')
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
args = ['C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealInsights.exe',
        '-OpenTraceFile=' + str(root / 'current-session.utrace'), '-NoUI', '-AutoQuit', '-Unattended',
        '-ExecOnAnalysisCompleteCmd=@=' + str(command_file), '-abslog=' + str(out / 'export.log')]
completed = subprocess.run(args, startupinfo=startup)
samples = [f['world_delta_seconds'] * 1000 for f in frames['frames'] if 1.0 <= f['wall_seconds'] <= meta['qpc_end'] - meta['qpc_begin'] - 1.0]
ordered = sorted(samples)
def quantile(p):
    index = (len(ordered) - 1) * p
    lo = int(index)
    return ordered[lo] + (ordered[min(lo + 1, len(ordered) - 1)] - ordered[lo]) * (index - lo)
summary = {'samples': len(samples), 'capture_seconds': meta['qpc_end'] - meta['qpc_begin'],
           'analysis_seconds': end - start, 'mean_frame_ms': statistics.mean(samples),
           'fps_by_mean_frame': 1000 / statistics.mean(samples), 'p50_ms': quantile(.5),
           'p95_ms': quantile(.95), 'p99_ms': quantile(.99), 'max_ms': max(samples),
           'over_33ms': sum(x > 33.333333 for x in samples), 'over_50ms': sum(x > 50 for x in samples),
           'export_exit_code': completed.returncode}
(root / 'frame-summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
print(json.dumps(summary, indent=2), flush=True)
sys.exit(completed.returncode)
