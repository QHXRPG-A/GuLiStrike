"""Paired Hybrid CPU captures using the existing native combat workload.

No screenshots, bridge queries or scene enumeration during the 30 second window.
Asset identity uses metadata, never binary asset reads.
"""
from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

from capture_live_pie import ROOT, PERF, SNAPSHOT, editor, run, commands, status, stop_owned_pie
from capture_ui_four_lod import save, profile, set_options
from hybrid_workload import camera_comparison

# Production profile. Comparison variants change only the named accepted path.
DEFAULTS = {"gs.Hybrid.FlightServiceFastPath": 1, "gs.Hybrid.YieldBetweenCaptureCuts": 1,
            "gs.CombatEffects.PreloadImpacts": 1, "gs.CombatEffects.PrecompileDestruction": 1,
            "gs.Hybrid.FastPackCacheKey": 1, "gs.Hybrid.NativePeerSend": 1,
            "gs.Hybrid.EntityLayoutCache": 1, "gs.Hybrid.NativePeerReceive": 1,
            "gs.Hybrid.ReusePayloadCompressor": 1, "Slate.SkipEmptyLocalPlayerOperations": 1,
            "gs.Hybrid.CacheFlightLifecycleLookup": 1, "gs.Commander.ImmediateMovement": 1,
            "gs.Commander.LocalAuthorityPose": 1, "gs.Commander.ActiveSoftPairs": 1,
            "gs.Commander.CompleteReadyRoutes": 1, "gs.Commander.IndexedInputSelection": 1,
            "gs.Hybrid.FastSmallPayloadCompression": 1}
OPTIONS = tuple(DEFAULTS)
OVERRIDES = {
    "candidate": {}, "fast_compression": {},
    "standard_compression": {"gs.Hybrid.FastSmallPayloadCompression": 0},
    "sha_cache": {"gs.Hybrid.FastPackCacheKey": 0},
    "copied_peer": {"gs.Hybrid.NativePeerSend": 0},
    "uncached_layout": {"gs.Hybrid.EntityLayoutCache": 0},
    "copied_receive": {"gs.Hybrid.NativePeerReceive": 0},
    "fresh_compressor": {"gs.Hybrid.ReusePayloadCompressor": 0},
    "empty_slate": {"Slate.SkipEmptyLocalPlayerOperations": 0},
    "tree_lifecycle": {"gs.Hybrid.CacheFlightLifecycleLookup": 0},
    "buffered_movement": {"gs.Commander.ImmediateMovement": 0, "gs.Commander.LocalAuthorityPose": 0},
    "network_pose": {"gs.Commander.LocalAuthorityPose": 0},
    "full_soft_pairs": {"gs.Commander.ActiveSoftPairs": 0},
    "verify_soft_pairs": {"gs.Commander.ActiveSoftPairs": 2},
    "deferred_routes": {"gs.Commander.CompleteReadyRoutes": 0},
    "full_input_selection": {"gs.Commander.IndexedInputSelection": 0},
}
VARIANTS = {name: tuple((DEFAULTS | values)[key] for key in OPTIONS)
            for name, values in OVERRIDES.items()}
COMMON = {"gs.Perf.HotspotMask": 31, "gs.Perf.GameplayWorkers": 2,
          "gs.SceneUI.IndexedGeometry": 1, "gs.SceneUI.OptimizedPaint": 1,
          "gs.Commander.MeshLOD": 1, "gs.Commander.PublishPoseAfterMovement": 1,
          "Slate.EnableGlobalInvalidation": 0,
          "guli.Commander.MoveLatencyDiagnostics": 0,
          "t.IdleWhenNotForeground": 0, "Slate.bAllowThrottling": 0}


