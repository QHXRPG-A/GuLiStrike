"""Compare explicitly supplied Commander ring before/after CSV captures.

Example (from the project directory):
  python Scripts/analyze_commander_ring_ab.py --warmup 120 --sample 600 \
    outputs/commander-ring-hud-20260830/near-before-r1.csv \
    outputs/commander-ring-hud-20260830/near-after-r1.csv

Names must be CASE-before.csv / CASE-after.csv, optionally ending in -rN.
Every case/run needs both stages. All inputs must share an output directory.
No capture is discovered automatically, and no performance pass/fail gate is
assumed. Negative percentage changes mean lower measured time/count after.
"""

import argparse
import csv
import json
import math
import re
import statistics
import sys
from datetime import datetime, timezone
from pathlib import Path


NAME_PATTERN = re.compile(
    r"^(?P<case>.+)-(?P<stage>before|after)(?:-r(?P<run>[1-9][0-9]*))?$",
    re.IGNORECASE,
)
CORE_COLUMNS = {
    "frame_time_ms": ("FrameTime", "ms"),
    "game_thread_ms": ("GameThreadTime", "ms"),
    "render_thread_ms": ("RenderThreadTime", "ms"),
    "gpu_ms": ("GPUTime", "ms"),
    "rhi_draw_calls": ("RHI/DrawCalls", "calls/frame"),
    "rhi_primitives_drawn": ("RHI/PrimitivesDrawn", "primitives/frame"),
}
PRESENTATION_METRIC = "presentation_rebuild_ms"
OPTIONAL_METRICS = {"render_thread_ms"}


def parse_number(value):
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) else None


def summarize(values):
    ordered = sorted(values)
    if not ordered:
        raise ValueError("Cannot summarize an empty sample")
    return {
        "samples": len(ordered),
        "median": statistics.median(ordered),
        "p95": ordered[math.ceil(len(ordered) * 0.95) - 1],
        "minimum": ordered[0],
        "maximum": ordered[-1],
    }


def capture_identity(path):
    match = NAME_PATTERN.fullmatch(path.stem)
    if path.suffix.lower() != ".csv" or not match:
        raise ValueError(
            f"{path}: expected CASE-before.csv / CASE-after.csv, "
            "or CASE-before-rN.csv / CASE-after-rN.csv"
        )
    return {
        "case": match["case"],
        "stage": match["stage"].lower(),
        "run": f"r{match['run']}" if match["run"] else "default",
    }


def is_header(row):
    if not row:
        return False
    first = row[0].strip().casefold()
    return first == "frametime" or (
        first == "events" and (len(row) == 1 or parse_number(row[1]) is None)
    )


def is_gpu_pass_column(name):
    return bool(re.match(r"^gpu(?:[0-9]+)?[/_]", name, re.IGNORECASE))


