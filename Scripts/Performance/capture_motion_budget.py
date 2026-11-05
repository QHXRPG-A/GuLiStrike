"""Measure the original 600-unit local PIE at the 18:38 baseline's battle tick."""
import argparse
import ctypes
import json
import time
from ctypes import wintypes
from pathlib import Path

from capture_live_pie import editor, status, stop_owned_pie

CAPTURES = Path("D:/UE5.7/PerformanceCaptures/GuLiStrike")
MAP = "/Game/Maps/LVL_CommanderMassPrototype"
BASELINE = "20261105-183827-557D4994"
ROLE_SETTINGS = "o=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrikeEditor.GuLiLocalPIESettings'))\n"
NO_NOTIFY = "unreal.PropertyAccessChangeNotifyMode.NEVER"


def focus_window(pid, preview):
    user = ctypes.WinDLL("user32")
    user.GetForegroundWindow.restype = wintypes.HWND
    user.SetForegroundWindow.argtypes = [wintypes.HWND]
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    matches = []
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user.IsWindowVisible(hwnd):
            title = ctypes.create_unicode_buffer(512)
            user.GetWindowTextW(hwnd, title, len(title))
            is_preview = any(key in title.value.lower() for key in ("preview", "预览", "netmode"))
            if (preview and is_preview) or (not preview and title.value.startswith("GuLiStrike - ")):
                matches.append(hwnd)
        return True
    user.EnumWindows(callback(visit), 0)
    if len(matches) == 1 and user.GetForegroundWindow() != matches[0]:
        user.SetForegroundWindow(matches[0])


def summarize(directory):
    report = json.loads((directory / "report.json").read_bytes())
    ctx = report["context"]
    native = ctx["project"]["native_profile"]
    samples = native["network_samples"]
    window = report["motion_queue_window"]
    dt = window["seconds"]
    transports = [s["hybrid_transport"] for s in samples]
    peers = [t["peers"][0] for t in transports]
    def diff(rows, field):
        return rows[-1][field] - rows[0][field]
    def rate(rows, field):
        return diff(rows, field) / dt / 1000
    census = [s["simulation_census"] for s in samples]
    wire = rate(peers, "lane_1_sent_bytes")
    errors = {key: diff(transports, key) for key in
              ("datagram_send_failures", "datagram_rejected_packets", "receive_queue_overflows", "peer_payload_decode_failures")}
    errors.update({key: diff(peers, key) for key in ("rejected_packets", "retransmit_bytes", "delivery_backpressure")})
    flights = [s["flights"]["active"] for s in samples]
    valid = (report["status"] == "complete" and ctx["mode"] == "PIE rendering client process"
             and ctx["map"] == MAP and ctx["cpu_window_complete"] and native["capture_valid"]
             and not native["network_samples_dropped"] and window["available"] and window["peer_count"] == 1
             and len(samples) == 6 and 4.8 <= dt <= 5.3 and not ctx["background_frames"]
             and native["counters"].get("Capture.ViewportBackgroundFrames",{}).get("total",0)==0
             and native["counters"].get("Capture.ViewportForegroundFrames",{}).get("total",0)>0
             and all(c["ready"] and c["local_simulating_soldiers"] == 300 for c in census)
             and all(t["connected"] and not t["reconnecting"] and len(t["peers"]) == 1 for t in transports)
             and 185 <= census[0]["logical_tick"] <= 240 and 285 <= diff(census, "logical_tick") <= 315
             and min(c["live_soldiers"] for c in census) >= 580 and min(flights) >= 500
             and not any(errors.values()))
    return {"directory": str(directory), "valid": bool(valid), "at_or_below_100": bool(valid and wire <= 100),
            "wire_kB_s": wire, "control_kB_s": rate(peers, "lane_0_sent_bytes"),
            "process_send_kB_s": rate(transports, "datagram_sent_bytes"), "seconds": dt, "errors": errors,
            "world_seconds": [samples[0]["world_seconds"], samples[-1]["world_seconds"]],
            "logical_ticks": [census[0]["logical_tick"], census[-1]["logical_tick"]],
            "local_soldiers": [c["local_simulating_soldiers"] for c in census], "active_flights": flights,
            "background_frames": ctx["background_frames"], "peak_gt_ms": report["game_thread_tick"]["peak_ms"],
            "p95_gt_ms": report["game_thread_tick"]["p95_ms"],
            "queue": window}


