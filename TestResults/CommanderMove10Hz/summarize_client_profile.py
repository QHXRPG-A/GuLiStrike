"""Reproduce the 2026-09-15 client diagnosis from CSV and Insights exports."""
from __future__ import annotations

import csv
import json
import math
from pathlib import Path

BASE = Path(__file__).resolve().parent
CAPTURES = (
    ("locked-profile-n1200-c1", "client1"),
    ("locked-profile-n1200-c2", "client1"),
    ("locked-profile-n1200-c2", "client2"),
)


def read_csv(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def p95(values):
    return sorted(values)[math.ceil(len(values) * .95) - 1]


def main():
    captures = []
    for run, client in CAPTURES:
        directory = BASE / run / client
        exports = directory / "insights"
        analysis = next(row for row in json.loads((BASE / run / "analysis.json").read_text())
                        if Path(row["directory"]).name == client)
        # UE 5.7's aggregation exporter includes all GPU queues even with a
        # CPU -threads filter. The separate GPU-only export identifies those
        # exact rows. Never rank them as CPU work.
        gpu_rows = {tuple(row.items()) for row in read_csv(exports / "timers-gpu.csv")}
        game_rows = [row for row in read_csv(exports / "timers-gamethread.csv")
                     if tuple(row.items()) not in gpu_rows]
        frame_count = int(next(row["Count"] for row in game_rows if row["Name"] == "FEngineLoop::Tick"))
        cpu = {}
        for thread in ("gamethread", "renderthread", "rhithread"):
            rows = [row for row in read_csv(exports / f"timers-{thread}.csv")
                    if tuple(row.items()) not in gpu_rows]
            cpu[thread] = [
                {"name": row["Name"], "count": int(row["Count"]),
                 "inclusive_ms_per_game_frame": float(row["Incl"]) * 1000 / frame_count,
                 "exclusive_ms_per_game_frame": float(row["Excl"]) * 1000 / frame_count,
                 "mean_inclusive_ms_per_call": float(row["I.Avg"]) * 1000,
                 "mean_exclusive_ms_per_call": float(row["E.Avg"]) * 1000}
                for row in sorted(rows, key=lambda row: float(row["Excl"]), reverse=True)[:30]
            ]
        event_groups = {}
        for row in read_csv(exports / "events-project.csv"):
            if row["TimerName"] in ("GuLiCommanderPresentation_RebuildLocalInstances", "GuLiCommanderHealthBars_UpdateInstances") or (
                    row["TimerName"].startswith("GuLiCommanderMiniMapWidget ") and row["TimerName"].endswith("_Paint")):
                event_groups.setdefault(row["TimerName"], []).append(float(row["Duration"]) * 1000)
        captures.append({
            "run": run, "client": client, "capture_seconds": analysis["measured_seconds"],
            "trace": str(directory / "client.utrace"),
            "trace_bytes": (directory / "client.utrace").stat().st_size,
            "trace_game_frames": frame_count,
            "camera_stationary_in_capture": analysis["camera_stationary_in_capture"],
            "actual_moving_p05": analysis["actual_moving_p05"],
            "accepted_pose_hz": analysis["accepted_pose_hz"],
            "frame_ms": analysis["frame_ms"], "engine": analysis["engine"],
            "cpu_top_exclusive": cpu,
            "project_events": {name: {"calls": len(values), "mean_ms_per_call": sum(values) / len(values),
                                      "p95_ms_per_call": p95(values)} for name, values in event_groups.items()},
            "presentation_callees": read_csv(exports / "callees-presentation.csv"),
            "analysis_tail_warnings": [line for line in (exports / "export.log").read_text(encoding="utf-8-sig").splitlines()
                                       if "Transport buffers are not empty" in line],
        })
    result = {
        "date": "2026-09-15", "units": "CPU/GPU timings in milliseconds; counts are both teams combined",
        "runtime": "D:/UnrealEngine-5.7; source Editor -game, Development, uncooked",
        "offline_trace_reader": "C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealInsights.exe (5.7.4)",
        "scope": "1200-unit online spawning, 40s, fixed camera, 1080p Epic; one and two clients on the same machine",
        "interpretation": [
            "Inclusive and exclusive scopes overlap across levels; do not add them or their percentiles.",
            "Exclusive subtracts instrumented children only; it still contains uninstrumented helpers and may include waiting.",
            "Trace export totals divided by FEngineLoop::Tick count are average per game frame, not per-call latency.",
            "CSV timings use only numeric FrameTime rows and the final full EVENTS header; metadata is excluded.",
            "GPU rows included by the engine's CPU statistics exporter are removed by matching the separate GPU-only export.",
            "Trace starts/stops mid-pipeline; boundary frame counts can differ by one or two. Small tail buffer warnings are retained.",
            "These are instrumented CPU/GPU timelines, not RenderDoc draw-call captures or independent-machine limits.",
        ],
        "captures": captures,
    }
    (BASE / "client-profile-locked.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    for row in captures:
        print(row["run"], row["client"], "stationary", row["camera_stationary_in_capture"])
        print(json.dumps(row["project_events"], ensure_ascii=False))


if __name__ == "__main__":
    main()