def read_capture(stream, path, warmup, sample):
    """Read UE's streaming CSV format, including its expanded final header."""
    records = [
        (line, row)
        for line, row in enumerate(csv.reader(stream), 1)
        if row and any(cell.strip() for cell in row)
    ]
    headers = [(line, [cell.strip() for cell in row]) for line, row in records if is_header(row)]
    if not headers:
        raise ValueError(f"{path}: no UE CSV header found")
    header = headers[-1][1]
    normalized = [name.casefold() for name in header]
    if len(set(normalized)) != len(normalized):
        raise ValueError(f"{path}: duplicate column names in final CSV header")
    for line, earlier in headers[:-1]:
        if normalized[:len(earlier)] != [name.casefold() for name in earlier]:
            raise ValueError(f"{path}:{line}: CSV header changed column order during capture")
    column_index = {name.casefold(): index for index, name in enumerate(header)}

    columns = {}
    for metric, (expected_name, _unit) in CORE_COLUMNS.items():
        index = column_index.get(expected_name.casefold())
        if index is None:
            if metric in OPTIONAL_METRICS:
                continue
            raise ValueError(f"{path}: missing required CSV column {expected_name!r}")
        columns[metric] = header[index]
    presentation = [
        name for name in header
        if name.casefold().startswith("gulicommanderpresentation/")
        and name.casefold().endswith("/rebuildlocalinstances")
    ]
    if len(presentation) != 1:
        raise ValueError(
            f"{path}: expected one Commander Presentation/RebuildLocalInstances "
            f"column, found {presentation}"
        )
    columns[PRESENTATION_METRIC] = presentation[0]

    frame_index = column_index["frametime"]
    frame_rows = []
    metadata_rows = []
    for line, row in records:
        if is_header(row):
            continue
        frame_time = parse_number(row[frame_index] if frame_index < len(row) else None)
        first = row[0].strip().casefold()
        if first in {"[hasheaderrowatend]", "[metadata]"} or (
            first.startswith("[") and frame_time is None
        ):
            metadata_rows.append(row)
            continue
        if frame_time is None:
            raise ValueError(f"{path}:{line}: invalid FrameTime; refusing to silently drop a frame")
        if len(row) > len(header):
            raise ValueError(f"{path}:{line}: frame has more fields than the final header")
        frame_rows.append((line, row))
    if len(frame_rows) < warmup + sample:
        raise ValueError(
            f"{path}: need {warmup + sample} frames ({warmup} warmup + {sample} sample), "
            f"found {len(frame_rows)}"
        )
    selected_rows = frame_rows[warmup:warmup + sample]

    def read_values(name, required=True):
        index = column_index[name.casefold()]
        values = []
        for line, row in selected_rows:
            value = parse_number(row[index] if index < len(row) else None)
            if value is None or value < 0:
                if required:
                    raise ValueError(f"{path}:{line}: {name!r} has a missing/non-finite/negative sample")
                return None
            values.append(value)
        return values

    values = {metric: read_values(name) for metric, name in columns.items()}
    if min(values["frame_time_ms"]) <= 0:
        raise ValueError(f"{path}: selected FrameTime includes zero; frame timing is not usable")
    if not any(value > 0 for value in values["gpu_ms"]):
        raise ValueError(f"{path}: GPUTime is entirely zero; enable GPU CSV timing and recapture")

    gpu_columns = [name for name in header if is_gpu_pass_column(name)]
    gpu_values = {}
    unavailable_gpu_columns = []
    for name in gpu_columns:
        samples = read_values(name, required=False)
        if samples is None:
            unavailable_gpu_columns.append(name)
        else:
            gpu_values[name] = samples
    details = {
        "file": str(path),
        **capture_identity(path),
        "available_frame_rows": len(frame_rows),
        "discarded_warmup_frames": warmup,
        "sampled_frames": sample,
        "unused_trailing_frames": len(frame_rows) - warmup - sample,
        "sample_duration_seconds": sum(values["frame_time_ms"]) / 1000.0,
        "header_source": "final_summary_header" if len(headers) > 1 else "single_header",
        "columns": columns,
        "missing_optional_metrics": sorted(OPTIONAL_METRICS - columns.keys()),
        "metrics": {metric: summarize(samples) for metric, samples in values.items()},
        "zero_value_counts": {metric: sum(value == 0 for value in samples) for metric, samples in values.items()},
        "gpu_pass_columns": gpu_columns,
        "gpu_pass_metrics_ms": {name: summarize(samples) for name, samples in gpu_values.items()},
        "gpu_passes_without_complete_numeric_samples": unavailable_gpu_columns,
        "csv_metadata_rows": metadata_rows,
    }
    return {"summary": details, "values": values, "gpu_values": gpu_values}


def compare_metrics(before, after, gpu_passes=False):
    comparisons = {}
    for metric in sorted(before.keys() & after.keys()):
        unit = "ms" if gpu_passes or metric == PRESENTATION_METRIC else CORE_COLUMNS[metric][1]
        comparison = {"unit": unit}
        for statistic in ("median", "p95"):
            old = before[metric][statistic]
            new = after[metric][statistic]
            comparison[statistic] = {
                "before": old,
                "after": new,
                "absolute_change": new - old,
                "percent_change": (new - old) / old * 100.0 if old != 0 else None,
            }
            if old == 0:
                comparison[statistic]["percent_change_unavailable_reason"] = "before_value_is_zero"
        comparisons[metric] = comparison
    return comparisons


def aggregate_stage(captures):
    def pool(key):
        common = set.intersection(*(set(capture[key]) for capture in captures))
        return {
            metric: summarize([
                value for capture in captures for value in capture[key][metric]
            ])
            for metric in sorted(common)
        }

    return {
        "capture_count": len(captures),
        "sampled_frames": sum(capture["summary"]["sampled_frames"] for capture in captures),
        "metrics": pool("values"),
        "gpu_pass_metrics_ms": pool("gpu_values"),
    }


