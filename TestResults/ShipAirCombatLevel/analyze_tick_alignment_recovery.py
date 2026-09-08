import json
import math
from pathlib import Path


root = Path("D:/UE5.7/test1/TestResults/ShipAirCombatLevel")
rows = [
    json.loads(line)
    for line in (root / "tick_alignment_recovery_timeseries.jsonl")
    .read_text(encoding="utf-8")
    .splitlines()
    if line.strip()
]


def distance(left, right):
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(left, right)))


def rotation_delta_degrees(left, right):
    dot = abs(sum(a * b for a, b in zip(left, right)))
    dot = max(-1.0, min(1.0, dot))
    return math.degrees(2.0 * math.acos(dot))


members = []
for index in range(25):
    samples = []
    for row in rows:
        item = row["items"][index]
        if max(abs(value) for value in item.get("scale", [1.0, 1.0, 1.0])) < 0.01:
            continue
        samples.append(
            {
                "time": row["game_time"],
                "location": item["location"],
                "rotation": item["rotation"],
            }
        )

    moving_intervals = 0
    maximum_stationary_seconds = 0.0
    maximum_turn_in_place_degrees = 0.0
    stationary_start = None
    total_distance = 0.0
    for previous, current in zip(samples, samples[1:]):
        elapsed = current["time"] - previous["time"]
        travelled = distance(previous["location"], current["location"])
        turned = rotation_delta_degrees(previous["rotation"], current["rotation"])
        total_distance += travelled
        if travelled > 5.0:
            moving_intervals += 1
            stationary_start = None
        else:
            if stationary_start is None:
                stationary_start = previous["time"]
            maximum_stationary_seconds = max(
                maximum_stationary_seconds, current["time"] - stationary_start
            )
            maximum_turn_in_place_degrees = max(maximum_turn_in_place_degrees, turned)

    members.append(
        {
            "index": index,
            "flight": index // 5,
            "member": index % 5,
            "visible_samples": len(samples),
            "moving_intervals": moving_intervals,
            "total_distance_cm": round(total_distance, 3),
            "maximum_stationary_seconds": round(maximum_stationary_seconds, 3),
            "maximum_turn_in_place_degrees_per_sample": round(
                maximum_turn_in_place_degrees, 3
            ),
            "first_location": samples[0]["location"] if samples else None,
            "last_location": samples[-1]["location"] if samples else None,
        }
    )

report = {
    "sample_count": len(rows),
    "first_game_time": rows[0]["game_time"] if rows else None,
    "last_game_time": rows[-1]["game_time"] if rows else None,
    "stationary_over_10_seconds": [
        member for member in members if member["maximum_stationary_seconds"] >= 10.0
    ],
    "members": members,
}
(root / "tick_alignment_recovery_analysis.json").write_text(
    json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
)
print(json.dumps(report["stationary_over_10_seconds"], ensure_ascii=False))
