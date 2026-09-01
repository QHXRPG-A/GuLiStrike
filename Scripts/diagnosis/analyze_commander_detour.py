"""Measure captured trajectories against an explicit navigation-query polyline.

Only reads provided JSON/CSV paths; a path corner alone does not prove collision
with an obstacle, nor does proximity prove the server used the identical path.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path


def xy_distance(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def segment_distance(point, start, end):
    dx, dy = end[0] - start[0], end[1] - start[1]
    square = dx * dx + dy * dy
    alpha = max(0.0, min(1.0, ((point[0] - start[0]) * dx + (point[1] - start[1]) * dy) / square)) if square else 0.0
    return xy_distance(point, (start[0] + alpha * dx, start[1] + alpha * dy))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--manifest", type=Path, required=True)
    p.add_argument("--navigation-probe", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    args = p.parse_args()
    nav = json.loads(args.navigation_probe.read_text(encoding="utf-8-sig"))["result"]["detours"][0]
    points = nav["points"]
    manifest = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    result = {"evidence_kind": "runtime_capture_with_readonly_navigation_query", "navigation_query": nav,
              "captures": [], "limitations": ["Query polyline is a reference, not a server path-tick dump.",
                  "The observed unit uses acceleration, slot correction and avoidance; proximity is not proof of identical path choice.",
                  "This covers one existing navigation detour, not all obstacle layouts."]}
    for cap in manifest["captures"]:
        source = Path(cap["analysis"]["source"])
        rows = list(csv.DictReader(source.open(encoding="utf-8-sig")))
        first_input = next(row for row in rows if row["event"] == "input")
        begin = float(first_input["local_seconds"])
        frames = [row for row in rows if row["event"] == "frame" and float(row["local_seconds"]) >= begin]
        metrics = {"mode": cap["mode"], "source": str(source), "command_id": int(first_input["command_id"]),
                   "duration_after_input_seconds": round(float(frames[-1]["local_seconds"]) - begin, 3)}
        for prefix in ("authoritative", "presented"):
            trajectory = [[float(row[f"{prefix}_{axis}"]) for axis in "xyz"] for row in frames]
            corners = []
            for corner in points[1:-1]:
                closest = min(range(len(trajectory)), key=lambda index: xy_distance(trajectory[index], corner))
                corners.append({"corner": corner, "closest_distance_cm": round(xy_distance(trajectory[closest], corner), 3),
                    "time_after_input_seconds": round(float(frames[closest]["local_seconds"]) - begin, 3),
                    "location": trajectory[closest]})
            metrics[prefix] = {"start_distance_from_query_origin_cm": round(xy_distance(trajectory[0], points[0]), 3),
                "end_distance_from_query_target_cm": round(xy_distance(trajectory[-1], points[-1]), 3),
                "maximum_distance_from_direct_query_segment_cm": round(max(segment_distance(point, points[0], points[-1]) for point in trajectory), 3),
                "travelled_distance_cm": round(sum(xy_distance(a, b) for a, b in zip(trajectory, trajectory[1:])), 3),
                "corners": corners}
        result["captures"].append(metrics)
    args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
