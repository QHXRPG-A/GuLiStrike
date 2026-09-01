"""Validate PredictionTrace's source contract and retain command-boundary motion.

Only explicit source files / CSV paths are read. Nothing searches Saved or starts UE.
This supplements the original analyzer without changing compiled runtime code.
"""
from __future__ import annotations

import argparse
import csv
import importlib.util
import json
import math
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Source/GuLiStrike/Commander/Presentation/GuLiCommanderPresentationActor"
EXPECTED_EVENTS = ["start", "input", "mouse_input", "replaced_prediction", "ack_accepted",
                   "ack_rejected", "sample_arrival", "render_handoff", "resolve", "frame",
                   "hard_snap", "network_reset", "stop"]


def review_contract() -> dict:
    source = SOURCE.with_suffix(".cpp").read_text(encoding="utf-8-sig")
    header = SOURCE.with_suffix(".h").read_text(encoding="utf-8-sig")
    csv_literal = re.search(r'FString Csv\(TEXT\("([^"\n]+)"\)\);', source).group(1)
    columns = csv_literal.removesuffix(r"\n").split(",")
    names_block = re.search(r"const TCHAR\* EventNames\[\] = \{(.*?)\};", source, re.S).group(1)
    names = re.findall(r'TEXT\("([a-z_]+)"\)', names_block)
    enum = re.search(r"enum class EGuLiPredictionTraceEvent.*?\{(.*?)\};", header, re.S).group(1)
    enum_names = [part.strip() for part in enum.split(",") if part.strip()]
    checks = {
        "forty_distinct_columns": len(columns) == len(set(columns)) == 40,
        "thirteen_enum_entries_match_export_names": len(enum_names) == len(names) == 13 and names == EXPECTED_EVENTS,
        "both_input_boundaries_exported": {"mouse_input", "input"} <= set(names),
        "position_offset_and_velocity_fields_present": all(
            f"{prefix}_{axis}" in columns for prefix in ("authoritative", "presented", "offset", "requested_offset")
            for axis in "xyz") and {"signed_presented_cm_s", "signed_authoritative_cm_s", "has_speed"} <= set(columns),
        "trace_api_is_non_shipping": "#if !UE_BUILD_SHIPPING" in header and "void TraceCommanderMoveInput" in header,
    }
    return {"evidence_kind": "static_contract_review", "passed": all(checks.values()),
            "checks": checks, "columns": columns, "events": names,
            "limitations": ["This is static contract validation, not a C++ build or PIE test."]}


def analyze_capture(path: Path, synthetic: bool = False) -> dict:
    spec = importlib.util.spec_from_file_location("base_prediction_analyzer", ROOT / "Scripts/analyze_commander_prediction_trace.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    result = module.analyze(path, synthetic=synthetic)
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != review_contract()["columns"]:
            raise ValueError(f"CSV/source column order mismatch: {path}")
        rows = list(reader)
    unknown = sorted({row["event"] for row in rows} - set(EXPECTED_EVENTS))
    if unknown:
        raise ValueError(f"Unknown trace events: {unknown}")
    frame_rows = [row for row in rows if row["event"] == "frame"]
    metrics = {}
    boundaries = []
    for previous, current in zip(frame_rows, frame_rows[1:]):
        delta = float(current["local_seconds"]) - float(previous["local_seconds"])
        if delta <= 0 or current["has_speed"] != "1":
            continue
        command = int(current["command_id"])
        item = metrics.setdefault(command, {"reverse_distance_including_boundary_cm": 0.0,
            "presentation_only_reverse_including_boundary_cm": 0.0, "command_boundary_frame_count": 0,
            "maximum_boundary_step_cm": 0.0})
        speed = float(current["signed_presented_cm_s"])
        reverse = max(0.0, -speed * delta)
        item["reverse_distance_including_boundary_cm"] += reverse
        if float(current["signed_authoritative_cm_s"]) >= -1:
            item["presentation_only_reverse_including_boundary_cm"] += reverse
        if previous["command_id"] != current["command_id"]:
            step = math.sqrt(sum((float(current[f"presented_{axis}"]) - float(previous[f"presented_{axis}"])) ** 2
                                 for axis in "xyz"))
            item["command_boundary_frame_count"] += 1
            item["maximum_boundary_step_cm"] = max(item["maximum_boundary_step_cm"], step)
            boundaries.append({"previous_command_id": int(previous["command_id"]), "command_id": command,
                "local_seconds": float(current["local_seconds"]), "delta_seconds": delta,
                "step_cm": round(step, 3), "signed_presented_cm_s": speed,
                "signed_authoritative_cm_s": float(current["signed_authoritative_cm_s"]),
                "reverse_cm": round(reverse, 3)})
    for command in result["commands"]:
        command["cross_command_motion"] = {key: round(value, 3) if isinstance(value, float) else value
            for key, value in metrics.get(command["command_id"], {}).items()}
        group = [row for row in rows if int(row["command_id"]) == command["command_id"]]
        mouse = next((row for row in group if row["event"] == "mouse_input"), None)
        ack = next((row for row in group if row["event"].startswith("ack_")), None)
        command["mouse_to_ack_ms"] = round((float(ack["local_seconds"]) - float(mouse["local_seconds"])) * 1000, 3) if ack and mouse else None
        # A sample can arrive after another command has replaced the prediction.
        # Its explicit order_id, joined through a real ACK, is the reliable key.
        start = next(row for row in group if row["event"] == "input")
        matching_sample = next((row for row in rows if ack and row["event"] == "sample_arrival"
            and row["order_id"] == ack["order_id"]
            and float(row["local_seconds"]) >= float(start["local_seconds"])), None)
        command["first_matching_sample_after_input_ms"] = round(
            (float(matching_sample["local_seconds"]) - float(start["local_seconds"])) * 1000, 3
        ) if matching_sample else None
        command["ack_count"] = sum(row["event"].startswith("ack_") for row in group)
    mouse_times = [float(row["local_seconds"]) for row in rows if row["event"] == "mouse_input"]
    result["actual_mouse_input_intervals_ms"] = [round((current - previous) * 1000, 3)
                                                 for previous, current in zip(mouse_times, mouse_times[1:])]
    result["command_boundaries"] = boundaries
    result["column_contract_matches_source"] = True
    result["limitations"].append("Cross-command metrics include the first frame after a new command; within-command base metrics do not.")
    result["limitations"].append("Sample timing joins real ACK order IDs across the whole capture. Missing ACKs in captures made before the late-ACK fix cannot be inferred.")
    result["limitations"].append("render_handoff is an observed threshold after the current command's ACK; if ACK arrived after that threshold it is an upper bound, and replaced commands may have no handoff event.")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path, nargs="*")
    parser.add_argument("--synthetic", action="store_true")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    result = {"schema_version": 1, "contract": review_contract(),
              "captures": [analyze_capture(path, args.synthetic) for path in args.csv]}
    data = json.dumps(result, ensure_ascii=False, indent=2)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(data + "\n", encoding="utf-8")
    else:
        print(data)
    if not result["contract"]["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
