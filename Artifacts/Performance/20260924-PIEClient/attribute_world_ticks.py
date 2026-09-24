"""Attribute embedded PIE world ticks to server/client/editor by nested scopes."""

from __future__ import annotations

import argparse
import bisect
import json
import re
import statistics
from collections import Counter, defaultdict
from pathlib import Path

from analyze_trace import CPU_SPEC, EVENT_THREAD, FRAME, percentile, varint


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace_text", type=Path)
    parser.add_argument("summary", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tail-seconds", type=float, default=0.0)
    args = parser.parse_args()

    baseline = json.loads(args.summary.read_text(encoding="utf-8"))
    wanted = {
        row["name"]
        for group in ("cpu_top_game_exclusive", "cpu_top_project_exclusive")
        for row in baseline[group]
    }
    wanted.add("UWorld_Tick")
    spec_names: dict[int, str] = {}
    frame_starts: list[int] = []
    worlds: list[tuple[int, int, int, int]] = []
    scopes: list[tuple[str, int, int, int, int]] = []
    last = 0
    stack: list[list[int]] = []

    # The text trace declares timer names before the captured PIE frame region.
    with args.trace_text.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if "CpuProfiler.EventSpec :" in line:
                match = CPU_SPEC.search(line)
                if match:
                    spec_names[int(match[1])] = match[2]
            elif "Misc.BeginFrame :" in line:
                match = FRAME.search(line)
                if match and match[1] == "BeginFrame" and int(match[4]) == 0:
                    frame_starts.append(int(match[2]))
            elif "CpuProfiler.EventBatchV3 : Data=0x[" in line:
                match = EVENT_THREAD.search(line)
                if not match or int(match[1]) != 2:  # GameThread
                    continue
                start = line.find("Data=0x[") + len("Data=0x[")
                end = line.find("]", start)
                if end < 0:
                    continue
                data = bytes.fromhex(line[start:end])
                pos = 0
                while pos < len(data):
                    encoded, pos = varint(data, pos)
                    cycle = encoded >> 2
                    if cycle < last:
                        cycle += last
                    if encoded & 2:
                        if encoded & 1:
                            _, pos = varint(data, pos)
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
                        exclusive = max(0, duration - children)
                        name = spec_names.get(spec, "")
                        if name == "UWorld_Tick":
                            worlds.append((began, cycle, duration, exclusive))
                        elif name in wanted:
                            scopes.append((name, began, cycle, duration, exclusive))
                        if stack:
                            stack[-1][2] += duration
                    last = cycle

    frame_starts.sort()
    cutoff_cycle = frame_starts[-1] - int(args.tail_seconds * baseline["ticks_per_second"]) if args.tail_seconds else 0
    if cutoff_cycle:
        worlds = [x for x in worlds if x[0] >= cutoff_cycle]
        scopes = [x for x in scopes if x[1] >= cutoff_cycle]
    grouped: dict[int, list[tuple[int, int, int, int]]] = defaultdict(list)
    for world in worlds:
        frame_idx = bisect.bisect_right(frame_starts, world[0]) - 1
        grouped[frame_idx].append(world)
    world_counts = Counter(len(items) for items in grouped.values())

    ordered: list[tuple[int, int, int, int, int]] = []
    ordinal_durations: dict[int, list[float]] = defaultdict(list)
    for frame_idx, items in grouped.items():
        if len(items) != 3:
            continue
        for ordinal, (began, ended, duration, exclusive) in enumerate(sorted(items)):
            ordered.append((began, ended, ordinal, duration, exclusive))
            ordinal_durations[ordinal].append(duration)
    ordered.sort()
    world_starts = [x[0] for x in ordered]

    by_ordinal: dict[int, dict[str, list[int]]] = defaultdict(lambda: defaultdict(lambda: [0, 0, 0]))
    unattributed = Counter()
    for name, began, ended, duration, exclusive in scopes:
        idx = bisect.bisect_right(world_starts, began) - 1
        if idx >= 0 and ended <= ordered[idx][1]:
            ordinal = ordered[idx][2]
            row = by_ordinal[ordinal][name]
            row[0] += duration
            row[1] += exclusive
            row[2] += 1
        else:
            unattributed[name] += 1

    frequency = baseline["ticks_per_second"]
    def ms(ticks: int) -> float:
        return ticks / frequency * 1000.0

    result = {
        "tail_seconds_requested": args.tail_seconds,
        "cutoff_cycle": cutoff_cycle,
        "world_tick_count": len(worlds),
        "per_game_frame_world_tick_count": dict(world_counts),
        "unattributed_selected_scope_calls": dict(unattributed.most_common(12)),
        "ordinals": {},
    }
    for ordinal, durations in sorted(ordinal_durations.items()):
        values = [ms(x) for x in durations]
        rows = []
        for name, (inclusive, exclusive, calls) in by_ordinal[ordinal].items():
            rows.append({
                "name": name,
                "calls": calls,
                "inclusive_per_world_tick_ms": round(ms(inclusive) / len(durations), 3),
                "exclusive_per_world_tick_ms": round(ms(exclusive) / len(durations), 3),
            })
        result["ordinals"][ordinal] = {
            "world_tick_count": len(durations),
            "world_tick_median_ms": round(statistics.median(values), 3),
            "world_tick_p95_ms": round(percentile(values, 0.95), 3),
            "world_tick_mean_ms": round(statistics.fmean(values), 3),
            "top_scopes_by_exclusive": sorted(rows, key=lambda x: x["exclusive_per_world_tick_ms"], reverse=True)[:35],
        }
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
