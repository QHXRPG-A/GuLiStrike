"""Summarize matched Commander predictive-avoidance CSV captures."""

from __future__ import annotations

import argparse
import json
import math
import statistics
from pathlib import Path

from analyze_commander_ring_ab import read_capture


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    return ordered[math.ceil(len(ordered) * fraction) - 1]


def summarize(values: list[float]) -> dict:
    mean = statistics.fmean(values)
    return {
        "samples": len(values),
        "mean": mean,
        "median": statistics.median(values),
        "p95": percentile(values, 0.95),
        "minimum": min(values),
        "maximum": max(values),
        "stddev": statistics.pstdev(values),
    }


def read(path: Path, stage: str, warmup: int, sample: int) -> dict:
    fake_name = Path(f"capture-{stage}.csv")
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        parsed = read_capture(stream, fake_name, warmup, sample)
    return {
        "file": str(path),
        "duration_seconds": sum(parsed["values"]["frame_time_ms"]) / 1000.0,
        "metrics": {name: summarize(values) for name, values in parsed["values"].items()},
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--capture", action="append", required=True,
                        help="Scenario:shared-suffix, or scenario:idle-suffix:moving-suffix")
    parser.add_argument("--warmup", type=int, default=120)
    parser.add_argument("--sample", type=int, default=600)
    args = parser.parse_args()
    result = {
        "warmup_frames": args.warmup,
        "sample_frames": args.sample,
        "scenarios": {},
    }
    for item in args.capture:
        parts = item.split(":")
        if len(parts) == 2:
            scenario, idle_suffix = parts
            moving_suffix = idle_suffix
        elif len(parts) == 3:
            scenario, idle_suffix, moving_suffix = parts
        else:
            raise ValueError(f"Invalid --capture value: {item}")
        idle = read(args.directory / f"idle_{scenario}_{idle_suffix}.csv", "before",
                    args.warmup, args.sample)
        moving = read(args.directory / f"moving_{scenario}_{moving_suffix}.csv", "after",
                      args.warmup, args.sample)
        comparisons = {}
        for metric in sorted(idle["metrics"].keys() & moving["metrics"].keys()):
            old = idle["metrics"][metric]["mean"]
            new = moving["metrics"][metric]["mean"]
            comparisons[metric] = {
                "idle_mean": old,
                "moving_mean": new,
                "delta": new - old,
                "percent": ((new - old) / old * 100.0) if old else None,
            }
        idle_frame = idle["metrics"]["frame_time_ms"]["mean"]
        moving_frame = moving["metrics"]["frame_time_ms"]["mean"]
        result["scenarios"][scenario] = {
            "idle_suffix": idle_suffix,
            "moving_suffix": moving_suffix,
            "idle": idle,
            "moving": moving,
            "comparison": comparisons,
            "derived": {
                "idle_fps_from_mean_frame_time": 1000.0 / idle_frame,
                "moving_fps_from_mean_frame_time": 1000.0 / moving_frame,
                "fps_change": 1000.0 / moving_frame - 1000.0 / idle_frame,
                "fps_percent": ((idle_frame / moving_frame) - 1.0) * 100.0,
            },
        }
    output = args.directory / "frame_summary.json"
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
