"""Capture matched idle/moving Commander CSV and Insights evidence.

This is a disposable PIE-only performance harness for the predictive avoidance
work.  It never saves the level or assets.  Each scenario starts a fresh local
standalone PIE world and refuses to overwrite existing evidence.
"""

from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

from commander_editor_python import call_editor


ROOT = Path(__file__).resolve().parents[1]


def run_editor(code: str) -> dict:
    response = call_editor(code)
    result = response.get("result")
    if not response.get("success") or not isinstance(result, dict):
        raise RuntimeError(json.dumps(response, ensure_ascii=True))
    if result.get("success") is False:
        raise RuntimeError(json.dumps(result, ensure_ascii=True))
    return result


def wait_for(predicate, timeout: float, label: str) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.2)
    raise TimeoutError(label)


def wait_for_csv(path: Path, timeout: float = 20.0) -> None:
    def complete() -> bool:
        if not path.exists():
            return False
        try:
            with path.open("rb") as stream:
                stream.seek(max(0, path.stat().st_size - 32768))
                return b"[hasheaderrowatend]" in stream.read().lower()
        except PermissionError:
            return False

    wait_for(complete, timeout, f"CSV did not complete: {path}")


def snapshot(path: Path) -> dict:
    run_editor(
        f"""
import unreal, json
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
unreal.SystemLibrary.execute_console_command(
    w, 'gs.Commander.QA.InputSnapshot {path.as_posix()}')
unreal.MCPythonHelper.submit_result(json.dumps({{'success': True}}))
"""
    )
    wait_for(path.exists, 3.0, f"Snapshot was not written: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def start_fresh_pie() -> dict:
    run_editor(
        """
import unreal, json
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if les.is_in_play_in_editor():
    les.editor_request_end_play()
unreal.MCPythonHelper.submit_result(json.dumps({'success': True}))
"""
    )
    time.sleep(2.0)
    run_editor(
        """
import unreal, json
cls = unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings')
settings = unreal.get_default_object(cls)
settings.set_editor_property('bThrottleCPUWhenNotForeground', False)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for command in ('t.MaxFPS 0', 't.IdleWhenNotForeground 0',
                'Slate.bAllowThrottling 0', 'r.VSync 0'):
    unreal.SystemLibrary.execute_console_command(world, command)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
unreal.MCPythonHelper.submit_result(json.dumps({'success': True}))
"""
    )

    ready = {}

    def pie_ready() -> bool:
        nonlocal ready
        try:
            ready = run_editor(
                """
import unreal, json
es = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
w = es.get_game_world()
pc = unreal.GameplayStatics.get_player_controller(w, 0) if w else None
ps = pc.get_editor_property('player_state') if pc else None
unreal.MCPythonHelper.submit_result(json.dumps({
    'success': True,
    'ready': bool(w and pc and ps and ps.is_sync_ready()),
    'world': w.get_path_name() if w else None,
    'team': str(ps.get_team()) if ps else None,
}))
"""
            )
            return bool(ready.get("ready"))
        except RuntimeError:
            return False

    wait_for(pie_ready, 15.0, "PIE authority did not become ready")
    return ready


def configure_world(camera_pawn_x: float, camera_y: float) -> dict:
    return run_editor(
        f"""
import unreal, json
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc = unreal.GameplayStatics.get_player_controller(w, 0)
values = {{
    't.MaxFPS': 0, 't.IdleWhenNotForeground': 0,
    'Slate.bAllowThrottling': 0, 'r.VSync': 0,
    'r.ScreenPercentage': 100, 'r.DynamicRes.OperationMode': 0,
    'r.GPUCsvStatsEnabled': 1,
}}
for key, value in values.items():
    unreal.SystemLibrary.execute_console_command(w, key + ' ' + str(value))
for quality in ('ViewDistance', 'AntiAliasing', 'Shadow', 'GlobalIllumination',
                'Reflection', 'PostProcess', 'Texture', 'Effects', 'Foliage', 'Shading'):
    unreal.SystemLibrary.execute_console_command(w, 'sg.' + quality + 'Quality 3')
pc.get_controlled_pawn().set_actor_location(
    unreal.Vector({camera_pawn_x}, {camera_y}, -9932.0), False, True)
unreal.MCPythonHelper.submit_result(json.dumps({{
    'success': True,
    'world': w.get_path_name(),
    'team': str(pc.get_editor_property('player_state').get_team()),
    'viewport': list(pc.get_viewport_size()),
    'cvars': {{key: unreal.SystemLibrary.get_console_variable_float_value(key)
              for key in values}},
}}))
"""
    )


def capture(output: Path, stem: str, frames: int, seconds: float) -> dict:
    csv_path = output / f"{stem}.csv"
    trace_path = output / f"{stem}.utrace"
    if csv_path.exists() or trace_path.exists():
        raise FileExistsError(f"Refusing to overwrite capture {stem}")
    relative_csv = csv_path.relative_to(ROOT).as_posix()
    started = run_editor(
        f"""
import unreal, json
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
unreal.SystemLibrary.execute_console_command(
    w, 'Trace.File {trace_path.as_posix()} default,cpu,frame,bookmark')
unreal.SystemLibrary.execute_console_command(w, 'Trace.Bookmark {stem}_start')
unreal.SystemLibrary.execute_console_command(
    w, 'CsvProfile STARTFILE=../../../{relative_csv}')
unreal.SystemLibrary.execute_console_command(w, 'CsvProfile FRAMES={frames}')
unreal.MCPythonHelper.submit_result(json.dumps({{'success': True}}))
"""
    )
    time.sleep(seconds)
    stopped = run_editor(
        f"""
import unreal, json
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
unreal.SystemLibrary.execute_console_command(w, 'Trace.Bookmark {stem}_end')
unreal.SystemLibrary.execute_console_command(w, 'Trace.Stop')
unreal.MCPythonHelper.submit_result(json.dumps({{'success': True}}))
"""
    )
    wait_for_csv(csv_path)
    return {
        "stem": stem,
        "requested_frames": frames,
        "trace_seconds": seconds,
        "csv_bytes": csv_path.stat().st_size,
        "trace_bytes": trace_path.stat().st_size,
        "start": started,
        "stop": stopped,
    }


def pointer(command: str, pause: float = 0.7) -> None:
    run_editor(
        f"""
import unreal, json
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
unreal.SystemLibrary.execute_console_command(w, {command!r})
unreal.MCPythonHelper.submit_result(json.dumps({{'success': True}}))
"""
    )
    time.sleep(pause)


def select_team(first_id: int, second_id: int, output: Path, stem: str) -> dict:
    pointer(f"gs.Commander.QA.MouseSoldier alt {first_id}")
    pointer(f"gs.Commander.QA.MouseSoldier altshift {second_id}")
    state = snapshot(output / f"{stem}.json")
    if len(state["selected_ids"]) != 250:
        raise RuntimeError(f"Expected 250 selected Soldiers, got {len(state['selected_ids'])}")
    return state


def issue_move(
    sample_pawn_x: float,
    command_pawn_x: float,
    y: float,
    target_x: float,
    target_z: float = -7100.0,
) -> None:
    run_editor(
        f"""
import unreal, json
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc = unreal.GameplayStatics.get_player_controller(w, 0)
pc.get_controlled_pawn().set_actor_location(
    unreal.Vector({command_pawn_x}, {y}, -9932.0), False, True)
unreal.MCPythonHelper.submit_result(json.dumps({{'success': True}}))
"""
    )
    time.sleep(0.5)
    pointer(f"gs.Commander.QA.MouseWorld right {target_x} {y} {target_z}")
    run_editor(
        f"""
import unreal, json
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc = unreal.GameplayStatics.get_player_controller(w, 0)
pc.get_controlled_pawn().set_actor_location(
    unreal.Vector({sample_pawn_x}, {y}, -9932.0), False, True)
unreal.MCPythonHelper.submit_result(json.dumps({{'success': True}}))
"""
    )
    time.sleep(0.8)


def team_average_x(state: dict, team: int) -> float:
    values = [row["world_position"]["x"] for row in state["soldiers"] if row["team"] == team]
    if len(values) != 250:
        raise RuntimeError(f"Expected 250 soldiers for team {team}, got {len(values)}")
    return sum(values) / len(values)


def create_blue_and_remove_red() -> dict:
    created = run_editor(
        """
import unreal, json
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc = unreal.GameplayStatics.create_player(w, 1, True)
unreal.MCPythonHelper.submit_result(json.dumps({
    'success': True, 'created': pc.get_path_name() if pc else None}))
"""
    )

    def both_ready() -> bool:
        result = run_editor(
            """
import unreal, json
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pcs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.PlayerController)
rows = []
for pc in pcs:
    ps = pc.get_editor_property('player_state')
    rows.append({'team': str(ps.get_team()), 'ready': bool(ps.is_sync_ready())})
unreal.MCPythonHelper.submit_result(json.dumps({
    'success': True, 'ready': len(rows) == 2 and all(x['ready'] for x in rows),
    'players': rows}))
"""
        )
        return bool(result["ready"])

    wait_for(both_ready, 8.0, "Second local commander did not become ready")
    removed = run_editor(
        """
import unreal, json
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pcs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.PlayerController)
red = next(pc for pc in pcs if pc.get_editor_property('player_state').get_team() == unreal.GuLiTeam.RED)
blue = next(pc for pc in pcs if pc.get_editor_property('player_state').get_team() == unreal.GuLiTeam.BLUE)
keyboard_user = red.get_platform_user_id()
unreal.GameplayStatics.remove_player(red, True)
unreal.GameplayStatics.set_player_platform_user_id(blue, keyboard_user)
unreal.GameplayStatics.set_player_controller_id(blue, 0)
unreal.MCPythonHelper.submit_result(json.dumps({'success': True}))
"""
    )
    time.sleep(0.8)
    remaining = run_editor(
        """
import unreal, json
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc = unreal.GameplayStatics.get_player_controller(w, 0)
ps = pc.get_editor_property('player_state') if pc else None
unreal.MCPythonHelper.submit_result(json.dumps({
    'success': True, 'players': unreal.GameplayStatics.get_num_player_controllers(w),
    'team': str(ps.get_team()) if ps else None,
    'ready': bool(ps and ps.is_sync_ready())}))
"""
    )
    if remaining["players"] != 1 or "BLUE" not in remaining["team"] or not remaining["ready"]:
        raise RuntimeError(f"Blue commander handoff failed: {remaining}")
    return {"created": created, "removed": removed, "remaining": remaining}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", choices=("250", "500"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--suffix", required=True)
    parser.add_argument("--frames", type=int, default=720)
    parser.add_argument("--trace-seconds", type=float, default=10.5)
    args = parser.parse_args()
    output = args.output.resolve()
    outputs_root = (ROOT / "outputs").resolve()
    if outputs_root not in output.parents:
        parser.error("--output must be below Project/outputs")
    output.mkdir(parents=True, exist_ok=True)
    metadata_path = output / f"scenario_{args.scenario}_{args.suffix}.json"
    if metadata_path.exists():
        raise FileExistsError(f"Refusing to overwrite {metadata_path}")

    result = {
        "scenario": int(args.scenario),
        "map": "/Game/Maps/LVL_CommanderMassPrototype",
        "suffix": args.suffix,
        "started_at_unix": time.time(),
    }
    result["pie"] = start_fresh_pie()
    result["settings"] = configure_world(32767.0, 127500.0)
    time.sleep(2.0)
    initial = snapshot(output / f"initial_{args.scenario}_{args.suffix}.json")
    result["initial_red_average_x"] = team_average_x(initial, 1)
    result["idle_capture"] = capture(
        output, f"idle_{args.scenario}_{args.suffix}", args.frames, args.trace_seconds)

    select_team(105, 230, output, f"selected_red_{args.scenario}_{args.suffix}")
    red_target_x = 110000.0 if args.scenario == "500" else 65000.0
    issue_move(32767.0, red_target_x - 17233.0, 127500.0, red_target_x)
    red_moving = snapshot(output / f"red_moving_{args.scenario}_{args.suffix}.json")
    result["red_before_capture_average_x"] = team_average_x(red_moving, 1)

    if args.scenario == "500":
        result["second_commander"] = create_blue_and_remove_red()
        result["blue_settings"] = configure_world(152767.0, 37500.0)
        time.sleep(0.5)
        # These two representatives sit above the bottom HUD dock at the fixed
        # blue camera and cover the two 125-member unit types.
        select_team(371, 496, output, f"selected_blue_{args.scenario}_{args.suffix}")
        issue_move(152767.0, 212767.0, 37500.0, 230000.0)

    before = snapshot(output / f"moving_{args.scenario}_{args.suffix}_before.json")
    result["before_capture_average_x"] = {
        "red": team_average_x(before, 1),
        "blue": team_average_x(before, 2),
    }
    run_editor(
        """
import unreal, json
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
unreal.SystemLibrary.execute_console_command(w, 'gs.GM.Commander.Nav.Stats')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True}))
"""
    )
    result["moving_capture"] = capture(
        output, f"moving_{args.scenario}_{args.suffix}", args.frames, args.trace_seconds)
    after = snapshot(output / f"moving_{args.scenario}_{args.suffix}_after.json")
    result["after_capture_average_x"] = {
        "red": team_average_x(after, 1),
        "blue": team_average_x(after, 2),
    }
    run_editor(
        """
import unreal, json
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
unreal.SystemLibrary.execute_console_command(w, 'gs.GM.Commander.Nav.Stats')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True}))
"""
    )
    result["finished_at_unix"] = time.time()
    metadata_path.write_text(
        json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2), flush=True)


if __name__ == "__main__":
    main()
