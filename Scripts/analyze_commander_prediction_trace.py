"""Summarize explicit PredictionTrace CSVs; never scans the project or Saved.

python Scripts/analyze_commander_prediction_trace.py trace.csv [trace2.csv] --out report.json
The CSV is runtime evidence. Synthetic fixtures must pass --synthetic so reports
cannot be mistaken for PIE measurements. This tool reports observations, not a gate.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from collections import Counter
from pathlib import Path


def number(row: dict[str, str], key: str) -> float:
    value = float(row[key])
    if not math.isfinite(value):
        raise ValueError(f"Non-finite {key}: {row[key]}")
    return value


def magnitude(row: dict[str, str], prefix: str) -> float:
    return math.sqrt(sum(number(row, f"{prefix}_{axis}") ** 2 for axis in "xyz"))


def summarize_command(command: list[dict[str, str]]) -> dict:
    input_row = next(row for row in command if row["event"] == "input")
    input_time = number(input_row, "local_seconds")
    mouse_input = next((row for row in command if row["event"] == "mouse_input"), None)
    ack = next((row for row in command if row["event"].startswith("ack_")), None)
    order_id = ack["order_id"] if ack else None
    first_sample = next((row for row in command if row["event"] == "sample_arrival"
                         and row["order_id"] == order_id), None)
    handoff = next((row for row in command if row["event"] == "render_handoff"), None)
    resolve = next((row for row in command if row["event"] == "resolve"), None)
    frames = [row for row in command if row["event"] == "frame"]
    measured = [row for row in frames if row["has_speed"] == "1"]
    reverse = [row for row in measured if number(row, "signed_presented_cm_s") < -1]
    visual_only = [row for row in reverse if number(row, "signed_authoritative_cm_s") >= -1]
    reverse_distance = 0.0
    visual_only_distance = 0.0
    max_frame_step = 0.0
    for previous, current in zip(frames, frames[1:]):
        delta = number(current, "local_seconds") - number(previous, "local_seconds")
        if delta <= 0:
            continue
        speed = number(current, "signed_presented_cm_s")
        backstep = max(0.0, -speed * delta)
        reverse_distance += backstep
        if number(current, "signed_authoritative_cm_s") >= -1:
            visual_only_distance += backstep
        max_frame_step = max(max_frame_step, math.sqrt(sum(
            (number(current, f"presented_{axis}") - number(previous, f"presented_{axis}")) ** 2
            for axis in "xyz")))

    def delay(row):
        return round((number(row, "local_seconds") - input_time) * 1000, 3) if row else None

    return {
        "soldier_id": int(input_row["soldier_id"]),
        "command_id": int(input_row["command_id"]),
        "has_mouse_input_hook": mouse_input is not None,
        "mouse_to_prediction_begin_ms": round((input_time - number(mouse_input, "local_seconds")) * 1000, 3)
        if mouse_input else None,
        "ack_result": ack["event"] if ack else None,
        "order_id": int(order_id) if order_id else None,
        "ack_after_input_ms": delay(ack),
        "first_matching_sample_after_input_ms": delay(first_sample),
        "render_handoff_after_input_ms": delay(handoff),
        "resolve_after_input_ms": delay(resolve),
        "resolve_before_render_handoff_ms": round(
            (number(handoff, "local_seconds") - number(resolve, "local_seconds")) * 1000, 3
        ) if resolve and handoff else None,
        "frame_count": len(frames),
        "speed_sample_count": len(measured),
        "reverse_frame_count": len(reverse),
        "presentation_only_reverse_frame_count": len(visual_only),
        "minimum_signed_presented_cm_s": min(
            (number(row, "signed_presented_cm_s") for row in measured), default=None),
        "minimum_signed_authoritative_cm_s": min(
            (number(row, "signed_authoritative_cm_s") for row in measured), default=None),
        "total_reverse_distance_cm": round(reverse_distance, 3),
        "presentation_only_reverse_distance_cm": round(visual_only_distance, 3),
        "maximum_frame_step_cm": round(max_frame_step, 3),
        "maximum_displacement_offset_cm": round(max(
            (magnitude(row, "offset") for row in frames), default=0.0), 3),
        "maximum_requested_offset_cm": round(max(
            (magnitude(row, "requested_offset") for row in frames), default=0.0), 3),
        "prediction_replacement_count": sum(row["event"] == "replaced_prediction" for row in command),
        "hard_snap_count": sum(row["event"] == "hard_snap" for row in command),
    }


def analyze(path: Path, synthetic: bool = False) -> dict:
    raw = path.read_bytes()
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        required = {"event", "local_seconds", "command_id", "order_id", "has_speed",
                    "signed_presented_cm_s", "signed_authoritative_cm_s", "displacement_disabled"}
        if not required <= set(reader.fieldnames or []):
            raise ValueError(f"{path}: not a PredictionTrace CSV")
        rows = list(reader)
    command_ids = list(dict.fromkeys(row["command_id"] for row in rows if row["event"] == "input"))
    return {
        "source": str(path.resolve()),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "evidence_kind": "synthetic_analysis_fixture" if synthetic else "runtime_capture",
        "row_count": len(rows),
        "event_counts": dict(Counter(row["event"] for row in rows)),
        "modes": sorted({"no_offset" if row["displacement_disabled"] == "1" else "baseline" for row in rows}),
        "net_modes": sorted({int(row["net_mode"]) for row in rows}),
        "has_prediction_begin_hook": bool(command_ids),
        "mouse_input_count": sum(row["event"] == "mouse_input" for row in rows),
        "mouse_inputs_not_dispatched": sorted({int(row["command_id"]) for row in rows
            if row["event"] == "mouse_input" and row["command_id"] not in command_ids}),
        "capture_stopped_normally": bool(rows and rows[-1]["event"] == "stop"),
        "commands": [summarize_command([row for row in rows if row["command_id"] == command_id])
                     for command_id in command_ids],
        "limitations": [
            "input means BeginPredictedMove/local dispatch; mouse_input is a separate controller hook. Neither proves OS injection.",
            "Authoritative means the client evaluated server timeline, not a direct server transform sample.",
            "Signed speed is projected onto the latest clicked target direction; legitimate turns can be negative.",
            "No pass/fail threshold is inferred; compare matching scenarios, map, selection and network conditions.",
        ],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, nargs="+")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--synthetic", action="store_true")
    args = parser.parse_args()
    result = {"schema_version": 1, "captures": [analyze(path, args.synthetic) for path in args.csv]}
    output = json.dumps(result, ensure_ascii=False, indent=2)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(output + "\n", encoding="utf-8")
    else:
        print(output)


if __name__ == "__main__":
    main()
