"""Offline per-queue GPU analysis inside a retained native hotspot QPC window.

GPU Frame scopes can include queue gaps and waits. Queue times overlap and must
not be added together or to CPU frame time.
"""
from __future__ import annotations

import argparse
import collections
import csv
import json
import math
from pathlib import Path
import statistics

import export_review_traces as export
from export_commander_pose_traces import trace_clock


def distribution(values):
    v = sorted(values)
    return {"n": len(v), "mean_ms": statistics.mean(v), "p95_ms": v[math.ceil(.95*len(v))-1],
            "peak_ms": v[-1]} if v else {"n": 0}


def gpu_frames(path, begin, end):
    frames = collections.defaultdict(list)
    stack, active, current_thread = [], None, None

    def finish():
        node = stack.pop()
        if stack:
            stack[-1]["own"] -= node["ms"]
        if active is not None:
            active["own"][node["name"]] += max(0., node["own"])
            if not any(p["name"] == node["name"] for p in stack):
                active["inclusive"][node["name"]] += node["ms"]

    with path.open(encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            thread, name, depth = row["ThreadName"], row["TimerName"], int(row["Depth"])
            if thread != current_thread:
                while stack:
                    finish()
                active, current_thread = None, thread
            while stack and stack[-1]["depth"] >= depth:
                finish()
            start, stop, duration = float(row["StartTime"]), float(row["EndTime"]), float(row["Duration"])*1000
            if not math.isfinite(duration):
                continue
            if name == "Frame":
                active = None
                if start >= begin and stop <= end:
                    active = {"start": start, "end": stop, "ms": duration,
                              "own": collections.defaultdict(float), "inclusive": collections.defaultdict(float)}
                    frames[thread].append(active)
            elif not stack:
                active = None
            if active is not None:
                stack.append({"name": name, "depth": depth, "ms": duration, "own": duration})
        while stack:
            finish()
    return frames


def analyze(root, insights, command_diagnostic=False):
    profile = json.loads((root / "profile-after.json").read_text(encoding="utf-8"))
    capture = json.loads((root / "capture.json").read_text(encoding="utf-8"))
    diagnostic = (command_diagnostic and capture.get("purpose") == "command_diagnostic"
                  and capture.get("diagnostic_valid"))
    assert capture["valid"] or diagnostic, "This command requires a valid capture or an explicitly labeled command diagnostic"
    clock = trace_clock(root / "session.utrace")
    work = profile["hotspot_workload"]
    begin, end = [work[k]/profile["qpc_frequency"]-clock["qpc_origin_seconds"] for k in ("start_qpc", "end_qpc")]
    out = root / "analysis-gpu"
    out.mkdir(exist_ok=True)
    events = out / "gpu-queue-events.csv"
    export.INSIGHTS = insights
    if not events.exists():
        export.analyze(root, [
            f'TimingInsights.ExportThreads "{(out/"threads.csv").as_posix()}"',
            f'TimingInsights.ExportTimingEvents "{events.as_posix()}" -threads=GPU0-Graphics0,GPU0-Compute0,GPU0-Copy0 '
            f'-startTime={begin:.9f} -endTime={end:.9f} '
            '-columns=ThreadId,ThreadName,TimerId,TimerName,StartTime,EndTime,Duration,Depth'], out)
    queues = {}
    for name, frames in gpu_frames(events, begin, end).items():
        names = set().union(*(f["inclusive"].keys() for f in frames))
        timers = [{"name": timer, "inclusive": distribution([f["inclusive"].get(timer, 0.) for f in frames]),
                   "exclusive_mean_ms": statistics.mean(f["own"].get(timer, 0.) for f in frames)} for timer in names]
        worst = max(frames, key=lambda f: f["ms"])
        queues[name] = {"frame_scope": distribution([f["ms"] for f in frames]),
                        "timers": sorted(timers, key=lambda t: t["inclusive"]["mean_ms"], reverse=True),
                        "worst_frame": worst}
    assert queues, "No GPU queue events were exported"
    result = {"window": [begin, end], "queues": queues,
              "purpose": capture.get("purpose"), "acceptance_eligible": bool(capture["valid"]),
              "method": "Complete GPU Frame scopes within the native QPC window; inclusive scopes counted once per name. Queue spans can contain waits; CPU, queues and parent/child scopes are not additive."}
    (out / "summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--command-diagnostic", action="store_true",
                        help="Analyze command-stage diagnostics without making them acceptance eligible")
    parser.add_argument("--insights", type=Path,
                        default=Path("D:/UnrealEngine-5.7/Engine/Binaries/Win64/UnrealInsights.exe"))
    args = parser.parse_args()
    result = analyze(args.capture.resolve(), args.insights, args.command_diagnostic)
    print(json.dumps({name: value["frame_scope"] for name, value in result["queues"].items()}, ensure_ascii=False))