def capture(out: Path, variant: str, recipe: dict | None, trace: bool = True,
            background_diagnostic: bool = False, editor_window_state: str = "minimized",
            move_diagnostic: bool = False) -> dict:
    assert not status().get("active"), "A fresh owned session is required"
    out.mkdir(parents=True, exist_ok=False)
    values = COMMON | dict(zip(OPTIONS, VARIANTS[variant]))
    values["guli.Commander.MoveLatencyDiagnostics"] = int(move_diagnostic)
    soft_diagnostic = variant == "verify_soft_pairs"
    algorithm_diagnostic = soft_diagnostic
    window_diagnostic = editor_window_state == "any"
    set_options(values)
    editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
           "unreal.SystemLibrary.execute_console_command(w,'gs.Commander.QA.StartPIE standalone 2359 1745')\n"
           "r={'success':True}")
    print(json.dumps({"phase": "starting-preview", "variant": variant,
                      "directory": str(out)}), flush=True)
    try:
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            state = status()
            if state.get("error") or state.get("network_error"):
                raise RuntimeError(state)
            if (state.get("world_ready") and state.get("units") == 600
                    and state.get("peer_owned_objects") == 300 and state.get("can_issue_orders")):
                break
            time.sleep(1)
        else:
            raise RuntimeError("Fresh room preparation timed out")
        save(out / "session.json", state)
        commands(["gs.Commander.QA.FocusPIE"])
        print(json.dumps({"phase": "await-foreground-preparation", "variant": variant,
                          "directory": str(out)}), flush=True)
        focus_deadline = time.monotonic() + 30
        while not background_diagnostic and not profile()["viewport_foreground"]:
            assert time.monotonic() < focus_deadline, "PIE did not become foreground; no timing window started"
            time.sleep(1)
        frozen = dict(recipe or {}, scenario="combat", start_logical_tick=3600)
        if "combat_camera_focus" not in frozen:
            manifest = json.loads((ROOT / "Config/Hybrid/RuntimeManifest-600.json").read_text(encoding="utf-8-sig"))
            anchor = min((o for o in manifest["objects"] if o["definition"] == "unit/2" and o["team"] == 2),
                         key=lambda o: o["ordinal"])
            frozen["combat_camera_focus"] = [float(anchor["x"]), float(anchor["y"])+3500, float(anchor["z"])]
        x, y, z = frozen["combat_camera_focus"]
        commands(["gs.Commander.QA.FocusPIE", "gs.GM.Commander.Camera.Height 133.4"])
        editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
               "pc=unreal.GameplayStatics.get_player_controller(w,0)\npc.set_mouse_location(1179,874)\n"
               "cam=pc.get_view_target()\ncam.set_actor_rotation(unreal.Rotator(pitch=0,yaw=0,roll=0),False)\n"
               f"cam.jump_to_world_location(unreal.Vector({x},{y},{z}))\nr={{'success':True}}")
        preparation = editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="
                             + PERF + f"\nr=json.loads(p.prepare_hotspot_workload({json.dumps(frozen)!r},False))")
        save(out / "preparation.json", preparation)
        work = preparation["hotspot_workload"]
        assert not work.get("error"), work
        frozen = work["frozen_recipe"]
        save(out / "recipe.json", frozen)
        if editor_window_state != "any":
            print(json.dumps({"phase": "await-editor-window", "required": editor_window_state,
                              "variant": variant}), flush=True)
            window_deadline = time.monotonic() + 30
            window_profile = profile()
            while window_profile.get("editor_window_state") != editor_window_state:
                assert time.monotonic() < window_deadline, window_profile.get("editor_window_state")
                time.sleep(1)
                window_profile = profile()
        print(json.dumps({"phase": "warmup", "variant": variant, "directory": str(out)}), flush=True)
        time.sleep(10)
        while True:
            p = profile()
            current = p["simulation_census"]["logical_tick"]
            assert current < 3600, "Preparation missed the prescribed logical start"
            if 3600-current <= 180:
                break
            time.sleep(min(5, (3600-current-150)/60))
        save(out / "profile-before.json", p)
        commands(["gs.Commander.QA.FocusPIE"])
        # Re-establish the prescribed view after focus/click preparation, outside
        # the native timing window. Runtime camera/input behavior stays enabled.
        editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
               "pc=unreal.GameplayStatics.get_player_controller(w,0)\npc.set_mouse_location(1179,874)\n"
               "cam=pc.get_view_target()\ncam.set_actor_rotation(unreal.Rotator(pitch=0,yaw=0,roll=0),False)\n"
               f"cam.jump_to_world_location(unreal.Vector({x},{y},{z}))\nr={{'success':True}}")
        time.sleep(.3)
        before = run(SNAPSHOT)
        camera = before["worlds"][0]["controllers"][0]
        assert camera["viewport"] == [2359, 1748], camera
        assert not camera["camera_transitioning"] and abs(camera["camera_target_height_m"]-133.4) < .01
        save(out / "context-before.json", before)
        preparation_state = editor(
            "w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
            "p=next(s for s in unreal.ObjectIterator(unreal.GuLiUnitFeedbackSubsystem) if s.get_outer()==w)\n"
            "r={'loaded':p.get_loaded_explosion_count(),"
            "'compile_requests':p.get_explosion_compile_request_count(),"
            "'pending':p.get_pending_explosion_compile_count()}")
        save(out / "destruction-preparation.json", preparation_state)
        commands([f"Trace.File {(out/'session.utrace').as_posix()} cpu,gpu,frame,bookmark"]
                 if trace else ["Trace.Stop", "Trace.Disable"])
        # Restore the preview after trace preparation, still outside timing.
        # The native per-frame guard below remains authoritative throughout.
        if not background_diagnostic:
            commands(["gs.Commander.QA.FocusPIE"])
        trace_state = profile()
        if not background_diagnostic and not trace_state["viewport_foreground"]:
            print(json.dumps({"phase": "await-foreground", "variant": variant,
                              "directory": str(out)}), flush=True)
            focus_deadline = time.monotonic() + 30
            while not trace_state["viewport_foreground"] and time.monotonic() < focus_deadline:
                time.sleep(1)
                trace_state = profile()
        assert background_diagnostic or trace_state["viewport_foreground"], "PIE viewport lost foreground before timing"
        assert editor_window_state == "any" or trace_state.get("editor_window_state") == editor_window_state, (
            "Editor root state changed before timing", trace_state.get("editor_window_state"))
        trace_before = {k: trace_state[k] for k in ("trace_connected", "trace_channels")}
        save(out / "trace-before.json", trace_before)
        assert trace_before["trace_connected"] == trace, trace_before
        if not trace:
            assert not trace_before["trace_channels"].strip(","), trace_before
        result = editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="
                        + PERF + "\nr={'started':p.start_hotspot_window()}")
        assert result["started"], result
        print(json.dumps({"phase": "native-30s", "variant": variant, "directory": str(out)}), flush=True)
        time.sleep(33)
        after = profile()
        commands(["Trace.Stop"])
        save(out / "profile-after.json", after)
        for sample in after["network_samples"]:
            async_state = sample["async_transactions"]
            assert async_state["capture_yield_enabled"] == bool(values["gs.Hybrid.YieldBetweenCaptureCuts"])
            assert async_state["fast_pack_cache_key_enabled"] == bool(values["gs.Hybrid.FastPackCacheKey"])
            assert async_state["native_peer_send_enabled"] == bool(values["gs.Hybrid.NativePeerSend"])
            assert async_state["native_peer_receive_enabled"] == bool(values["gs.Hybrid.NativePeerReceive"])
            assert async_state["reuse_payload_compressor_enabled"] == bool(values["gs.Hybrid.ReusePayloadCompressor"])
            assert async_state["fast_small_payload_compression_enabled"] == bool(values["gs.Hybrid.FastSmallPayloadCompression"])
            assert async_state["entity_layout_cache_enabled"] == bool(values["gs.Hybrid.EntityLayoutCache"])
        context_after = run(SNAPSHOT)
        save(out / "context-after.json", context_after)
        camera_stable = camera_comparison(camera, context_after["worlds"][0]["controllers"][0])
        work = after["hotspot_workload"]
        foreground = after["counters"].get("Capture.ViewportForegroundFrames", {}).get("total", 0)
        background = after["counters"].get("Capture.ViewportBackgroundFrames", {}).get("total", 0)
        intervals = after["counters"].get("Capture.FrameIntervalMs", {})
        trace_after = {k: after[k] for k in ("trace_connected", "trace_channels")}
        trace_valid = trace_after["trace_connected"] == trace and (
            trace or not trace_after["trace_channels"].strip(","))
        stat = (ROOT / "Content/Maps/LVL_CommanderMassPrototype.umap").stat()
        engine_exe = Path("D:/UnrealEngine-5.7/Engine/Binaries/Win64/UnrealEditor.exe").stat()
        diagnostic_valid = bool(work.get("valid") and after["capture_valid"] and trace_valid
                                and intervals.get("calls", 0) > 100 and camera_stable["comparable"])
        editor_frames = {name: after["counters"].get(f"Capture.EditorRoot{name.title()}Frames", {}).get("total", 0)
                         for name in ("unavailable", "visible", "minimized", "hidden")}
        editor_state_valid = editor_window_state == "any" or (
            editor_frames[editor_window_state] > 0 and
            editor_frames[editor_window_state] == sum(editor_frames.values()) == foreground + background)
        diagnostic_valid = diagnostic_valid and editor_state_valid
        equivalence = {k: {f: after["counters"].get(k, {}).get(f, 0)
                          for f in ("calls", "total", "max_active_frame")}
                       for k in ("Avoidance.SoftVerifiedUnits", "Avoidance.SoftMismatches",
                                 "Avoidance.SoftMaximumError")}
        if soft_diagnostic:
            assert equivalence["Avoidance.SoftVerifiedUnits"]["total"] > 0, equivalence
            assert equivalence["Avoidance.SoftMismatches"]["total"] == 0, equivalence
        result = {"variant": variant, "trace": trace,
                  "purpose": "command_diagnostic" if move_diagnostic else (
                      "algorithm_diagnostic" if algorithm_diagnostic else (
                      "background_diagnostic" if background_diagnostic else (
                          "uncontrolled_window_diagnostic" if window_diagnostic else "paired_acceptance"))),
                  "soft_pair_equivalence": equivalence if soft_diagnostic else None,
                  "diagnostic_valid": diagnostic_valid,
                  "valid": bool(diagnostic_valid and not background_diagnostic and not algorithm_diagnostic
                                and not window_diagnostic and not move_diagnostic
                                and foreground > 0 and background == 0),
                  "foreground_frames": foreground,
                  "background_frames": background, "foreground_basis": after["foreground_basis"],
                  "editor_window_required": editor_window_state,
                  "editor_window_frames": editor_frames,
                  "editor_window_state_valid": editor_state_valid,
                  "editor_window_basis": after.get("editor_window_basis"),
                  "application_foreground_frames": after["counters"].get("Capture.ForegroundFrames", {}).get("total", 0),
                  "application_background_frames": after["counters"].get("Capture.BackgroundFrames", {}).get("total", 0),
                  "settings": values, "seconds": work.get("seconds"),
                  "trace_before": trace_before, "trace_after": trace_after,
                  "camera_stable": camera_stable,
                  "native_frames": after["engine_frames"], "error": work.get("error"),
                  "frame_intervals": {"n": intervals.get("calls", 0),
                      "mean_ms": intervals.get("total", 0)/max(1, intervals.get("calls", 0)),
                      "p95_ms": intervals.get("p95_active_frame"), "peak_ms": intervals.get("max_active_frame")},
                  "identity": {"map_size": stat.st_size, "map_mtime_ns": stat.st_mtime_ns,
                               "engine_exe_size": engine_exe.st_size, "engine_exe_mtime_ns": engine_exe.st_mtime_ns,
                               "modules": after["simulation_census"]["sync_identity"]["modules"]}}
        save(out / "capture.json", result)
        print(json.dumps(result, ensure_ascii=False), flush=True)
        assert result["valid"] or ((background_diagnostic or algorithm_diagnostic or window_diagnostic or move_diagnostic)
                                   and diagnostic_valid), result
        frozen = work["frozen_recipe"]
        save(out / "recipe.json", frozen)
        return frozen
    finally:
        if status().get("active"):
            commands(["Trace.Stop"])
        save(out / "stopped.json", stop_owned_pie())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=1)
    parser.add_argument("--variants", default="standard_compression,candidate")
    parser.add_argument("--recipe", type=Path)
    parser.add_argument("--no-trace", action="store_true", help="Use native frame intervals to measure Trace overhead")
    parser.add_argument("--background-diagnostic", action="store_true",
                        help="Collect hotspot structure without requiring foreground; always invalid for acceptance")
    parser.add_argument("--editor-window-state", choices=("any", "visible", "minimized"), default="minimized",
                        help="Pin editor root state every frame (default: minimized); any is diagnostic only")
    parser.add_argument("--move-diagnostic", action="store_true",
                        help="Enable existing command stage logs; never eligible for performance acceptance")
    args = parser.parse_args()
    variants = args.variants.split(",")
    assert all(v in VARIANTS for v in variants)
    assert "verify_soft_pairs" not in variants or variants == ["verify_soft_pairs"], (
        "The double-solve diagnostic must not be included in performance comparisons")
    args.output.mkdir(parents=True, exist_ok=False)
    names = list(COMMON) + list(OPTIONS)
    previous = editor("r={k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in "+repr(names)+"}")
    save(args.output / "settings-before.json", previous)
    recipe = json.loads(args.recipe.read_text(encoding="utf-8")) if args.recipe else None
    try:
        for i in range(args.rounds):
            order = variants if i % 2 == 0 else list(reversed(variants))
            for variant in order:
                recipe = capture(args.output / f"r{i+1}-{variant}", variant, recipe, not args.no_trace,
                                 args.background_diagnostic, args.editor_window_state, args.move_diagnostic)
            comparisons = {}
            for variant in order[1:]:
                a = args.output / f"r{i+1}-{order[0]}"
                b = args.output / f"r{i+1}-{variant}"
                cameras = {}
                for phase in ("before", "after"):
                    controllers = [json.loads((p / f"context-{phase}.json").read_text(
                        encoding="utf-8"))["worlds"][0]["controllers"][0] for p in (a, b)]
                    cameras[phase] = camera_comparison(*controllers)
                identities = [json.loads((p / "capture.json").read_text(encoding="utf-8"))["identity"]
                              for p in (a, b)]
                comparisons[variant] = {"cameras": cameras, "identity_equal": identities[0] == identities[1],
                                       "acceptance_eligible": not args.background_diagnostic
                                           and not args.move_diagnostic
                                           and args.editor_window_state != "any"
                                           and all(v != "verify_soft_pairs" for v in order),
                                       "valid": all(c["comparable"] for c in cameras.values())
                                                and identities[0] == identities[1]}
            save(args.output / f"r{i+1}-comparison.json", comparisons)
            assert all(c["valid"] for c in comparisons.values()), comparisons
    finally:
        set_options(previous)


if __name__ == "__main__":
    main()
