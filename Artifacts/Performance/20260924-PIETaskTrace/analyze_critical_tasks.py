"""Follow TaskTrace prerequisite chains for the longest client tick waits."""

from __future__ import annotations

import argparse
import bisect
import json
import re
from collections import defaultdict
from pathlib import Path


EVENT_THREAD = re.compile(r'\bEVENT \[(\d+)\]')
TASK_TIME = re.compile(r'\bTimestamp=0x([0-9A-Fa-f]+)')
TASK_ID = re.compile(r'\bTaskId=(\d+)')
SUBSEQUENT_ID = re.compile(r'\bSubsequentId=(\d+)')
DEBUG_NAME = re.compile(r'\bDebugName="([^"]*)"')
EVENT_NAME = re.compile(r'TaskTrace\.([A-Za-z]+) :')
FREQ = 10_000_000


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace_text", type=Path)
    parser.add_argument("wait_summary", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--count", type=int, default=30)
    args = parser.parse_args()

    summary = json.loads(args.wait_summary.read_text(encoding="utf-8"))
    waits = summary["top_task_waits"][:args.count]
    windows = sorted((x["begin_cycle"] - 50_0000, x["end_cycle"] + 10_000) for x in waits)
    merged = []
    for begin, end in windows:
        if merged and begin <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((begin, end))
    starts = [x[0] for x in merged]
    tasks: dict[int, dict] = defaultdict(dict)
    incoming: dict[int, list[int]] = defaultdict(list)
    kept_events = 0

    with args.trace_text.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if "TaskTrace." not in line:
                continue
            ts_match = TASK_TIME.search(line)
            if not ts_match:
                continue
            timestamp = int(ts_match[1], 16)
            window_index = bisect.bisect_right(starts, timestamp) - 1
            if window_index < 0 or timestamp > merged[window_index][1]:
                continue
            event_match = EVENT_NAME.search(line)
            id_match = TASK_ID.search(line)
            if not event_match or not id_match:
                continue
            event = event_match[1]
            task_id = int(id_match[1])
            tid_match = EVENT_THREAD.search(line)
            tid = int(tid_match[1]) if tid_match else None
            kept_events += 1
            if event == "SubsequentAdded":
                subsequent_match = SUBSEQUENT_ID.search(line)
                if subsequent_match:
                    incoming[int(subsequent_match[1])].append(task_id)
            elif event in ("Launched", "Scheduled", "Started", "Finished", "Completed", "Destroyed", "Created"):
                row = tasks[task_id]
                row[event.lower()] = timestamp
                if event in ("Started", "Finished"):
                    row["thread_id"] = tid
                elif event == "Launched":
                    name_match = DEBUG_NAME.search(line)
                    row["name"] = name_match[1] if name_match else "<unknown>"

    def describe(task_id: int) -> dict:
        row = tasks.get(task_id, {})
        result = {"task_id": task_id, "name": row.get("name", "<unknown>"), "thread_id": row.get("thread_id")}
        for key in ("created", "launched", "scheduled", "started", "finished", "completed"):
            if key in row:
                result[key + "_cycle"] = row[key]
        if "started" in row and "finished" in row:
            result["execution_ms"] = round((row["finished"] - row["started"]) / FREQ * 1000, 3)
        return result

    output_waits = []
    for wait in waits:
        task_id = wait["task_ids"][0] if len(wait["task_ids"]) == 1 else None
        chain = []
        current = task_id
        seen = set()
        while current is not None and current not in seen and len(chain) < 16:
            seen.add(current)
            chain.append({**describe(current), "direct_predecessors": incoming.get(current, [])[:12]})
            preds = incoming.get(current, [])
            current = max(preds, key=lambda x: tasks.get(x, {}).get("completed", tasks.get(x, {}).get("finished", 0))) if preds else None
        output_waits.append({**wait, "critical_chain_waited_to_upstream": chain})

    result = {"kept_task_events": kept_events, "windows": merged, "waits": output_waits}
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    for wait in output_waits[:10]:
        print("wait", wait["duration_ms"], "ms ordinal", wait["process_ordinal"])
        for row in wait["critical_chain_waited_to_upstream"]:
            print(" ", row["task_id"], row["name"], "tid", row["thread_id"],
                  "run", row.get("execution_ms"), "preds", len(row["direct_predecessors"]))


if __name__ == "__main__":
    main()