def build_report(captures, warmup, sample):
    grouped = {}
    for capture in captures:
        identity = capture["summary"]
        stages = grouped.setdefault(identity["case"], {}).setdefault(identity["run"], {})
        if identity["stage"] in stages:
            raise ValueError(
                f"Duplicate capture for {identity['case']}/{identity['run']}/{identity['stage']}"
            )
        stages[identity["stage"]] = capture
    cases = {}
    warnings = []
    for capture in captures:
        summary = capture["summary"]
        if summary["zero_value_counts"]["gpu_ms"]:
            warnings.append(
                f"{summary['file']}: GPUTime contains {summary['zero_value_counts']['gpu_ms']} "
                "zero samples; verify GPU query availability before interpreting the comparison."
            )
    for case, runs in sorted(grouped.items()):
        paired_comparisons = {}
        for run, stages in sorted(runs.items()):
            if set(stages) != {"before", "after"}:
                missing = sorted({"before", "after"} - stages.keys())
                raise ValueError(f"{case}/{run}: missing {missing} capture; no baseline will be inferred")
            paired_comparisons[run] = {
                "before_file": stages["before"]["summary"]["file"],
                "after_file": stages["after"]["summary"]["file"],
                "metrics": compare_metrics(
                    stages["before"]["summary"]["metrics"], stages["after"]["summary"]["metrics"]
                ),
                "gpu_pass_metrics_ms": compare_metrics(
                    stages["before"]["summary"]["gpu_pass_metrics_ms"],
                    stages["after"]["summary"]["gpu_pass_metrics_ms"],
                    gpu_passes=True,
                ),
            }
        before = aggregate_stage([stages["before"] for stages in runs.values()])
        after = aggregate_stage([stages["after"] for stages in runs.values()])
        comparisons = compare_metrics(before["metrics"], after["metrics"])
        missing_optional = sorted(OPTIONAL_METRICS - comparisons.keys())
        if missing_optional:
            warnings.append(f"{case}: optional metrics not present in every paired capture: {missing_optional}")
        cases[case] = {
            "matched_runs": sorted(runs),
            "before": before,
            "after": after,
            "metrics": comparisons,
            "gpu_pass_metrics_ms": compare_metrics(
                before["gpu_pass_metrics_ms"], after["gpu_pass_metrics_ms"], gpu_passes=True
            ),
            "paired_run_comparisons": paired_comparisons,
            "unavailable_optional_comparisons": missing_optional,
        }
    return {
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "analysis_complete": True,
        "method": {
            "discarded_warmup_frames_per_capture": warmup,
            "sample_frames_per_capture": sample,
            "sample_window": "First sample frames immediately after warmup; no outlier trimming.",
            "p95": "Nearest rank: sorted_values[ceil(0.95 * count) - 1].",
            "case_aggregation": "Pool equal-length frame samples from all matched runs; also report each run pair.",
            "percent_change": "100 * (after - before) / before; negative means lower measured time/count.",
            "zero_before_value": "Percentage change is null; absolute change is still reported.",
            "performance_verdict": "No automatic pass/fail threshold or optimization win is assumed.",
            "comparability_limit": "CSV names do not verify matching camera, soldiers, quality, net mode, or background load; the capture operator must hold them fixed.",
        },
        "captures": [capture["summary"] for capture in captures],
        "cases": cases,
        "gpu_pass_columns": sorted({
            name for capture in captures for name in capture["summary"]["gpu_pass_columns"]
        }),
        "warnings": warnings,
    }


def print_report(report, output):
    def change_text(value):
        return "n/a (before=0)" if value is None else f"{value:+.2f}%"

    for case, comparison in report["cases"].items():
        print(f"{case}: {len(comparison['matched_runs'])} matched run(s)")
        for metric, result in comparison["metrics"].items():
            median = result["median"]
            p95 = result["p95"]
            print(
                f"  {metric} [{result['unit']}]: median {median['before']:.4f} -> "
                f"{median['after']:.4f} ({change_text(median['percent_change'])}); "
                f"p95 {p95['before']:.4f} -> {p95['after']:.4f} "
                f"({change_text(p95['percent_change'])})"
            )
    print("GPU pass columns (per-pass statistics are in the JSON):")
    for name in report["gpu_pass_columns"]:
        print(f"  {name}")
    if not report["gpu_pass_columns"]:
        print("  None detected; only the required total GPUTime was available.")
    for warning in report["warnings"]:
        print(f"Warning: {warning}")
    print(f"Saved {output}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", type=Path, nargs="+", help="Explicit paired CSV capture paths")
    parser.add_argument("--warmup", type=int, default=120, help="Frames to discard per capture (default: 120)")
    parser.add_argument("--sample", type=int, default=600, help="Frames to sample per capture (default: 600)")
    args = parser.parse_args(argv)
    if args.warmup < 0 or args.sample < 1:
        parser.error("--warmup must be >= 0 and --sample must be >= 1")
    paths = [path.resolve() for path in args.captures]
    if len({path.parent for path in paths}) != 1:
        parser.error("All capture CSVs must be in the same directory as the generated summary")
    try:
        captures = []
        for path in paths:
            capture_identity(path)
            with path.open(encoding="utf-8-sig", newline="") as stream:
                captures.append(read_capture(stream, path, args.warmup, args.sample))
        report = build_report(captures, args.warmup, args.sample)
        output = paths[0].parent / "ring-performance-summary.json"
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")
    except (OSError, UnicodeError, ValueError, csv.Error) as error:
        print(f"Ring CSV analysis failed: {error}", file=sys.stderr)
        return 1
    print_report(report, output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
