"""Offline CPU/GPU frame diagnosis of one source-engine Hybrid client.

The recording remains a functional diagnostic, never a final performance run.
QPC maps the native entity observations to the trace. Only the rendered PID's
socket counters are used; WebSocket application bytes are labelled separately.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts'))
from export_commander_pose_traces import trace_clock
from Performance.analyze_stress_frames import events, aggregate


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def export(executable, trace, destination, commands, name):
    source = destination / (name + '-commands.txt')
    source.write_text('\n'.join(commands) + '\n', encoding='utf-8')
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    subprocess.run([str(executable), '-OpenTraceFile=' + str(trace), '-NoUI', '-AutoQuit', '-Unattended',
                    '-ExecOnAnalysisCompleteCmd=@=' + str(source),
                    '-abslog=' + str(destination / (name + '-export.log'))],
                   check=True, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW, timeout=240)


def rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


def rates(before, after, seconds):
    first, last = before['network'], after['network']
    def delta(key):
        value = last[key] - first[key]
        if value < 0:
            raise RuntimeError('Network counters reset inside the observation window: ' + key)
        return value
    peers = {p['peer']: p for p in first['peers']}
    values = {}
    for key in ('retransmit_bytes', 'lane_0_sent_bytes', 'lane_1_sent_bytes', 'lane_2_sent_bytes', 'lane_3_sent_bytes'):
        values[key] = sum(p.get(key, 0) - peers.get(p['peer'], {}).get(key, 0) for p in last['peers'])
    sent, received = delta('datagram_sent_bytes'), delta('datagram_received_bytes')
    return {
        'seconds': seconds, 'udp_sent_bytes': sent, 'udp_received_bytes': received,
        'udp_sent_MB_s': sent / seconds / 1e6, 'udp_received_MB_s': received / seconds / 1e6,
        'udp_basis': last['datagram_byte_basis'],
        'websocket_submitted_application_bytes': delta('control_submitted_application_bytes'),
        'websocket_received_application_bytes': delta('control_received_application_bytes'),
        'websocket_basis': last['control_byte_basis'], **values,
        'retransmit_fraction_of_udp_sent': values['retransmit_bytes'] / sent if sent else 0,
        'end_send_queue_bytes': sum(p['queued_bytes'] for p in last['peers']),
        'end_oldest_send_ms': max((p['oldest_queue_ms'] for p in last['peers']), default=0),
        'end_oldest_receive_ms': last['oldest_receive_ms'],
        'end_pending_metadata': last['pending_metadata'],
        'control_interruptions': last['control_interruptions'] - first['control_interruptions'],
        'scope': 'Rendered PID only. Cumulative socket-payload counter deltas around native QPC observations; no ETW calibration, no final performance eligibility.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--insights', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--final', type=Path, required=True)
    args = parser.parse_args()
    run = args.run.resolve()
    launch = read(run / 'launch.json')
    entry = next(e for e in launch['processes'] if e['rendered'])
    trace = Path(entry['directory']) / 'frames.utrace'
    destination = run / 'frame-diagnosis'
    destination.mkdir(exist_ok=True)
    first, last = read(args.baseline)[entry['name']], read(args.final)[entry['name']]
    if first['pid'] != last['pid']:
        raise RuntimeError('The rendered PID changed inside the diagnostic window')
    clock = trace_clock(trace)
    start_qpc, end_qpc = int(first['snapshot_finished_qpc']), int(last['snapshot_started_qpc'])
    begin = (start_qpc - clock['start_cycle']) / clock['cycle_frequency']
    end = (end_qpc - clock['start_cycle']) / clock['cycle_frequency']
    if not 0 <= begin < end:
        raise RuntimeError('Invalid native QPC-to-trace window')
    save(destination / 'window.json', {'pid': first['pid'], 'trace': str(trace), 'analysis_tool': str(args.insights),
         'begin_seconds': begin, 'end_seconds': end, 'qpc': [start_qpc, end_qpc], 'clock': clock,
         'baseline': str(args.baseline), 'final': str(args.final), 'performance_eligible': False})
    ticks_file = destination / 'engine-ticks.csv'
    project_file = destination / 'project-timers.csv'
    window = f'-startTime={begin:.9f} -endTime={end:.9f}'
    export(args.insights, trace, destination, [
        f'TimingInsights.ExportThreads "{(destination / "threads.csv").as_posix()}"',
        f'TimingInsights.ExportTimingEvents "{ticks_file.as_posix()}" -threads=GameThread -timers=FEngineLoop::Tick {window} -columns=ThreadId,TimerId,TimerName,StartTime,EndTime,Duration,Depth',
        f'TimingInsights.ExportTimingEvents "{project_file.as_posix()}" -threads=GameThread -timers=GuLiHybrid_*,GuLiReview_* {window} -columns=ThreadId,TimerId,TimerName,StartTime,EndTime,Duration,Depth'], 'window')
    ticks = sorted([row for row in rows(ticks_file) if math.isfinite(float(row['EndTime']))
                    and float(row['StartTime']) >= begin and float(row['EndTime']) <= end],
                   key=lambda row: float(row['StartTime']))
    if len(ticks) < 100:
        raise RuntimeError('Not enough complete engine frames for diagnosis')
    by_duration = sorted(ticks, key=lambda row: float(row['Duration']))
    choices = {name: by_duration[min(len(ticks)-1, int(len(ticks)*quantile))]
               for name, quantile in [('fast', .1), ('median', .5), ('p95', .95), ('worst', 1)]}
    commands = []
    for name, row in choices.items():
        a, b = float(row['StartTime']), float(row['EndTime'])
        commands.append(f'TimingInsights.ExportTimingEvents "{(destination / (name+"-events.csv")).as_posix()}" '
                        f'-startTime={a:.9f} -endTime={b:.9f} -columns=ThreadId,ThreadName,TimerId,TimerName,StartTime,EndTime,Duration,Depth')
    export(args.insights, trace, destination, commands, 'frames')
    frames = []
    for name, row in choices.items():
        a, b = float(row['StartTime']), float(row['EndTime'])
        threads = events(destination / (name+'-events.csv'), a, b)
        frame = {'name': name, 'start_seconds': a, 'end_seconds': b,
                 'engine_tick_ms': float(row['Duration'])*1000,
                 'game_thread': aggregate(threads.get('GameThread', []))[:25],
                 'other_threads': {k: aggregate(v)[:12] for k, v in threads.items() if k != 'GameThread'},
                 'scope': 'One actual game-thread engine tick. Other threads and GPU queues share its wall-clock interval, not necessarily its logical frame, and must not be added to the GT duration.'}
        frames.append(frame)
        save(destination / (name+'-analysis.json'), frame)
        save(destination / (name+'-timeline.json'), {k: [
            {'timer': e['timer'], 'start_ms': (e['clipped_start']-a)*1000,
             'end_ms': (e['clipped_end']-a)*1000, 'depth': e['depth'], 'exclusive_ms': e['exclusive_ms']}
            for e in v if e['inclusive_ms'] >= .1] for k, v in threads.items()})
    intervals = [(float(right['StartTime'])-float(left['StartTime']))*1000 for left, right in zip(ticks, ticks[1:])]
    ordered = sorted(intervals)
    timers = {}
    for row in rows(project_file):
        timers.setdefault(row['TimerName'], []).append(float(row['Duration'])*1000)
    summary = {'schema': 'guli-frame-diagnosis/v1', 'pid': first['pid'], 'performance_eligible': False,
               'window_seconds': end-begin, 'complete_engine_ticks': len(ticks),
               'frame_interval_ms': {'minimum': min(intervals), 'median': statistics.median(intervals),
                                     'p95': ordered[int(len(ordered)*.95)], 'maximum': max(intervals)},
               'project_scopes': {k: {'calls': len(v), 'sum_ms': sum(v), 'median_call_ms': statistics.median(v), 'max_call_ms': max(v)}
                                  for k, v in timers.items()},
               'selected_frames': frames, 'network': rates(first, last, end-begin),
               'limits': ['Functional review with full diagnostic state export and trace overhead.',
                          'No final G/F gate or six-round performance pass follows from this capture.',
                          'Native socket-payload bytes exclude UDP/IP headers and are not an ETW OS-byte measurement.']}
    save(destination / 'summary.json', summary)
    print(json.dumps({k:v for k,v in summary.items() if k != 'selected_frames'}, ensure_ascii=False, indent=2), flush=True)


if __name__ == '__main__':
    main()
