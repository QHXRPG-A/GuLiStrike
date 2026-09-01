"""Probe selection -> immediate move using real Slate and already-configured lag.

Requires exclusive access to an already-running local PIE world with old_id
selected. Does not set network conditions, move cameras, or start/stop PIE.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

from capture_commander_prediction import Capture, TRACE_DIR, xyz, distance
from commander_prediction_evidence import analyze_capture


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--world", required=True)
    p.add_argument("--old-id", type=int, default=55)
    p.add_argument("--new-id", type=int, default=66)
    p.add_argument("--target", type=float, nargs=3, required=True)
    p.add_argument("--outdir", type=Path, required=True)
    args = p.parse_args()
    args.soldier = args.new_id
    args.network_label = "Existing network emulation; external log/ping evidence required"
    run = Capture(args)
    try:
        before = run.idle("before")
        if before["selected_ids"] != [args.old_id]:
            raise RuntimeError("Fixture must have exactly old-id selected")
        seed = run.seed(before)
        if not seed["on_screen"] or not seed["alive"]:
            raise RuntimeError("New selection target must be alive and visible")
        previous_files = {x.resolve() for x in TRACE_DIR.glob("prediction-*.csv")}
        run.command("gs.Commander.PredictionTrace.Start baseline 0 10")
        try:
            run.command(f"gs.Commander.QA.MouseSoldier click {args.new_id}")
            time.sleep(0.25)
            after_click = run.idle("after-click")
            if after_click["selected_ids"] != [args.old_id]:
                raise RuntimeError("Selection already confirmed; this is not a pending-selection test")
            run.right(args.target)
            time.sleep(0.25)
            after_right = run.idle("after-right")
            time.sleep(2.0)
            after = run.idle("after")
        finally:
            run.command("gs.Commander.PredictionTrace.Stop")
        added = sorted({x.resolve() for x in TRACE_DIR.glob("prediction-*.csv")} - previous_files)
        if len(added) != 1:
            raise RuntimeError("Expected exactly one new trace")
        analysis = analyze_capture(added[0])
        commands = analysis["commands"]
        old_before = next(x for x in before["soldiers"] if x["id"] == args.old_id)
        old_after = next(x for x in after["soldiers"] if x["id"] == args.old_id)
        checks = {
            "old_selection_still_confirmed_after_click": after_click["selected_ids"] == [args.old_id],
            "new_selection_confirmed": after["selected_ids"] == [args.new_id],
            "one_real_mouse_move": analysis["mouse_input_count"] == 1 and len(commands) == 1,
            "prediction_binds_new_soldier": len(commands) == 1 and commands[0]["soldier_id"] == args.new_id,
            "prediction_waited_for_selection": len(commands) == 1 and commands[0]["mouse_to_prediction_begin_ms"] > 0,
        }
        run.manifest.update({"probe": "pending selection then immediate real mouse right-click",
            "before": before, "after_click": after_click, "after_right": after_right, "after": after,
            "old_soldier_snapshot_net_displacement_cm": distance(xyz(old_before["world_position"]), xyz(old_after["world_position"])),
            "analysis": analysis, "checks": checks, "completed": all(checks.values())})
        run.save()
        print(json.dumps({"manifest": str(run.output / "manifest.json"), "checks": checks,
                          "commands": commands}, ensure_ascii=False, indent=2))
        if not all(checks.values()):
            raise RuntimeError("Pending selection evidence did not meet the explicit checks")
    except Exception as error:
        run.manifest["error"] = str(error)
        run.save()
        raise


if __name__ == "__main__":
    main()
