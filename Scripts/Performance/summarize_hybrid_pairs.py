"""Combine retained native frame samples from completed, comparable captures."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


def read(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def stats(values: list[float]) -> dict:
    assert values and all(math.isfinite(v) and v > 0 for v in values)
    ordered = sorted(values)
    return {"n": len(values), "mean_ms": sum(values) / len(values),
            "p95_ms": ordered[math.ceil(len(ordered) * .95) - 1],
            "peak_ms": ordered[-1]}


def summarize(root: Path, baseline: str, candidate: str, rounds: int) -> dict:
    merged = {baseline: [], candidate: []}
    rows = []
    identity = None
    for index in range(1, rounds + 1):
        row = {"round": index}
        for variant in (baseline, candidate):
            directory = root / f"r{index}-{variant}"
            capture, profile = read(directory / "capture.json"), read(directory / "profile-after.json")
            assert capture["valid"] and not capture["trace"], directory
            assert capture.get("purpose") == "paired_acceptance", directory
            assert capture.get("editor_window_required") == "minimized", directory
            assert capture.get("editor_window_state_valid"), directory
            window_frames = capture["editor_window_frames"]
            assert window_frames["minimized"] > 0 and window_frames["minimized"] == sum(window_frames.values()), directory
            assert profile["capture_valid"] and not capture["background_frames"], directory
            if identity is None:
                identity = capture["identity"]
            assert capture["identity"] == identity, "Map or binary identity changed"
            counter = profile["counters"]["Capture.FrameIntervalMs"]
            frames = counter["timed_frames"]
            assert len(frames) == counter["stored_active_frames"] == counter["calls"]
            assert len({f["frame"] for f in frames}) == len(frames), "Duplicate frame IDs"
            values = [f["value"] for f in frames]
            result = stats(values)
            assert abs(result["mean_ms"] - capture["frame_intervals"]["mean_ms"]) < .00001
            merged[variant].extend(values)
            row[variant] = result
        comparison = read(root / f"r{index}-comparison.json")
        assert comparison and all(c["valid"] and c["identity_equal"] and c["acceptance_eligible"]
                                  for c in comparison.values())
        row["mean_delta_ms"] = row[candidate]["mean_ms"] - row[baseline]["mean_ms"]
        row["p95_delta_ms"] = row[candidate]["p95_ms"] - row[baseline]["p95_ms"]
        rows.append(row)
    combined = {variant: stats(values) for variant, values in merged.items()}
    p95_not_worse = combined[candidate]["p95_ms"] <= combined[baseline]["p95_ms"]
    return {"method": "nearest-rank P95 of all retained native intervals, not average of round P95s",
            "rounds": rows, "combined": combined, "identity": identity,
            "combined_p95_not_worse": p95_not_worse,
            "comparison_rounds": rounds,
            "response_target_ms": 20,
            "full_goal_pass": False,
            "remaining_gates": ["local click to green feedback and actual movement strictly < 20ms, without RTT allowance",
                                "CPU/GPU attribution and workload invariants",
                                "separate player visual acceptance"]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", default="candidate")
    parser.add_argument("--rounds", type=int, default=3)
    args = parser.parse_args()
    assert args.rounds > 0 and args.baseline != args.candidate
    summary = summarize(args.directory, args.baseline, args.candidate, args.rounds)
    (args.directory / "native-paired-summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False))
