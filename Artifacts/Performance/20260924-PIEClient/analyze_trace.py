"""Summarize UE 5.7 CPU V3 scopes, frame markers, and GPU breadcrumbs.

Reads text produced by the engine's TraceAnalyzer from one .utrace capture.
All durations are derived from trace timestamps; no PIE settings are changed.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
from collections import defaultdict
from pathlib import Path


THREAD = re.compile(r'ThreadId=(\d+).*?Name="([^"]*)"')
CPU_SPEC = re.compile(r'CpuProfiler\.EventSpec : Id=(\d+) Name="([^"]*)" File="([^"]*)"')
CPU_META = re.compile(r'CpuProfiler\.Metadata : Id=(\d+) SpecId=(\d+)')
GPU_SPEC = re.compile(r'GpuProfiler\.EventBreadcrumbSpec : SpecId=(\d+) StaticName="([^"]*)"')
EVENT_THREAD = re.compile(r'\bEVENT \[(\d+)\]')
FRAME = re.compile(r'Misc\.(BeginFrame|EndFrame) : Cycle=(\d+)\(([\d.]+)\) FrameType=(\d+)')
GPU_BEGIN = re.compile(r'EventBeginBreadcrumb : SpecId=(\d+) QueueId=(\d+) GPUTimestampTOP=0x([0-9A-Fa-f]+)')
GPU_END = re.compile(r'EventEndBreadcrumb : QueueId=(\d+) GPUTimestampBOP=0x([0-9A-Fa-f]+)')


def varint(data: bytes, pos: int) -> tuple[int, int]:
    value = 0
    shift = 0
    while True:
        byte = data[pos]
        pos += 1
        value |= (byte & 0x7F) << shift
        if byte < 0x80:
            return value, pos
        shift += 7


def percentile(values: list[float], quantile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * quantile) - 1)]


def summary(values: list[float]) -> dict:
    if not values:
        return {"count": 0}
    return {
        "count": len(values),
        "median_ms": round(statistics.median(values), 3),
        "p95_ms": round(percentile(values, 0.95), 3),
        "max_ms": round(max(values), 3),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace_text", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tail-seconds", type=float, default=0.0)
    args = parser.parse_args()

    cutoff_cycle = 0
    if args.tail_seconds:
        last_frame_cycle = 0
        with args.trace_text.open("r", encoding="utf-8", errors="replace") as stream:
            for line in stream:
                if "Misc.BeginFrame :" in line and "FrameType=0" in line:
                    match = FRAME.search(line)
                    if match:
                        last_frame_cycle = max(last_frame_cycle, int(match[2]))
        cutoff_cycle = last_frame_cycle - int(args.tail_seconds * 10_000_000)

    threads: dict[int, str] = {}
    cpu_specs: dict[int, tuple[str, str]] = {}
    cpu_metadata: dict[int, int] = {}
    gpu_specs: dict[int, str] = {}
    frames = {0: {"begin": [], "end": []}, 1: {"begin": [], "end": []}}
    cpu_last: dict[int, int] = defaultdict(int)
    cpu_stacks: dict[int, list[list[int]]] = defaultdict(list)
    gpu_stacks: dict[int, list[list[int]]] = defaultdict(list)
    cpu_totals: dict[tuple[int, int], list[int]] = defaultdict(lambda: [0, 0, 0, 0])
    gpu_totals: dict[tuple[int, int], list[int]] = defaultdict(lambda: [0, 0, 0, 0])
    gpu_frame_cycles: list[int] = []
    cpu_events = 0
    gpu_events = 0
    unmatched_cpu_ends = 0

    with args.trace_text.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if "$Trace.ThreadInfo :" in line:
                match = THREAD.search(line)
                if match:
                    threads[int(match[1])] = match[2]
            elif "CpuProfiler.EventSpec :" in line:
                match = CPU_SPEC.search(line)
                if match:
                    cpu_specs[int(match[1])] = (match[2], match[3])
            elif "CpuProfiler.Metadata :" in line:
                match = CPU_META.search(line)
                if match:
                    cpu_metadata[int(match[1])] = int(match[2])
            elif "GpuProfiler.EventBreadcrumbSpec :" in line:
                match = GPU_SPEC.search(line)
                if match:
                    gpu_specs[int(match[1])] = match[2]
            elif "Misc.BeginFrame :" in line or "Misc.EndFrame :" in line:
                match = FRAME.search(line)
                if match:
                    frames[int(match[4])][match[1].removesuffix("Frame").lower()].append(
                        (int(match[2]), float(match[3]))
                    )
            elif "CpuProfiler.EventBatchV3 : Data=0x[" in line:
                match = EVENT_THREAD.search(line)
                if not match:
                    continue
                tid = int(match[1])
                start = line.find("Data=0x[") + len("Data=0x[")
                end = line.find("]", start)
                if end < 0:
                    continue
                data = bytes.fromhex(line[start:end])
                pos = 0
                stack = cpu_stacks[tid]
                last = cpu_last[tid]
                while pos < len(data):
                    encoded, pos = varint(data, pos)
                    cycle = encoded >> 2
                    if cycle < last:
                        cycle += last
                    if encoded & 2:  # coroutine save/restore
                        if encoded & 1:
                            _, pos = varint(data, pos)  # coroutine id
                            depth, pos = varint(data, pos)
                            stack.append([-2, cycle, 0])
                            for _ in range(depth):
                                stack.append([-3, cycle, 0])
                        else:
                            depth, pos = varint(data, pos)
                            for _ in range(depth + 1):
                                if stack:
                                    stack.pop()
                    elif encoded & 1:
                        encoded_spec, pos = varint(data, pos)
                        spec = -(encoded_spec >> 1) if encoded_spec & 1 else encoded_spec >> 1
                        stack.append([spec, cycle, 0])
                    elif stack:
                        spec, began, children = stack.pop()
                        duration = max(0, cycle - began)
                        if spec >= 0 and began >= cutoff_cycle:
                            row = cpu_totals[(tid, spec)]
                            row[0] += duration
                            row[1] += max(0, duration - children)
                            row[2] += 1
                            row[3] = max(row[3], duration)
                        if stack:
                            stack[-1][2] += duration
                    else:
                        unmatched_cpu_ends += 1
                    last = cycle
                    cpu_events += 1
                cpu_last[tid] = last
            elif "GpuProfiler.EventBeginBreadcrumb :" in line:
                match = GPU_BEGIN.search(line)
                if match:
                    spec, queue, cycle = int(match[1]), int(match[2]), int(match[3], 16)
                    if cycle:
                        gpu_stacks[queue].append([spec, cycle, 0])
                        gpu_events += 1
            elif "GpuProfiler.EventEndBreadcrumb :" in line:
                match = GPU_END.search(line)
                if match:
                    queue, cycle = int(match[1]), int(match[2], 16)
                    stack = gpu_stacks[queue]
                    if cycle and stack:
                        spec, began, children = stack.pop()
                        duration = max(0, cycle - began)
                        if began >= cutoff_cycle:
                            row = gpu_totals[(queue, spec)]
                            row[0] += duration
                            row[1] += max(0, duration - children)
                            row[2] += 1
                            row[3] = max(row[3], duration)
                        if stack:
                            stack[-1][2] += duration
                        if began >= cutoff_cycle and gpu_specs.get(spec) == "Frame" and queue == 0:
                            gpu_frame_cycles.append(duration)
                        gpu_events += 1

    game_begins = sorted(x for x in frames[0]["begin"] if x[0] >= cutoff_cycle)
    ratios = [
        (b[0] - a[0]) / (b[1] - a[1])
        for a, b in zip(game_begins, game_begins[1:])
        if b[1] > a[1] and b[0] > a[0]
    ]
    ticks_per_second = statistics.median(ratios) if ratios else 10_000_000.0
    frame_count = max(1, len(game_begins) - 1)

    def ms(cycles: int) -> float:
        return cycles / ticks_per_second * 1000.0

    game_intervals = [ms(b[0] - a[0]) for a, b in zip(game_begins, game_begins[1:])]
    render_begins = sorted(x for x in frames[1]["begin"] if x[0] >= cutoff_cycle)
    render_intervals = [ms(b[0] - a[0]) for a, b in zip(render_begins, render_begins[1:])]

    cpu_rows = []
    for (tid, spec), (inclusive, exclusive, calls, maximum) in cpu_totals.items():
        name, file = cpu_specs.get(spec, (f"<spec {spec}>", ""))
        if spec < 0:
            name, file = cpu_specs.get(cpu_metadata.get(-spec, -1), (f"<metadata {-spec}>", ""))
        cpu_rows.append({
            "thread": threads.get(tid, f"Thread {tid}"),
            "thread_id": tid,
            "name": name,
            "file": file,
            "calls": calls,
            "inclusive_total_ms": round(ms(inclusive), 3),
            "exclusive_total_ms": round(ms(exclusive), 3),
            "inclusive_per_game_frame_ms": round(ms(inclusive) / frame_count, 3),
            "exclusive_per_game_frame_ms": round(ms(exclusive) / frame_count, 3),
            "max_call_ms": round(ms(maximum), 3),
        })
    gpu_rows = []
    for (queue, spec), (inclusive, exclusive, calls, maximum) in gpu_totals.items():
        gpu_rows.append({
            "queue": queue,
            "name": gpu_specs.get(spec, f"<spec {spec}>"),
            "calls": calls,
            "inclusive_total_ms": round(ms(inclusive), 3),
            "exclusive_total_ms": round(ms(exclusive), 3),
            "inclusive_per_game_frame_ms": round(ms(inclusive) / frame_count, 3),
            "exclusive_per_game_frame_ms": round(ms(exclusive) / frame_count, 3),
            "max_call_ms": round(ms(maximum), 3),
        })

    result = {
        "trace_text": str(args.trace_text),
        "tail_seconds_requested": args.tail_seconds,
        "cutoff_cycle": cutoff_cycle,
        "ticks_per_second": round(ticks_per_second),
        "capture_seconds_from_game_frames": round(sum(game_intervals) / 1000.0, 3),
        "game_frame_intervals": summary(game_intervals[10:-5] if len(game_intervals) > 20 else game_intervals),
        "render_frame_intervals": summary(render_intervals[10:-5] if len(render_intervals) > 20 else render_intervals),
        "gpu_frame_scopes": summary([ms(x) for x in gpu_frame_cycles]),
        "frames_seen_in_raw_trace": {kind: {k: len(v) for k, v in events.items()} for kind, events in frames.items()},
        "game_frames_in_selected_window": len(game_begins),
        "threads": threads,
        "cpu_events_in_raw_trace": cpu_events,
        "gpu_events_in_raw_trace": gpu_events,
        "unmatched_cpu_ends_in_raw_trace": unmatched_cpu_ends,
        "cpu_top_game_exclusive": sorted((x for x in cpu_rows if x["thread"] == "GameThread"), key=lambda x: x["exclusive_total_ms"], reverse=True)[:35],
        "cpu_top_render_exclusive": sorted((x for x in cpu_rows if "RenderThread" in x["thread"]), key=lambda x: x["exclusive_total_ms"], reverse=True)[:25],
        "cpu_top_project_exclusive": sorted((x for x in cpu_rows if "\\UE5.7\\test1\\Source\\" in x["file"] or "\\UE5.7\\test1\\Plugins\\" in x["file"]), key=lambda x: x["exclusive_total_ms"], reverse=True)[:35],
        "gpu_top_graphics_exclusive": sorted((x for x in gpu_rows if x["queue"] == 0), key=lambda x: x["exclusive_total_ms"], reverse=True)[:35],
        "gpu_top_graphics_inclusive": sorted((x for x in gpu_rows if x["queue"] == 0), key=lambda x: x["inclusive_total_ms"], reverse=True)[:20],
    }
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("ticks_per_second", "capture_seconds_from_game_frames", "game_frame_intervals", "render_frame_intervals", "gpu_frame_scopes", "game_frames_in_selected_window")}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
