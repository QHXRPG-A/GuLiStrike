"""Attribute GameThread tick barriers and TaskTrace waits in a UE text trace."""

from __future__ import annotations

import argparse
import bisect
import json
import math
import re
import statistics
from collections import Counter, defaultdict
from pathlib import Path


CPU_SPEC = re.compile(r'CpuProfiler\.EventSpec : Id=(\d+) Name="([^"]*)"')
FRAME = re.compile(r'Misc\.BeginFrame : Cycle=(\d+)\([^)]*\) FrameType=0')
EVENT_THREAD = re.compile(r'\bEVENT \[(\d+)\]')
TASK_TIME = re.compile(r'\bTimestamp=0x([0-9A-Fa-f]+)')
TASK_IDS = re.compile(r'\bTasks=\[([^]]*)\]')
THREAD = re.compile(r'ThreadId=(\d+).*?Name="([^"]*)"')
FREQ = 10_000_000


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


def ms(cycles: int) -> float:
    return cycles / FREQ * 1000.0


def pct(values: list[float], q: float) -> float:
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * q) - 1)]


def stats(cycles: list[int]) -> dict:
    if not cycles:
        return {"count": 0}
    values = [ms(x) for x in cycles]
    return {
        "count": len(values),
        "mean_ms": round(statistics.fmean(values), 3),
        "median_ms": round(statistics.median(values), 3),
        "p95_ms": round(pct(values, 0.95), 3),
        "max_ms": round(max(values), 3),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace_text", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tail-seconds", type=float, default=12.0)
    args = parser.parse_args()

    specs: dict[int, str] = {}
    threads: dict[int, str] = {}
    frames: list[int] = []
    worlds: list[dict] = []
    processes: list[dict] = []
    waits: list[dict] = []
    wait_stack: dict[int, list[dict]] = defaultdict(list)
    cpu_stack: list[list] = []
    last_cycle = 0
    truncated_cpu_batches = 0
    truncated_wait_lists = 0

    with args.trace_text.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if "CpuProfiler.EventSpec :" in line:
                match = CPU_SPEC.search(line)
                if match:
                    specs[int(match[1])] = match[2]
            elif "$Trace.ThreadInfo :" in line:
                match = THREAD.search(line)
                if match:
                    threads[int(match[1])] = match[2]
            elif "Misc.BeginFrame :" in line:
                match = FRAME.search(line)
                if match:
                    frames.append(int(match[1]))
            elif "TaskTrace.WaitingStarted :" in line:
                tid_match = EVENT_THREAD.search(line)
                ts_match = TASK_TIME.search(line)
                ids_match = TASK_IDS.search(line)
                if tid_match and ts_match and ids_match:
                    ids_text = ids_match[1]
                    truncated = "..." in ids_text
                    truncated_wait_lists += truncated
                    ids = [int(x) for x in ids_text.split() if x.isdecimal()]
                    wait_stack[int(tid_match[1])].append({
                        "thread_id": int(tid_match[1]),
                        "begin": int(ts_match[1], 16),
                        "task_ids": ids,
                        "ids_truncated": truncated,
                        "end": None,
                    })
            elif "TaskTrace.WaitingFinished :" in line:
                tid_match = EVENT_THREAD.search(line)
                ts_match = TASK_TIME.search(line)
                if tid_match and ts_match and wait_stack[int(tid_match[1])]:
                    item = wait_stack[int(tid_match[1])].pop()
                    item["end"] = int(ts_match[1], 16)
                    waits.append(item)
            elif "CpuProfiler.EventBatchV3 : Data=0x[" in line:
                tid_match = EVENT_THREAD.search(line)
                if not tid_match or int(tid_match[1]) != 2:
                    continue
                if "..." in line:
                    truncated_cpu_batches += 1
                    continue
                start = line.find("Data=0x[") + len("Data=0x[")
                end = line.find("]", start)
                data = bytes.fromhex(line[start:end])
                pos = 0
                while pos < len(data):
                    encoded, pos = varint(data, pos)
                    cycle = encoded >> 2
                    if cycle < last_cycle:
                        cycle += last_cycle
                    if encoded & 2:
                        if encoded & 1:
                            _, pos = varint(data, pos)
                            depth, pos = varint(data, pos)
                            cpu_stack.append([-2, cycle, 0, None, None])
                            for _ in range(depth):
                                cpu_stack.append([-3, cycle, 0, None, None])
                        else:
                            depth, pos = varint(data, pos)
                            for _ in range(depth + 1):
                                if cpu_stack:
                                    cpu_stack.pop()
                    elif encoded & 1:
                        encoded_spec, pos = varint(data, pos)
                        spec = -(encoded_spec >> 1) if encoded_spec & 1 else encoded_spec >> 1
                        name = specs.get(spec, "")
                        world_id = cpu_stack[-1][3] if cpu_stack else None
                        process_id = cpu_stack[-1][4] if cpu_stack else None
                        if name == "UWorld_Tick":
                            world_id = len(worlds)
                            worlds.append({"begin": cycle, "end": None, "client": False, "server": False})
                        if name == "ProcessUntilTasksComplete":
                            process_id = len(processes)
                            processes.append({
                                "begin": cycle,
                                "end": None,
                                "exclusive": None,
                                "world_id": world_id,
                                "children_exclusive": Counter(),
                            })
                        cpu_stack.append([spec, cycle, 0, world_id, process_id])
                    elif cpu_stack:
                        spec, began, children, world_id, process_id = cpu_stack.pop()
                        name = specs.get(spec, "")
                        duration = max(0, cycle - began)
                        exclusive = max(0, duration - children)
                        if name == "UWorld_Tick" and world_id is not None:
                            worlds[world_id]["end"] = cycle
                        elif name == "GuLiCommanderPresentation_Interpolation" and world_id is not None:
                            worlds[world_id]["client"] = True
                        elif name == "GuLiCommanderMassStateTreeProcessor_0" and world_id is not None:
                            worlds[world_id]["server"] = True
                        if process_id is not None:
                            if name == "ProcessUntilTasksComplete":
                                processes[process_id]["end"] = cycle
                                processes[process_id]["exclusive"] = exclusive
                            else:
                                processes[process_id]["children_exclusive"][name] += exclusive
                        if cpu_stack:
                            cpu_stack[-1][2] += duration
                    last_cycle = cycle

    frames.sort()
    cutoff = frames[-1] - int(args.tail_seconds * FREQ)
    client_world_ids = {
        i for i, item in enumerate(worlds)
        if item["end"] is not None and item["begin"] >= cutoff and item["client"]
    }
    client_processes = [
        item for item in processes
        if item["end"] is not None and item["world_id"] in client_world_ids
    ]
    by_world: dict[int, list[dict]] = defaultdict(list)
    for item in client_processes:
        by_world[item["world_id"]].append(item)
    for items in by_world.values():
        items.sort(key=lambda x: x["begin"])
        for ordinal, item in enumerate(items):
            item["ordinal"] = ordinal
    client_processes.sort(key=lambda x: x["begin"])
    starts = [x["begin"] for x in client_processes]
    client_waits = []
    for wait in waits:
        if wait["thread_id"] != 2 or wait["end"] is None:
            continue
        i = bisect.bisect_right(starts, wait["begin"]) - 1
        if i >= 0 and wait["end"] <= client_processes[i]["end"]:
            wait["process_index"] = i
            client_waits.append(wait)
    grouped: dict[int, list[dict]] = defaultdict(list)
    for item in client_processes:
        grouped[item["ordinal"]].append(item)
    waits_by_process: dict[int, list[dict]] = defaultdict(list)
    for wait in client_waits:
        waits_by_process[wait["process_index"]].append(wait)

    def describe_process(index: int, item: dict) -> dict:
        nested = item["children_exclusive"].most_common(12)
        its_waits = waits_by_process[index]
        return {
            "begin_cycle": item["begin"],
            "end_cycle": item["end"],
            "world_id": item["world_id"],
            "ordinal": item["ordinal"],
            "inclusive_ms": round(ms(item["end"] - item["begin"]), 3),
            "exclusive_ms": round(ms(item["exclusive"]), 3),
            "task_wait_total_ms": round(ms(sum(x["end"] - x["begin"] for x in its_waits)), 3),
            "task_wait_count": len(its_waits),
            "top_nested_exclusive": [{"name": k, "ms": round(ms(v), 3)} for k, v in nested],
            "waits": [
                {
                    "begin_cycle": x["begin"],
                    "end_cycle": x["end"],
                    "duration_ms": round(ms(x["end"] - x["begin"]), 3),
                    "task_ids": x["task_ids"],
                    "ids_truncated": x["ids_truncated"],
                }
                for x in sorted(its_waits, key=lambda x: x["end"] - x["begin"], reverse=True)[:5]
            ],
        }

    def describe_group(items: list[dict]) -> dict:
        nested = Counter()
        for item in items:
            nested.update(item["children_exclusive"])
        return {
            "inclusive": stats([x["end"] - x["begin"] for x in items]),
            "exclusive": stats([x["exclusive"] for x in items]),
            "wait_total": stats([
                sum(w["end"] - w["begin"] for w in waits_by_process[starts.index(x["begin"])])
                for x in items
            ]),
            "nested_exclusive_per_call": [
                {"name": name, "mean_ms": round(ms(cycles) / len(items), 3)}
                for name, cycles in nested.most_common(20)
            ],
        }

    output = {
        "tail_seconds": args.tail_seconds,
        "cutoff_cycle": cutoff,
        "game_frames_selected": sum(x >= cutoff for x in frames),
        "client_world_count": len(client_world_ids),
        "client_process_count": len(client_processes),
        "client_wait_count": len(client_waits),
        "truncated_cpu_batches": truncated_cpu_batches,
        "truncated_wait_lists_all_threads": truncated_wait_lists,
        "thread_names": threads,
        "by_ordinal": {ordinal: describe_group(items) for ordinal, items in sorted(grouped.items())},
        "top_processes": [
            describe_process(client_processes.index(item), item)
            for item in sorted(client_processes, key=lambda x: x["end"] - x["begin"], reverse=True)[:20]
        ],
        "top_task_waits": [
            {
                "begin_cycle": wait["begin"],
                "end_cycle": wait["end"],
                "duration_ms": round(ms(wait["end"] - wait["begin"]), 3),
                "process_ordinal": client_processes[wait["process_index"]]["ordinal"],
                "process_begin_cycle": client_processes[wait["process_index"]]["begin"],
                "task_ids": wait["task_ids"],
                "ids_truncated": wait["ids_truncated"],
            }
            for wait in sorted(client_waits, key=lambda x: x["end"] - x["begin"], reverse=True)[:30]
        ],
    }
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2), encoding="utf-8")
    compact = {k: output[k] for k in (
        "game_frames_selected", "client_world_count", "client_process_count",
        "client_wait_count", "truncated_cpu_batches", "truncated_wait_lists_all_threads", "by_ordinal",
    )}
    print(json.dumps(compact, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
