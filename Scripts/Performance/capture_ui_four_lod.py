"""Fresh-room UI/four-tier comparisons: 10 s settled warmup, native 30 s windows.

No bridge, screenshots, object enumeration or analysis during a measured window.
Map identity uses filesystem metadata; Unreal asset binaries are not read.
"""
from __future__ import annotations
import argparse
import json
import time
from pathlib import Path
from capture_live_pie import ROOT, PERF, SNAPSHOT, editor, run, commands, status, stop_owned_pie

CVAR_NAMES = ("gs.SceneUI.IndexedGeometry", "gs.SceneUI.OptimizedPaint",
              "gs.Commander.MeshLOD", "Slate.EnableGlobalInvalidation")
VARIANTS = {"baseline": (0, 0, 0, 0), "ui": (1, 1, 0, 0),
            "final": (1, 1, 1, 0), "global": (1, 1, 1, 1)}


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def profile():
    return editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="
                  + PERF + "\nr=json.loads(p.get_capture_json())")


def visual_stats():
    return editor("""w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
r={'ui':[json.loads(x.get_frame_stats_json()) for x in unreal.ObjectIterator(unreal.GuLiSceneUIWidget) if x.get_world()==w],
   'lod':[json.loads(x.get_mesh_lod_stats_json()) for x in unreal.ObjectIterator(unreal.GuLiCommanderLODSubsystem) if x.get_outer()==w]}""")


def set_options(values):
    editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
           + "\n".join(f"unreal.SystemLibrary.execute_console_command(w,{(k+' '+str(v))!r})" for k, v in values.items())
           + "\nr={k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in "
           + repr(list(values)) + "}")


def capture(out, variant, recipe=None):
    out.mkdir(parents=True, exist_ok=False)
    assert not status().get("active")
    set_options(dict(zip(CVAR_NAMES, VARIANTS[variant])))
    settings = {"gs.Perf.HotspotMask": 31, "gs.Perf.GameplayWorkers": 2,
                "t.IdleWhenNotForeground": 0, "Slate.bAllowThrottling": 0}
    set_options(settings)
    # The current desktop scale adds three physical client pixels to the requested height.
    editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
           "unreal.SystemLibrary.execute_console_command(w,'gs.Commander.QA.StartPIE standalone 2359 1745')\n"
           "r={'success':True}")
    try:
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            s = status()
            if s.get("error") or s.get("network_error"):
                raise RuntimeError(s)
            if s.get("world_ready") and s.get("units") == 600 and s.get("peer_owned_objects") == 300 and s.get("can_issue_orders"):
                break
            time.sleep(1)
        else:
            raise RuntimeError("PIE preparation timeout")
        save(out / "session.json", s)
        commands(["gs.GM.Commander.Camera.Height 133.4"])
        editor("""w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc=unreal.GameplayStatics.get_player_controller(w,0)
pc.set_mouse_location(1179,874)
cam=pc.get_view_target()
cam.set_actor_rotation(unreal.Rotator(pitch=0,yaw=0,roll=0),False)
cam.jump_to_world_location(unreal.Vector(-6000,60000,998.6220703125))
r={'success':True}""")
        frozen = dict(recipe or {}, scenario="static", start_logical_tick=3600)
        prep = editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="
                      + PERF + f"\nr=json.loads(p.prepare_hotspot_workload({json.dumps(frozen)!r},False))")
        save(out / "preparation.json", prep)
        work = prep["hotspot_workload"]
        assert not work.get("error"), work
        frozen = work["frozen_recipe"]
        save(out / "recipe.json", frozen)
        # Settling and shader warmup are outside the timed window.
        time.sleep(10)
        context = run(SNAPSHOT)
        pc = context["worlds"][0]["controllers"][0]
        assert pc["viewport"] == [2359, 1748], pc
        assert not pc["camera_transitioning"] and abs(pc["camera_target_height_m"] - 133.4) < .01
        save(out / "context-before.json", context)
        while True:
            p = profile()
            current = p["simulation_census"]["logical_tick"]
            if current >= 3600:
                raise RuntimeError("Missed fixed logical start")
            if 3600-current <= 90:
                break
            time.sleep(min(5, (3600-current-60)/60))
        save(out / "profile-before.json", p)
        save(out / "scene-before.json", visual_stats())
        trace = (out / "session.utrace").as_posix()
        scope = (out / "slate-window-scope.json").as_posix()
        commands([f"Trace.File {trace} cpu,gpu,frame,bookmark", f"gs.Slate.Windows.Start {scope}"])
        result = editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="
                        + PERF + "\nr={'started':p.start_hotspot_window()}")
        assert result["started"], result
        print(json.dumps({"phase": "native-30s", "variant": variant, "directory": str(out)}), flush=True)
        time.sleep(33)
        after = profile()
        commands(["gs.Slate.Windows.Stop", "Trace.Stop"])
        save(out / "profile-after.json", after)
        save(out / "scene-after.json", visual_stats())
        save(out / "context-after.json", run(SNAPSHOT))
        foreground = after["counters"].get("Capture.ForegroundFrames", {}).get("total", 0)
        background = after["counters"].get("Capture.BackgroundFrames", {}).get("total", 0)
        work = after["hotspot_workload"]
        map_file = ROOT / "Content/Maps/LVL_CommanderMassPrototype.umap"
        stat = map_file.stat()
        result = {"variant": variant, "valid": bool(work.get("valid") and after["capture_valid"]
                  and foreground > 0 and background == 0), "foreground_frames": foreground,
                  "background_frames": background, "seconds": work.get("seconds"),
                  "native_frames": after["engine_frames"], "error": work.get("error"),
                  "identity": {"map": {"path": str(map_file), "size": stat.st_size, "mtime_ns": stat.st_mtime_ns},
                  "modules": after["simulation_census"]["sync_identity"]["modules"]},
                  "settings": dict(zip(CVAR_NAMES, VARIANTS[variant])), "output": str(out)}
        save(out / "capture.json", result)
        print(json.dumps(result, ensure_ascii=False), flush=True)
        assert result["valid"], result
        return frozen
    finally:
        if status().get("active"):
            commands(["gs.Slate.Windows.Stop", "Trace.Stop"])
        save(out / "stopped.json", stop_owned_pie())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--variants", default="baseline,ui,final")
    args = parser.parse_args()
    variants = args.variants.split(",")
    assert all(v in VARIANTS for v in variants)
    args.output.mkdir(parents=True, exist_ok=False)
    previous = editor("r={k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in "
                      + repr(list(CVAR_NAMES)+["gs.Perf.HotspotMask","gs.Perf.GameplayWorkers",
                              "t.IdleWhenNotForeground","Slate.bAllowThrottling"]) + "}")
    save(args.output / "settings-before.json", previous)
    recipe = None
    try:
        for round_index in range(args.rounds):
            order = variants[round_index % len(variants):] + variants[:round_index % len(variants)]
            for variant in order:
                recipe = capture(args.output / f"r{round_index+1}-{variant}", variant, recipe)
    finally:
        set_options(previous)


if __name__ == "__main__":
    main()
