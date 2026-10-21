"""Read-only timing of the production semantic move entry in a fresh review room.

This does not inject an OS mouse click, read images, or certify display scanout.
The short window checks local green submission and visible ISM displacement;
it is not a replacement for the fixed combat performance acceptance capture.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import time

from capture_live_pie import PERF, commands, editor, status, stop_owned_pie
from capture_ui_four_lod import save, profile, set_options


def unit_stage_results(stages, expected, names):
    """Every selected unit must be observed; the fastest member is insufficient."""
    result = {}
    for soldier in expected:
        selected = {}
        for name in names:
            matches = [r for r in stages if r['stage'] == name
                       and re.search(rf'\bsoldier={soldier}\b', r['detail'])]
            selected[name] = matches[0] if len(matches) == 1 else None
        result[str(soldier)] = selected
    return result


def all_units_within_limit(units, limit=20):
    return bool(units) and all(row is not None and 0 <= row['ms'] < limit
                              for stages in units.values() for row in stages.values())


def complete_response_total(green, units):
    """Cumulative time to the last required submission; never sum milestones."""
    rows = [row for stages in units.values() for row in stages.values()]
    if green is None or not rows or any(row is None for row in rows):
        return None
    return max(green['ms'], *(row['ms'] for row in rows))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--editor-log", type=Path, required=True)
    parser.add_argument("--immediate-planning", type=int, choices=(0, 1), default=1)
    parser.add_argument("--post-movement-poses", type=int, choices=(0, 1), default=1)
    parser.add_argument("--immediate-movement", type=int, choices=(0, 1), default=1)
    parser.add_argument("--local-pose", type=int, choices=(0, 1), default=1)
    parser.add_argument("--members", type=int, choices=range(1, 25), default=1)
    parser.add_argument("--combat", action="store_true", help="Prepare the native 150-unit firefight; still a response diagnostic")
    parser.add_argument("--recipe", type=Path)
    parser.add_argument("--trace", action="store_true", help="CPU/GPU trace for diagnosis only")
    parser.add_argument("--fast-compression", type=int, choices=(0, 1), default=1)
    parser.add_argument("--command-wake", type=int, choices=(0, 1), default=0)
    parser.add_argument("--late-presentation", type=int, choices=(0, 1), default=0)
    parser.add_argument("--movement-clock-soldier", type=int, default=0,
                        help="Diagnostic log for one SoldierId; 0 disables")
    parser.add_argument("--network-retry-batch", type=int, choices=range(0, 65), default=0)
    parser.add_argument("--defer-peer-transactions", type=int, choices=(0, 1), default=0)
    parser.add_argument("--batch-move-admissions", type=int, choices=(0, 1), default=0)
    parser.add_argument("--reuse-resource-validation-snapshot", type=int, choices=(0, 1), default=0)
    parser.add_argument("--direct-capture-membership", type=int, choices=(0, 1, 2), default=0)
    parser.add_argument("--foreground-timeout", type=float, default=45,
                        help="Preparation timeout only; timed warmup and capture are unchanged")
    parser.add_argument("--selection-lookup", type=int, choices=(0, 1, 2), default=1,
                        help="0 directory baseline, 1 selected lives, 2 equality diagnostic")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    assert not status().get("active"), "Requires a fresh owned review room"
    values = {"gs.Commander.ImmediateMoveFeedback": 1,
              "gs.Commander.ImmediateInputPlanning": args.immediate_planning,
              "gs.Commander.PublishPoseAfterMovement": args.post_movement_poses,
              "gs.Commander.ImmediateMovement": args.immediate_movement,
              "gs.Commander.LocalAuthorityPose": args.local_pose,
              "gs.Commander.IndexedInputSelection": args.selection_lookup,
              "gs.Hybrid.FastSmallPayloadCompression": args.fast_compression,
              "gs.Hybrid.CommandApplicationWake": args.command_wake,
              "gs.Commander.PresentationAfterCommands": args.late_presentation,
              "gs.Commander.MovementClockSoldier": args.movement_clock_soldier,
              "gs.Hybrid.NetworkRetryBatchLimit": args.network_retry_batch,
              "gs.Hybrid.DeferPeerTransactions": args.defer_peer_transactions,
              "gs.Commander.BatchMoveAdmissions": args.batch_move_admissions,
              "gs.Hybrid.ReuseResourceValidationSnapshot": args.reuse_resource_validation_snapshot,
              "gs.Hybrid.DirectCaptureMembership": args.direct_capture_membership,
              "guli.Commander.MoveLatencyDiagnostics": 0,
              "t.IdleWhenNotForeground": 0, "Slate.bAllowThrottling": 0}
    previous = editor("r={k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in "
                      + repr(list(values)) + "}")
    save(args.output / "settings-before.json", previous)
    offset = args.editor_log.stat().st_size
    try:
        set_options(values)
        editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
               "unreal.SystemLibrary.execute_console_command(w,'gs.Commander.QA.StartPIE standalone 2359 1745')\n"
               "r={'success':True}")
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            state = status()
            if state.get("error") or state.get("network_error"):
                raise RuntimeError(state)
            if state.get("world_ready") and state.get("units") == 600 and state.get("can_issue_orders"):
                break
            time.sleep(1)
        else:
            raise RuntimeError("Review room did not become ready")
        save(args.output / "session.json", state)
        print(json.dumps({"phase": "foreground-preparation", "out": str(args.output)}), flush=True)
        commands(["gs.Commander.QA.FocusPIE", "gs.GM.Commander.Camera.Height 133.4"])
        deadline = time.monotonic() + args.foreground_timeout
        while not profile()["viewport_foreground"]:
            assert time.monotonic() < deadline, "Preview did not become foreground"
            time.sleep(1)
        frozen = json.loads(args.recipe.read_text(encoding='utf-8')) if args.recipe else {}
        frozen['scenario'] = 'combat' if args.combat else 'static'
        prep = editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
                      "p=" + PERF + f"\nr=json.loads(p.prepare_hotspot_workload({json.dumps(frozen)!r},False))")
        save(args.output / "preparation.json", prep)
        work = prep["hotspot_workload"]
        assert not work["error"], work
        recipe = work["frozen_recipe"]
        expected = recipe['red_ids'][:args.members]
        assert len(expected) == args.members, recipe
        if args.combat:
            combat = editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
                            "p=" + PERF + "\nr=json.loads(p.prepare_hotspot_combat(150))")
            save(args.output / 'combat-preparation.json', combat)
            assert not combat.get('error'), combat
        positions = [v["position"] for v in recipe["red_positions"][:24]]
        center = [sum(p[i] for p in positions) / len(positions) for i in range(3)]
        target = recipe["move_target"]
        setup = ("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
                 "pc=unreal.GameplayStatics.get_player_controller(w,0)\n"
                 "pc.set_mouse_location(1179,874)\ncam=pc.get_view_target()\n"
                 "cam.set_actor_rotation(unreal.Rotator(pitch=0,yaw=0,roll=0),False)\n"
                 f"cam.jump_to_world_location(unreal.Vector({','.join(map(str, center))}))\n"
                 "r={'success':True}")
        editor(setup)
        time.sleep(10)
        # Re-establish the exact probe selection after focus preparation. A
        # queued diagnostic is not proof that the controller accepted the move.
        preflight = editor(
            "w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
            "pc=unreal.GameplayStatics.get_player_controller(w,0)\np=" + PERF + "\n"
            f"selection=json.loads(p.prepare_hotspot_selection({expected!r}))\n"
            "presenters=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiCommanderPresentationActor)\n"
            "presentation_ticks=[]\n"
            "for a in presenters:\n"
            "    tick={'actor':a.get_path_name()}\n"
            "    try: tick['group']=str(a.get_editor_property('primary_actor_tick').get_editor_property('tick_group'))\n"
            "    except TypeError as error: tick['group_reflection_error']=str(error)\n"
            "    presentation_ticks.append(tick)\n"
            "r={'selection':selection,'can_issue_orders':pc.can_issue_commander_orders(),"
            "'stream_ready':pc.get_commander_net_sync_component().is_soldier_stream_ready(),"
            "'presentation_ticks':presentation_ticks}"
        )
        save(args.output / "input-preflight.json", preflight)
        assert preflight["can_issue_orders"] and preflight["stream_ready"], preflight
        # Cohort/formation traversal can reorder a multi-unit selection. Validate
        # the exact membership (including duplicates) without changing that order.
        assert sorted(preflight["selection"].get("soldiers", [])) == sorted(expected), preflight
        before = profile()
        assert before["viewport_foreground"], "Preview lost foreground before response window"
        assert before['editor_window_state'] == 'minimized', 'Minimize the editor before the response probe'
        save(args.output / "profile-before.json", before)
        commands(([f"Trace.File {(args.output/'session.utrace').as_posix()} cpu,gpu,frame,bookmark"]
                  if args.trace else ["Trace.Stop", "Trace.Disable"])
                 + ["guli.Commander.MoveLatencyDiagnostics 1"])
        result = editor(
            "w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
            "pc=unreal.GameplayStatics.get_player_controller(w,0)\n"
            "sync=pc.get_commander_net_sync_component()\np=" + PERF + "\n"
            "assert p.begin_timed_capture(6.)\n"
            f"ok=sync.submit_move_response_diagnostic('input-move',1,0,unreal.Vector({','.join(map(str, target))}))\n"
            "r={'success':ok,'input_kind':'controller semantic move, not OS input','viewport':list(pc.get_viewport_size())}")
        assert result["success"], result
        print(json.dumps({"phase": "native-response-window", "out": str(args.output)}), flush=True)
        time.sleep(7)
        after = profile()
        save(args.output / "profile-after.json", after)
        commands(["Trace.Stop", "guli.Commander.MoveLatencyDiagnostics 0"])
        with args.editor_log.open("rb") as log:
            log.seek(offset)
            text = log.read().decode("utf-8", errors="replace")
        rows = []
        pattern = re.compile(r"MassMoveLatency stage=(\S+) clock=([\d.]+) frame=(\d+) player=(\w+) epoch=(\d+) command=(\d+).*?members=(\d+) (.*)")
        for match in pattern.finditer(text):
            stage, clock, frame, player, epoch, command, members, detail = match.groups()
            rows.append(dict(stage=stage, clock=float(clock), frame=int(frame), player=player,
                             epoch=int(epoch), command=int(command), members=int(members), detail=detail.strip()))
        save(args.output / "stages.json", rows)
        inputs = [r for r in rows if r["stage"] == "local-input"]
        assert len(inputs) == 1, inputs
        start = inputs[0]
        matching = [r for r in rows if r["command"] == start["command"]
                    and r["player"] == start["player"] and r["epoch"] == start["epoch"]]
        route = next((r for r in matching if r['stage'] == 'local-route'), None)
        event = re.search(r'\bevent=(\w+)', route['detail']) if route else None
        core = []
        for m in re.finditer(r'HybridCommandTiming stage=(\S+) clock=([\d.]+) thread=(\d+) event=(\w+) value=(\d+)', text):
            stage, clock, thread, identity, value = m.groups()
            if event and identity == event.group(1):
                core.append(dict(stage=stage, clock=float(clock), ms=(float(clock)-start['clock'])*1000,
                                 thread=int(thread), event=identity, value=int(value)))
        save(args.output / 'command-delivery.json', core)
        eligible = [r for r in matching if r["stage"] == "local-probe-ready"]
        ineligible = [r for r in matching if r["stage"] == "local-probe-ineligible"]
        summary = {"input_kind": result["input_kind"], "combat_acceptance": False,
                   "purpose": "command_diagnostic", "combat_prepared": args.combat, "trace": args.trace,
                   "command_event": event.group(1) if event else None,
                   "fast_compression": args.fast_compression,
                   "command_wake": args.command_wake,
                   "late_presentation": args.late_presentation,
                   "network_retry_batch": args.network_retry_batch,
                   "defer_peer_transactions": args.defer_peer_transactions,
                   "batch_move_admissions": args.batch_move_admissions,
                   "reuse_resource_validation_snapshot": args.reuse_resource_validation_snapshot,
                   "direct_capture_membership": args.direct_capture_membership,
                   "movement_clock_soldier": args.movement_clock_soldier,
                   "input_phase": "TimerManager callback before Tickable subsystems; not OS/player-input polling",
                   "expected_soldiers": expected,
                   "selection_lookup": args.selection_lookup,
                   "immediate_planning": args.immediate_planning, "viewport": result["viewport"],
                   "post_movement_poses": args.post_movement_poses,
                   "immediate_movement": args.immediate_movement,
                   "local_pose": args.local_pose,
                   "idle_probe_eligible": len(eligible) == len(expected) and not ineligible,
                   "authority_sources": [r["detail"] for r in eligible + ineligible],
                   "foreground_frames": after["counters"].get("Capture.ViewportForegroundFrames", {}).get("total", 0),
                   "background_frames": after["counters"].get("Capture.ViewportBackgroundFrames", {}).get("total", 0),
                   "stages": [{"stage": r["stage"], "ms": (r["clock"]-start["clock"])*1000,
                               "frames": r["frame"]-start["frame"], "detail": r["detail"]} for r in matching],
                   "local_response_limit_ms": 30.0, "preferred_response_limit_ms": 20.0,
                   "os_click_to_display_acceptance": False}
        summary["all_probe_units_locally_simulated_at_input"] = (
            len(eligible) == len(expected) and all("localSimulation=1 " in r["detail"] for r in eligible))
        summary['editor_minimized_frames'] = after['counters'].get('Capture.EditorRootMinimizedFrames', {}).get('total', 0)
        summary['editor_state_valid'] = (summary['editor_minimized_frames'] > 0
            and summary['editor_minimized_frames'] == summary['foreground_frames'] + summary['background_frames'])
        # This probe starts at the controller's semantic input. It measures real
        # Mass displacement and model submission, but not OS input or GPU display.
        green = next((r for r in summary['stages'] if r['stage'] == 'local-green-submit'), None)
        green_pass = green is not None and 0 <= green['ms'] < 20 and green['frames'] == 0
        measured = unit_stage_results(summary['stages'], expected, ('local-mass-write', 'local-model-submit'))
        summary['unit_2cm_stages'] = measured
        summary["local_semantic_20ms_pass"] = bool(
            summary["idle_probe_eligible"] and summary["all_probe_units_locally_simulated_at_input"]
            and summary['editor_state_valid']
            and summary["foreground_frames"] > 0 and summary["background_frames"] == 0
            and all_units_within_limit(measured) and green_pass)
        first = unit_stage_results(summary['stages'], expected, ('local-first-mass-write', 'local-first-model-submit'))
        summary['unit_1cm_stages'] = first
        summary['response_totals_ms'] = {
            'first_motion_1cm': complete_response_total(green, first),
            'existing_observation_2cm': complete_response_total(green, measured),
        }
        summary["motion_thresholds_cm"] = {"first_motion": 1, "existing_observation": 2}
        summary["first_motion_20ms_pass"] = bool(
            summary["idle_probe_eligible"] and summary["all_probe_units_locally_simulated_at_input"]
            and summary['editor_state_valid']
            and summary["foreground_frames"] > 0 and summary["background_frames"] == 0
            and all_units_within_limit(first) and green_pass)
        green_30 = green is not None and 0 <= green['ms'] < 30 and green['frames'] == 0
        common_response = (summary["idle_probe_eligible"]
            and summary["all_probe_units_locally_simulated_at_input"] and summary['editor_state_valid']
            and summary["foreground_frames"] > 0 and summary["background_frames"] == 0)
        summary["local_semantic_30ms_pass"] = bool(common_response
            and all_units_within_limit(measured, 30) and green_30)
        summary["first_motion_30ms_pass"] = bool(common_response
            and all_units_within_limit(first, 30) and green_30)
        save(args.output / "summary.json", summary)
        print(json.dumps({k: v for k, v in summary.items() if k not in
                          ('stages', 'authority_sources', 'unit_1cm_stages', 'unit_2cm_stages')},
                         ensure_ascii=False), flush=True)
        assert summary["foreground_frames"] > 0 and summary["background_frames"] == 0, summary
        assert summary["idle_probe_eligible"], summary
        assert summary['editor_state_valid'], summary
        assert any(r["stage"] == "local-green-submit" and r["frames"] == 0 for r in summary["stages"]), summary
        assert any(r["stage"] == "local-model-submit" for r in summary["stages"]), summary
    finally:
        if status().get("active"):
            commands(["Trace.Stop", "guli.Commander.MoveLatencyDiagnostics 0"])
            save(args.output / "stopped.json", stop_owned_pie())
        set_options(previous)


if __name__ == "__main__":
    main()
