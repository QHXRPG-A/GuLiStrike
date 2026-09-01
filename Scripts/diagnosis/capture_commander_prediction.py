"""Drive an already-running PIE through the real Slate QA console commands.

Example (no editor launch / PIE start / lifecycle changes):
  python Scripts/diagnosis/capture_commander_prediction.py --world /Game/Maps/UEDPIE_0_Map.Map --soldier 1 --network-label "standalone"
Targets may be supplied with --target and --retarget (X Y Z). Otherwise existing
on-screen allied soldier positions provide ground anchors; that choice does not
prove the route is open or uncongested. --dry-run never connects to the editor.

Requires exclusive use of PointerQA and PredictionTrace during the run. The real
Slate command moves the OS cursor but does not inject OS mouse buttons. This test
changes selection and issues real authoritative movement, including a return to
the original position between A/B modes. It does not teleport, reset the world,
change network conditions, or claim that A/B initial states are identical.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import json
import math
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Scripts"))
from commander_editor_python import call_editor
from commander_prediction_evidence import analyze_capture, review_contract

TRACE_DIR = ROOT / "outputs/commander-selection-20260831/diagnostics"


def xyz(value):
    return tuple(float(value[key]) for key in "xyz")


def distance(a, b):
    return math.sqrt(sum((a[index] - b[index]) ** 2 for index in range(2)))


class Capture:
    def __init__(self, args):
        self.args = args
        self.output = args.outdir.resolve()
        if not self.output.is_relative_to((ROOT / "outputs").resolve()):
            raise ValueError("--outdir must stay beneath this project's outputs directory")
        self.output.mkdir(parents=True, exist_ok=True)
        self.counter = 0
        self.manifest = {"schema_version": 1, "world": args.world, "soldier_id": args.soldier,
            "network_condition_label": args.network_label, "network_conditions_changed": False,
            "editor_or_pie_lifecycle_changed": False, "transport": "commander_editor_python.call_editor",
            "input_path": "Slate viewport QA events -> normal PlayerInput -> PC -> NetSync ready-to-send",
            "contract": review_contract(), "actions": [], "captures": [], "completed": False,
            "limitations": ["Requires exclusive diagnostic/QA ownership.",
                "A/B returns use normal movement; exact identical authority state is not guaranteed.",
                "QA actions take about 245ms; actual mouse-input intervals are measured from the trace.",
                "No runtime result is inferred from a console command being submitted successfully."]}

    def save(self):
        (self.output / "manifest.json").write_text(json.dumps(self.manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    def command(self, command):
        code = (
            "import unreal, json\n"
            f"_prediction_world = unreal.find_object(None, {json.dumps(self.args.world)})\n"
            "if _prediction_world is None: raise RuntimeError('The requested PIE world no longer exists')\n"
            "_prediction_pc = unreal.GameplayStatics.get_player_controller(_prediction_world, 0)\n"
            "if _prediction_pc is None or not _prediction_pc.is_local_controller(): raise RuntimeError('No local player in requested world')\n"
            f"unreal.SystemLibrary.execute_console_command(_prediction_world, {json.dumps(command)}, _prediction_pc)\n"
            "unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'world': _prediction_world.get_path_name()}))\n"
        )
        start = time.monotonic()
        response = call_editor(code, timeout=20.0)
        self.manifest["actions"].append({"command": command, "host_monotonic_seconds": start,
                                          "transport_response": response})
        if not response.get("success") or not isinstance(response.get("result"), dict) or not response["result"].get("success"):
            raise RuntimeError(f"Editor command transport failed: {response}")

    def snapshot(self, label):
        self.counter += 1
        path = self.output / f"snapshot-{self.counter:03d}-{label}.json"
        self.command(f'gs.Commander.QA.InputSnapshot "{path.as_posix()}"')
        if not path.is_file():
            raise RuntimeError("InputSnapshot produced no file; command may be unavailable or context rejected")
        result = json.loads(path.read_text(encoding="utf-8-sig"))
        if result["world"] != self.args.world:
            raise RuntimeError(f"Snapshot came from wrong world: {result['world']}")
        return result

    def idle(self, label="idle", timeout=5.0):
        deadline = time.monotonic() + timeout
        while True:
            result = self.snapshot(label)
            if not result["pointer_qa_active"]:
                return result
            if time.monotonic() >= deadline:
                raise RuntimeError("PointerQA did not finish within the bounded wait")
            time.sleep(0.12)

    def key(self, key):
        self.command(f"gs.Commander.QA.Key {key}")
        time.sleep(0.14)
        return self.idle("key-" + key.lower())

    def right(self, target):
        self.command("gs.Commander.QA.MouseWorld right " + " ".join(f"{coordinate:.6f}" for coordinate in target))

    def seed(self, snapshot):
        row = next((row for row in snapshot["soldiers"] if row["id"] == self.args.soldier), None)
        if not row or not row["alive"]:
            raise RuntimeError("Requested soldier is not alive/presented in this client")
        return row

    def select(self):
        before = self.idle("before-selection")
        self.command(f"gs.Commander.QA.MouseSoldier click {self.args.soldier}")
        time.sleep(self.args.interval)
        deadline = time.monotonic() + 5.0
        while True:
            state = self.idle("selection")
            # Re-selecting the same IDs can ACK without changing the revision.
            acknowledged = (state["selection_revision"] > before["selection_revision"]
                            or state["last_ack_command_id"] > before["last_ack_command_id"])
            if self.args.soldier in state["selected_ids"] and acknowledged:
                return state
            if time.monotonic() >= deadline:
                raise RuntimeError("Real Slate click did not produce an authoritative selection containing the requested soldier")
            time.sleep(0.15)

    def choose_targets(self, state):
        seed = self.seed(state)
        origin = xyz(seed["world_position"])
        candidates = [row for row in state["soldiers"] if row["id"] != self.args.soldier and row["alive"]
            and row["team"] == seed["team"] and row["on_screen"]
            and 0.15 * state["viewport_width"] < row["screen_x"] < 0.85 * state["viewport_width"]
            and 0.15 * state["viewport_height"] < row["screen_y"] < 0.65 * state["viewport_height"]]
        candidates.sort(key=lambda row: distance(origin, xyz(row["world_position"])), reverse=True)
        if self.args.target:
            first = tuple(self.args.target)
        elif candidates and distance(origin, xyz(candidates[0]["world_position"])) >= 1500:
            first = xyz(candidates[0]["world_position"])
        else:
            raise RuntimeError("No useful on-screen allied ground anchor; provide --target X Y Z")
        second = tuple(self.args.retarget) if self.args.retarget else origin
        return origin, first, second

    def return_to(self, origin):
        self.right(origin)
        time.sleep(self.args.interval)
        deadline = time.monotonic() + self.args.return_timeout
        previous = None
        stable = 0
        while time.monotonic() < deadline:
            state = self.idle("return")
            position = xyz(self.seed(state)["world_position"])
            if previous is not None and distance(position, origin) <= self.args.return_tolerance and distance(position, previous) <= 50:
                stable += 1
                if stable >= 2:
                    return {"remaining_distance_cm": distance(position, origin), "position": position,
                            "selection": state["selected_ids"]}
            else:
                stable = 0
            previous = position
            time.sleep(0.4)
        raise RuntimeError("Normal movement did not return the seed near its original position; A/B continuation refused")

    def run(self):
        initial = self.idle("initial")
        self.manifest["initial_snapshot"] = initial
        if not self.seed(initial)["on_screen"]:
            raise RuntimeError("Requested soldier is off screen")
        # Escape is an editor-level EndPIE shortcut in a separate PIE window.
        # Require selection mode from the fixture; never inject Escape here.
        if initial["selection_shape"] != "box":
            switched = self.key("Seven")
            if switched["selection_shape"] != "box":
                raise RuntimeError("Could not establish box selection through Slate Seven")
        selected = self.select()
        origin, first, second = self.choose_targets(selected)
        self.manifest["origin"] = origin
        self.manifest["targets"] = [first, second]
        self.manifest["target_source"] = "explicit" if self.args.target else "on-screen ally ground anchor; not validated as open terrain"
        modes = ["baseline", "no_offset"] if self.args.mode == "both" else [self.args.mode]
        for index, mode in enumerate(modes):
            if index:
                self.manifest["return_between_modes"] = self.return_to(origin)
            before = self.idle(mode + "-before")
            existing = {path.resolve() for path in TRACE_DIR.glob("prediction-*.csv")}
            trace_started = False
            try:
                self.command(f"gs.Commander.PredictionTrace.Start {mode} {self.args.soldier} {self.args.capture_seconds}")
                trace_started = True
                for move in range(self.args.burst):
                    self.right(first if move % 2 == 0 else second)
                    # This bounded host wait lets UE tick; it does not sleep inside the editor.
                    time.sleep(self.args.interval)
                time.sleep(self.args.tail_seconds)
                self.idle(mode + "-after-burst")
            finally:
                if trace_started:
                    self.command("gs.Commander.PredictionTrace.Stop")
            added = sorted({path.resolve() for path in TRACE_DIR.glob("prediction-*.csv")} - existing)
            if len(added) != 1:
                raise RuntimeError(f"Expected one exclusive trace CSV, got {len(added)}; do not interpret ambiguous capture")
            result = analyze_capture(added[0])
            record = {"mode": mode, "before_snapshot": before, "analysis": result,
                "expected_mouse_commands": self.args.burst,
                "observed_mouse_commands_match": result["mouse_input_count"] == self.args.burst,
                "all_mouse_commands_dispatched": not result["mouse_inputs_not_dispatched"],
                "csv_mode_matches": result["modes"] == [mode]}
            self.manifest["captures"].append(record)
            self.save()
            if not all(record[key] for key in ("observed_mouse_commands_match", "all_mouse_commands_dispatched", "csv_mode_matches")):
                raise RuntimeError("Trace did not confirm the intended real mouse command sequence; see manifest")
        self.manifest["completed"] = True
        self.manifest["completed_does_not_mean_drag_fixed"] = True
        self.save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--world", required=True, help="Exact PIE World object path from a probe/snapshot")
    parser.add_argument("--soldier", type=int, required=True)
    parser.add_argument("--target", type=float, nargs=3)
    parser.add_argument("--retarget", type=float, nargs=3)
    parser.add_argument("--mode", choices=("baseline", "no_offset", "both"), default="both")
    parser.add_argument("--network-label", required=True, help="Observed/configured conditions; script never changes them")
    parser.add_argument("--burst", type=int, default=3)
    parser.add_argument("--interval", type=float, default=0.32, help="At least 0.27s for current 7-frame PointerQA action")
    parser.add_argument("--tail-seconds", type=float, default=2.0)
    parser.add_argument("--capture-seconds", type=float, default=12.0)
    parser.add_argument("--return-timeout", type=float, default=15.0)
    parser.add_argument("--return-tolerance", type=float, default=1000.0)
    parser.add_argument("--outdir", type=Path, default=TRACE_DIR / ("slate-ab-" + datetime.now().strftime("%Y%m%d-%H%M%S-%f")))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    values = [args.interval, args.tail_seconds, args.capture_seconds, args.return_timeout, args.return_tolerance]
    values += (args.target or []) + (args.retarget or [])
    if not all(math.isfinite(value) for value in values) or args.soldier <= 0 or args.burst not in range(1, 21) or args.interval < 0.27:
        parser.error("Finite inputs, positive soldier, burst 1..20 and interval >=0.27 are required")
    if args.tail_seconds < 0.5 or not 1 <= args.capture_seconds <= 120 or args.return_timeout <= 0 or args.return_tolerance <= 0:
        parser.error("Invalid duration/tolerance")
    if args.burst * args.interval + args.tail_seconds + 2.0 >= args.capture_seconds:
        parser.error("Capture duration must exceed the intended burst plus tail and a two-second margin")
    if args.dry_run:
        print(json.dumps({"dry_run": True, "connects_to_editor": False, "world": args.world,
            "soldier": args.soldier, "mode": args.mode, "targets": [args.target, args.retarget],
            "contract": review_contract(), "outdir": str(args.outdir)}, ensure_ascii=False, indent=2))
        return
    runner = Capture(args)
    try:
        runner.run()
    except Exception as error:
        runner.manifest["error"] = str(error)
        runner.save()
        raise
    print(json.dumps({"completed": True, "manifest": str(runner.output / "manifest.json")}, ensure_ascii=False))


if __name__ == "__main__":
    main()
