"""Export CPU evidence, mapping trace time to the common QPC window via $Trace.NewTrace.

The small metadata reader follows UE 5.7 TraceLog/Transport.h, Protocol7.h,
Protocol6::FNewEventEvent and TraceAnalysis/Analysis/Engine.cpp::OnTiming.
It does not parse CPU events: Unreal Insights performs that analysis.
"""
from __future__ import annotations
import argparse
import json
import struct
import subprocess
from pathlib import Path


def lz4_block(data, expected):
    output = bytearray()
    pos = 0
    while pos < len(data):
        token = data[pos]; pos += 1
        literals = token >> 4
        if literals == 15:
            while True:
                count = data[pos]; pos += 1; literals += count
                if count != 255: break
        output += data[pos:pos + literals]; pos += literals
        if pos == len(data): break
        offset = int.from_bytes(data[pos:pos + 2], 'little'); pos += 2
        length = (token & 15) + 4
        if token & 15 == 15:
            while True:
                count = data[pos]; pos += 1; length += count
                if count != 255: break
        assert 0 < offset <= len(output)
        if offset >= length:
            start = len(output) - offset
            output += output[start:start + length]
        else:
            for _ in range(length): output.append(output[-offset])
    assert len(output) == expected
    return output


def trace_clock(path):
    descriptions, streams = {}, {0: bytearray(), 1: bytearray()}
    with path.open('rb') as file:
        assert file.read(4) == b'2CRT', 'Expected UE trace format 2'
        metadata_size = struct.unpack('<H', file.read(2))[0]
        file.read(metadata_size)
        assert file.read(2) == bytes((4, 7)), 'Expected TidPacketSync / protocol 7'
        while file.tell() < 32 * 1024 * 1024:
            size, tid = struct.unpack('<HH', file.read(4))
            data = file.read(size - 4)
            thread = tid & 0x3fff
            if thread not in streams: continue
            if tid & 0x8000:
                expected = struct.unpack_from('<H', data)[0]
                data = lz4_block(data[2:], expected)
            streams[thread] += data
            stream = streams[thread]
            offset = 0
            while len(stream) - offset >= 4:
                uid, length = struct.unpack_from('<HH', stream, offset)
                if offset + 4 + length > len(stream): break
                event = stream[offset + 4:offset + 4 + length]
                offset += 4 + length
                if thread == 0:
                    assert uid == 0
                    event_id, fields, flags, logger_len, name_len = struct.unpack_from('<HBBBB', event)
                    names = 6 + 8 * fields
                    logger = event[names:names + logger_len].decode(); names += logger_len
                    name = event[names:names + name_len].decode(); names += name_len
                    schema = {}
                    for i in range(fields):
                        family, _, field_offset, field_size, type_info, name_size = struct.unpack_from('<BBHHBB', event, 6 + 8 * i)
                        if family == 2: continue
                        field_name = event[names:names + name_size].decode(); names += name_size
                        schema[field_name] = (field_offset, field_size)
                    descriptions[event_id] = (logger, name, schema)
                elif uid in descriptions:
                    logger, name, schema = descriptions[uid]
                    if logger == '$Trace' and name == 'NewTrace':
                        def value(key):
                            field_offset, field_size = schema[key]
                            return int.from_bytes(event[field_offset:field_offset + field_size], 'little')
                        start, frequency = value('StartCycle'), value('CycleFrequency')
                        assert start > 0 and frequency > 0
                        return {'start_cycle': start, 'cycle_frequency': frequency,
                                'qpc_origin_seconds': start / frequency,
                                'monotonic_origin_seconds': start / frequency + 16777216.0}
            del stream[:offset]
    raise ValueError('No $Trace.NewTrace metadata within first 32 MiB')


def export_role(run, role, insights):
    directory = (run / role).resolve()
    trace = directory / 'process.utrace'
    clock = trace_clock(trace)
    comparison = json.loads((run / 'comparison-window.json').read_text(encoding='utf-8'))
    begin, end = comparison['window_monotonic_seconds']
    start_time, end_time = begin - clock['monotonic_origin_seconds'], end - clock['monotonic_origin_seconds']
    clock['trace_window_seconds'] = [start_time, end_time]
    out = directory / 'insights'
    out.mkdir(exist_ok=True)
    (out / 'clock.json').write_text(json.dumps(clock, indent=2), encoding='utf-8')
    window = f'-startTime={start_time:.9f} -endTime={end_time:.9f}'
    commands = [f'TimingInsights.ExportThreads {out / "threads.csv"}',
                f'TimingInsights.ExportTimingEvents {out / "events-project.csv"} -threads=GameThread -timers=*GuLi*,*EngineLoop*,UWorld_Tick {window} -columns=ThreadId,TimerId,TimerName,StartTime,EndTime,Duration,Depth',
                f'TimingInsights.ExportTimerStatistics {out / "timers-gamethread.csv"} -threads=GameThread {window} -sortBy=TotalInclusiveTime',
                f'TimingInsights.ExportTimerStatistics {out / "timers-gpu.csv"} -threads=GPU1,GPU2 {window} -sortBy=TotalInclusiveTime']
    command_file = out / 'export-commands.txt'
    command_file.write_text('\n'.join(commands) + '\n', encoding='utf-8')
    args = [str(insights), '-OpenTraceFile=' + str(trace), '-NoUI', '-AutoQuit', '-Unattended',
            '-ExecOnAnalysisCompleteCmd=@=' + str(command_file), '-abslog=' + str(out / 'export.log')]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    subprocess.run(args, check=True, startupinfo=startup)
    assert (out / 'events-project.csv').is_file()
    print(json.dumps({'run': run.name, 'role': role, 'trace_window': [start_time, end_time]}), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--role', choices=('server', 'client1', 'client2'))
    parser.add_argument('--clock-only', action='store_true')
    parser.add_argument('--insights', type=Path, default=Path('C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealInsights.exe'))
    args = parser.parse_args()
    for role in ([args.role] if args.role else ('server', 'client1', 'client2')):
        if args.clock_only: print(role, trace_clock(args.run / role / 'process.utrace'))
        else: export_role(args.run, role, args.insights)


if __name__ == '__main__': main()
