"""Measure complete PIE startup on an explicitly identified MCP editor process."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import statistics
import time

from commander_editor_python import call_editor


def editor(code, timeout=30):
    response = call_editor(code, timeout)
    result = response.get("result", {})
    if not response.get("success") or not isinstance(result, dict) or not result.get("success"):
        raise RuntimeError(json.dumps(response, ensure_ascii=False))
    return result


def timestamp(line):
    found = re.match(r"\[(\d{4}\.\d\d\.\d\d-\d\d\.\d\d\.\d\d):(\d{3})\]", line)
    if not found:
        return None
    return datetime.strptime(found[1] + "." + found[2], "%Y.%m.%d-%H.%M.%S.%f").replace(tzinfo=timezone.utc).timestamp()


def summarize(lines):
    patterns = {
        "request": "[GULI_NAV_STARTUP_TEST] request",
        "pie_copy_started": "Creating play world package:",
        "resource_initialized": "waiting for dynamic navigation",
        "navigation_ready": "Resource world and navigation are Ready",
        "soldiers_spawned": "Spawned 500 independent server-authoritative Mass Soldiers",
    }
    times = {}
    ready = []
    for line in lines:
        stamp = timestamp(line)
        if stamp is None:
            continue
        for key, pattern in patterns.items():
            if pattern in line and key not in times:
                times[key] = stamp
        if "Commander bootstrap ready:" in line:
            ready.append(stamp)
    if "request" not in times or len(ready) < 2 or "soldiers_spawned" not in times:
        return None
    start = times["request"]
    result = {name + "_seconds": round(value - start, 3) for name, value in times.items()}
    result["host_ready_seconds"] = round(ready[0] - start, 3)
    result["client_ready_seconds"] = round(ready[1] - start, 3)
    result["navigation_wait_seconds"] = round(times["navigation_ready"] - times["resource_initialized"], 3)
    result["navigation_preparation_logs"] = [line.rstrip() for line in lines if "[GULI_NAV_PREPARE]" in line]
    entries = [line for line in result["navigation_preparation_logs"] if "kind=" in line]
    for field, key in (("check_s", "hash_and_payload_check_seconds"), ("build_s", "bake_seconds")):
        result[key] = round(sum(float(match[1]) for line in entries
                                if (match := re.search(rf"\b{field}=([\d.]+)", line))), 6)
    for line in result["navigation_preparation_logs"]:
        if "result=Ready" in line:
            for field, key in (("total_s", "prepare_seconds"), ("save_s", "save_seconds"),
                               ("ground_rebuilds", "ground_rebuilds"), ("flight_rebuilds", "flight_rebuilds")):
                match = re.search(rf"\b{field}=([\d.]+)", line)
                if match:
                    result[key] = float(match[1]) if field.endswith("_s") else int(match[1])
    if "pie_copy_started" in times:
        result["pie_initialization_seconds"] = round(times["resource_initialized"] - times["pie_copy_started"], 3)
    result["gates_in_order"] = times["soldiers_spawned"] >= times["navigation_ready"] and min(ready) >= times["navigation_ready"]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--keep-last-play", action="store_true", help="Leave the successful last run open for runtime inspection")
    args = parser.parse_args()
    snapshot = editor("""import os, unreal, json
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
settings = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.LevelEditorPlaySettings'))
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'pid': os.getpid(), 'world': world.get_path_name(), 'clients': settings.get_editor_property('PlayNumberOfClients'), 'one_process': settings.get_editor_property('RunUnderOneProcess'), 'playing': unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()}))""")
    if snapshot["pid"] != args.pid or snapshot["clients"] != 2 or not snapshot["one_process"] or snapshot["playing"]:
        raise RuntimeError(f"Editor/configuration mismatch: {snapshot}")
    if snapshot["world"] != "/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype":
        raise RuntimeError(f"Unexpected map: {snapshot['world']}")
    args.output.mkdir(parents=True, exist_ok=True)
    summary = {"editor": snapshot, "source_log": str(args.log.resolve()), "runs": []}
    for run in range(1, args.runs + 1):
        offset = args.log.stat().st_size
        editor("""import unreal, json
unreal.log('[GULI_NAV_STARTUP_TEST] request')
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
unreal.MCPythonHelper.submit_result(json.dumps({'success': True}))""")
        deadline = time.monotonic() + args.timeout
        result = None
        while time.monotonic() < deadline:
            time.sleep(5)
            with args.log.open("rb") as stream:
                stream.seek(offset)
                content = stream.read().decode("utf-8", errors="replace")
            result = summarize(content.splitlines())
            if result:
                break
        (args.output / f"run-{run}.log").write_text(content, encoding="utf-8")
        if not result or not args.keep_last_play or run != args.runs:
            editor("""import unreal, json
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
unreal.MCPythonHelper.submit_result(json.dumps({'success': True}))""")
        if not result:
            raise RuntimeError(f"Run {run} did not produce two ready clients and 500 soldiers; inspect its log")
        summary["runs"].append(result)
        summary["median_host_ready_seconds"] = statistics.median(x["host_ready_seconds"] for x in summary["runs"])
        summary["median_client_ready_seconds"] = statistics.median(x["client_ready_seconds"] for x in summary["runs"])
        (args.output / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
        print(json.dumps({"run": run, **result}), flush=True)
        time.sleep(5)


if __name__ == "__main__":
    main()