def capture():
    before = set(CAPTURES.iterdir())
    initial=status()
    if initial.get("active"):
        raise RuntimeError("A local PIE session is already active.")
    focus_window(initial["editor_pid"],False)
    # This uses the same LocalPIE launcher, coordinator and headless peer as Play.
    settings=editor(ROLE_SETTINGS+"r={'success':True,'role':o.get_editor_property('MainRole').name,"
                    "'prompt':o.get_editor_property('bPromptForRole')}")
    def restore_role():
        editor(ROLE_SETTINGS+f"o.set_editor_property('MainRole',getattr(unreal.GuLiCommanderRole,{settings['role']!r}),{NO_NOTIFY})\n"
               f"o.set_editor_property('bPromptForRole',{settings['prompt']!r},{NO_NOTIFY})\nr={{'success':True}}")
    try:
        editor(f"w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
               f"assert w.get_path_name().split('.')[0]=={MAP!r} and not unreal.EditorLevelLibrary.get_pie_worlds(True)\n"
               +ROLE_SETTINGS+f"o.set_editor_property('MainRole',unreal.GuLiCommanderRole.COMMANDER,{NO_NOTIFY})\n"
               f"o.set_editor_property('bPromptForRole',False,{NO_NOTIFY})\n"
               "unreal.SystemLibrary.execute_console_command(w,'gs.Commander.QA.StartPIE standalone 1290 994')\nr={'success':True}")
        deadline = time.monotonic() + 100
        focused = False
        while time.monotonic() < deadline:
            result = editor(
                "w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
                "r={'success':True,'triggered':False}\n"
                "if w and 'UEDPIE_' in w.get_path_name():\n"
                " p=next((v for v in unreal.ObjectIterator(unreal.GuLiPerformanceSubsystem) if v.get_outer()==w),None)\n"
                " profile=json.loads(p.get_capture_json()) if p else {}; c=profile.get('simulation_census',{})\n"
                " r['foreground']=bool(profile.get('viewport_foreground') and profile.get('application_foreground'))\n"
                " tick=c.get('logical_tick',0); r['tick']=tick\n"
                f" if not {focused!r} and tick>20:\n"
                "  unreal.SystemLibrary.execute_console_command(w,'gs.Commander.QA.FocusPIE'); r['focus']=True\n"
                " if r['foreground'] and c.get('ready') and tick>=211 and c.get('local_simulating_soldiers')==300:\n"
                "  unreal.SystemLibrary.execute_console_command(w,'gs.FrameProfiler.CaptureCPU'); r['triggered']=True")
            if result.get("focus"):
                restore_role()
                focused=True
            if focused and not result.get("foreground"):
                focus_window(initial["editor_pid"],True)
            if result.get("tick",0)>240 and not result["triggered"]:
                raise RuntimeError(f"Matching foreground viewport unavailable: {result}")
            if result["triggered"]:
                break
            time.sleep(.12 if result.get("tick",0)>175 else .45)
        else:
            raise RuntimeError("Matching room/tick unavailable: "+json.dumps(status(),ensure_ascii=False)[:2500])
        # No engine queries, UI operations or input during the native window.
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            for directory in set(CAPTURES.iterdir())-before:
                if not (directory / "report.md").is_file():
                    continue
                try:
                    return summarize(directory)
                except (OSError, json.JSONDecodeError):
                    continue
            time.sleep(.65)
        raise RuntimeError("Frame Profiler did not finish: "+json.dumps(status(),ensure_ascii=False)[:2500])
    finally:
        try:
            stop_owned_pie()
        finally:
            restore_role()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Use a fresh evidence file.")
    result = capture()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({k:v for k,v in result.items() if k!="queue"}, ensure_ascii=False), flush=True)
    raise SystemExit(0 if result["at_or_below_100"] else 2)
