"""Summarize the three standalone Commander presentation CSV captures."""

import csv
import json
import math
import statistics
import sys
from pathlib import Path


PROJECT = Path("D:/UE5.7/test1")
DEFAULT_CAPTURES = [
    PROJECT / "Progress/Performance/Commander80000-Run1.csv",
    PROJECT / "Progress/Performance/Commander80000-Run2.csv",
    PROJECT / "Progress/Performance/Commander80000-Run3.csv",
]
OUTPUT = PROJECT / "Progress/Performance/Commander80000-Summary.json"
WARMUP_FRAMES = 1200
SAMPLE_FRAMES = 3600
FRAME_P95_GATE_MS = 16.67
PRESENTATION_P95_GATE_MS = 1.0


def parse_float(value):
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        return None
    return parsed if math.isfinite(parsed) else None


def percentile(values, fraction):
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, math.ceil(len(ordered) * fraction) - 1))
    return ordered[index]


def summarize(values):
    return {
        "minimum": min(values),
        "median": statistics.median(values),
        "p95": percentile(values, 0.95),
        "maximum": max(values),
    }


def summarize_capture(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        fieldnames = reader.fieldnames or []
        presentation_columns = [
            name
            for name in fieldnames
            if name.endswith("/RebuildLocalInstances")
            and name.startswith("GuLiCommanderPresentation/")
        ]
        if len(presentation_columns) != 1:
            raise RuntimeError(
                f"{path}: expected one Presentation timing column, got "
                f"{presentation_columns}"
            )
        presentation_column = presentation_columns[0]
        rows = []
        for row in reader:
            frame_time = parse_float(row.get("FrameTime"))
            if frame_time is None:
                continue
            rows.append(row)

    if len(rows) < WARMUP_FRAMES + SAMPLE_FRAMES:
        raise RuntimeError(
            f"{path}: expected at least {WARMUP_FRAMES + SAMPLE_FRAMES} "
            f"frames, got {len(rows)}"
        )
    rows = rows[WARMUP_FRAMES : WARMUP_FRAMES + SAMPLE_FRAMES]

    columns = {
        "frame_time_ms": "FrameTime",
        "game_thread_ms": "GameThreadTime",
        "gpu_ms": "GPUTime",
        "presentation_rebuild_ms": presentation_column,
        "rhi_draw_calls": "RHI/DrawCalls",
        "rhi_primitives_drawn": "RHI/PrimitivesDrawn",
    }
    metrics = {}
    for output_name, column_name in columns.items():
        values = [parse_float(row.get(column_name)) for row in rows]
        values = [value for value in values if value is not None]
        if len(values) != SAMPLE_FRAMES:
            raise RuntimeError(
                f"{path}: {column_name} has {len(values)}/{SAMPLE_FRAMES} numeric rows"
            )
        metrics[output_name] = summarize(values)

    metrics["sample_duration_seconds"] = sum(
        parse_float(row["FrameTime"]) for row in rows
    ) / 1000.0
    return {
        "file": str(path),
        "captured_frames": WARMUP_FRAMES + SAMPLE_FRAMES,
        "discarded_warmup_frames": WARMUP_FRAMES,
        "sampled_frames": SAMPLE_FRAMES,
        "metrics": metrics,
        "gates": {
            "frame_p95_le_16_67_ms": metrics["frame_time_ms"]["p95"]
            <= FRAME_P95_GATE_MS,
            "presentation_p95_le_1_ms": metrics["presentation_rebuild_ms"]["p95"]
            <= PRESENTATION_P95_GATE_MS,
        },
    }


capture_paths = [Path(argument) for argument in sys.argv[1:]] or DEFAULT_CAPTURES
captures = [summarize_capture(path) for path in capture_paths]
report = {
    "environment": {
        "target": "UnrealEditor Development -game (uncooked standalone game mode)",
        "map": "/Game/Maps/LVL_CommanderMassPrototype",
        "resolution": [1920, 1080],
        "screen_percentage": 100,
        "vsync": False,
        "maximum_fps": 120,
        "quality": "Epic (all sg.* quality groups = 3)",
        "camera_arm_cm": 80000,
    },
    "captures": captures,
    "aggregate": {
        "median_of_frame_medians_ms": statistics.median(
            capture["metrics"]["frame_time_ms"]["median"]
            for capture in captures
        ),
        "worst_frame_p95_ms": max(
            capture["metrics"]["frame_time_ms"]["p95"]
            for capture in captures
        ),
        "median_of_presentation_medians_ms": statistics.median(
            capture["metrics"]["presentation_rebuild_ms"]["median"]
            for capture in captures
        ),
        "worst_presentation_p95_ms": max(
            capture["metrics"]["presentation_rebuild_ms"]["p95"]
            for capture in captures
        ),
    },
    "gates": {
        "all_frame_p95_le_16_67_ms": all(
            capture["gates"]["frame_p95_le_16_67_ms"] for capture in captures
        ),
        "all_presentation_p95_le_1_ms": all(
            capture["gates"]["presentation_p95_le_1_ms"] for capture in captures
        ),
        "server_regression_le_5_percent": "not_evaluated_without_prechange_server_capture",
    },
}
report["passed_client_gates"] = bool(
    report["gates"]["all_frame_p95_le_16_67_ms"]
    and report["gates"]["all_presentation_p95_le_1_ms"]
)

OUTPUT.parent.mkdir(parents=True, exist_ok=True)
with OUTPUT.open("w", encoding="utf-8") as stream:
    json.dump(report, stream, ensure_ascii=False, indent=2)

print(OUTPUT)
