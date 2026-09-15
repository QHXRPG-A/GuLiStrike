"""Summarize the explicitly requested independent-process movement stress captures."""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
from statistics import mean


def percentile(values: list[float], quantile: float) -> float | None:
    if not values:
        return None
    return sorted(values)[max(0, math.ceil(len(values) * quantile) - 1)]


def stats(values: list[float]) -> dict:
    return {"n": len(values), "mean": mean(values) if values else None,
            "p50": percentile(values, .5), "p95": percentile(values, .95),
            "p99": percentile(values, .99), "max": max(values) if values else None}


def read_engine(path: Path) -> dict:
    if not path.exists():
        return {"error": "engine CSV missing"}
    with path.open(encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.reader(stream))
    if not rows:
        return {"error": "engine CSV empty"}
    # UE appends a complete header when new counters appear during a capture,
    # followed by metadata (including numeric values such as 1920/1080).
    header = next(row for row in reversed(rows) if row and row[0] == "EVENTS")
    frame_index = header.index("FrameTime")
    frame_rows = []
    for row in rows[1:]:
        if frame_index >= len(row):
            continue
        try:
            frame_time = float(row[frame_index])
        except ValueError:
            continue
        if math.isfinite(frame_time):
            frame_rows.append(row)
    relevant = [(i, name) for i, name in enumerate(header)
                if name in ("FrameTime", "GameThread", "RenderThread", "RHIThread", "GPU", "GameThreadTime", "RenderThreadTime", "RHIThreadTime", "GPUTime", "GameThreadTime_CriticalPath", "RenderThreadTime_CriticalPath")
                or "GuLiCommander" in name or name.startswith(("GPU/", "Exclusive/GameThread/", "Exclusive/RenderThread/", "RenderThreadIdle/", "Slate/GameThread/", "View/"))]
    result = {}
    for index, name in relevant:
        values = []
        for row in frame_rows:
            if index >= len(row):
                continue
            try:
                value = float(row[index])
            except ValueError:
                continue
            if math.isfinite(value):
                values.append(value)
        result[name] = stats(values)
    return result


def summarize(path: Path) -> dict:
    summary = json.loads((path / "summary.json").read_text(encoding="utf-8-sig"))
    result = {"directory": str(path), **summary}
    frames_path = path / "frames.csv"
    if not frames_path.exists() or summary["error"]:
        return result
    with frames_path.open(encoding="utf-8-sig", newline="") as stream:
        rows = [{key: float(value) for key, value in row.items()} for row in csv.DictReader(stream)]
    rows = [row for row in rows if row["seconds"] >= 1.0]
    for key in ("frame_ms", "sim_ms", "combat_ms", "alive", "ordered_moving", "observed_moving", "blocked", "arrived", "in_bytes_s", "out_bytes_s", "pose_age_ms"):
        result[key] = stats([row[key] for row in rows])
    step_rows = [row for row in rows if row["sim_steps"] > 0]
    result["sampled_fixed_step_ms"] = stats([row["sim_ms"] / row["sim_steps"] for row in step_rows])
    result["multi_step_frames"] = sum(row["sim_steps"] > 1 for row in step_rows)
    result["engine"] = read_engine(path / "engine.csv")
    if summary["mode"] == "client":
        camera_speed = result["engine"].get("View/Speed", {}).get("max")
        result["camera_stationary_in_capture"] = camera_speed is not None and camera_speed <= .01
    duration = summary["measured_seconds"]
    if duration > 0:
        if summary["mode"] == "server":
            result["effective_sim_hz"] = summary["simulation_steps"] / duration
            result["movement_updates_per_unit_second"] = summary["movement_updates"] / duration / summary["requested_population"]
            result["planning_ms"] = stats(summary.get("server_plan_commit_ms", []))
        else:
            result["accepted_pose_hz"] = summary["accepted_pose_frames"] / duration
    result["actual_moving_p05"] = percentile([row["observed_moving"] for row in rows], .05)
    result["movement_load_valid"] = (bool(rows) and result["actual_moving_p05"] >= .95 * summary["requested_population"]
                                      and result["alive"]["p50"] == summary["requested_population"])
    if summary["mode"] == "server":
        result["server_budget_pass"] = (result["movement_load_valid"] and summary.get("dropped_steps") == 0
                                         and result.get("effective_sim_hz", 0) >= 9.9
                                         and (result["frame_ms"]["p95"] or math.inf) <= 100
                                         and (result["sampled_fixed_step_ms"]["p95"] or math.inf) <= 50)
    else:
        result["client_60fps_pass"] = result["movement_load_valid"] and (result["frame_ms"]["p95"] or math.inf) <= 1000 / 60
        result["client_30fps_pass"] = result["movement_load_valid"] and (result["frame_ms"]["p95"] or math.inf) <= 1000 / 30
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    results = [summarize(path.parent) for path in sorted(args.directory.glob("*/summary.json"))]
    launch_path = args.directory / "launch.json"
    launch = json.loads(launch_path.read_text(encoding="utf-8-sig")) if launch_path.exists() else {}
    for row in results:
        row["headless_clients"] = launch.get("HeadlessClients", False)
        if row["mode"] == "client" and row["headless_clients"]:
            row["client_60fps_pass"] = None
            row["client_30fps_pass"] = None
    (args.directory / "analysis.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    for row in results:
        compact = {key: row.get(key) for key in ("mode", "requested_population", "error", "measured_seconds", "effective_sim_hz", "movement_updates_per_unit_second", "actual_moving_p05", "movement_load_valid", "server_budget_pass", "client_60fps_pass", "client_30fps_pass", "accepted_pose_hz")}
        compact["frame_p95_ms"] = row.get("frame_ms", {}).get("p95")
        compact["fixed_step_p95_ms"] = row.get("sampled_fixed_step_ms", {}).get("p95")
        compact["planning_p95_ms"] = row.get("planning_ms", {}).get("p95")
        compact["game_thread"] = row.get("engine", {}).get("GameThreadTime")
        print(json.dumps(compact, ensure_ascii=False))


if __name__ == "__main__":
    main()
