"""Find CPU scopes executed inside selected TaskTrace task intervals."""

from __future__ import annotations

import argparse
import bisect
import json
import re
from collections import Counter, defaultdict
from pathlib import Path


CPU_SPEC = re.compile(r'CpuProfiler\.EventSpec : Id=(\d+) Name="([^"]*)"')
EVENT_THREAD = re.compile(r'\bEVENT \[(\d+)\]')
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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace_text", type=Path)
    parser.add_argument("critical_chains", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    chains = json.loads(args.critical_chains.read_text(encoding="utf-8"))
    tasks: dict[int, dict] = {}
    windows: dict[int, list[tuple[int, int, int]]] = defaultdict(list)
    for wait in chains["waits"]:
        for task in wait["critical_chain_waited_to_upstream"]:
            if "started_cycle" not in task or "finished_cycle" not in task or task.get("thread_id") is None:
                continue
            task_id = task["task_id"]
            tasks[task_id] = {**task, "scopes_exclusive": Counter(), "scopes_inclusive": Counter()}
            windows[task["thread_id"]].append((task["started_cycle"], task["finished_cycle"], task_id))
    for items in windows.values():
        items.sort()
    starts = {tid: [x[0] for x in items] for tid, items in windows.items()}
    specs: dict[int, str] = {}
    last: dict[int, int] = defaultdict(int)
    stacks: dict[int, list[list[int]]] = defaultdict(list)
    matched_scopes = 0

    with args.trace_text.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if "CpuProfiler.EventSpec :" in line:
                match = CPU_SPEC.search(line)
                if match:
                    specs[int(match[1])] = match[2]
            elif "CpuProfiler.EventBatchV3 : Data=0x[" in line:
                tid_match = EVENT_THREAD.search(line)
                if not tid_match:
                    continue
                tid = int(tid_match[1])
                if tid not in windows:
                    continue
                start = line.find("Data=0x[") + len("Data=0x[")
                end = line.find("]", start)
                data = bytes.fromhex(line[start:end])
                pos = 0
                stack = stacks[tid]
                clock = last[tid]
                while pos < len(data):
                    encoded, pos = varint(data, pos)
                    cycle = encoded >> 2
                    if cycle < clock:
                        cycle += clock
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
                        if spec >= 0:
                            i = bisect.bisect_right(starts[tid], began) - 1
                            if i >= 0:
                                task_begin, task_end, task_id = windows[tid][i]
                                if cycle <= task_end:
                                    name = specs.get(spec, f"#{spec}")
                                    tasks[task_id]["scopes_exclusive"][name] += exclusive
                                    tasks[task_id]["scopes_inclusive"][name] += duration
                                    matched_scopes += 1
                        if stack:
                            stack[-1][2] += duration
                    clock = cycle
                last[tid] = clock

    for task in tasks.values():
        task["top_exclusive"] = [
            {"name": name, "ms": round(cycles / FREQ * 1000, 3)}
            for name, cycles in task.pop("scopes_exclusive").most_common(15)
        ]
        task["top_inclusive"] = [
            {"name": name, "ms": round(cycles / FREQ * 1000, 3)}
            for name, cycles in task.pop("scopes_inclusive").most_common(15)
        ]
    output = {"matched_scopes": matched_scopes, "tasks": tasks}
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2), encoding="utf-8")
    for wait in chains["waits"][:10]:
        print("wait", wait["duration_ms"], "ms")
        for item in wait["critical_chain_waited_to_upstream"]:
            task = tasks.get(item["task_id"])
            if task:
                print(" ", item["task_id"], "tid", item["thread_id"],
                      "run", item["execution_ms"], "scopes", task["top_exclusive"][:8])


if __name__ == "__main__":
    main()
