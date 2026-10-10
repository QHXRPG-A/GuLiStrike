"""Capture the existing PIE session without restarting it or changing game quality.

This profiling helper writes CSV/Insights evidence and context to a fresh output
directory. Only GPU CSV instrumentation is enabled temporarily, then restored.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Scripts"))
from commander_editor_python import call_editor

SNAPSHOT = '''
import unreal, json, collections
es = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
worlds = unreal.EditorLevelLibrary.get_pie_worlds(True)
rows = []
for w in worlds:
    actors = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor)
    controllers = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.PlayerController)
    flights = [a for a in actors if a.get_class().get_name() == 'GuLiFlightVisualActor']
    instance_components = []
    for a in actors:
        if a.get_class().get_name() == 'GuLiCommanderPresentationActor':
            instance_components = [{'name': c.get_name(), 'count': c.get_instance_count(),
                'mesh': c.static_mesh.get_path_name() if c.static_mesh else None}
                for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent)]
    rows.append({'world': w.get_path_name(), 'time_seconds': unreal.GameplayStatics.get_time_seconds(w),
        'actor_count': len(actors), 'classes': dict(collections.Counter(a.get_class().get_name() for a in actors)),
        'controllers': [{'name': pc.get_name(), 'local': pc.is_local_player_controller(),
            'viewport': list(pc.get_viewport_size()),
            'view_target': pc.get_view_target().get_actor_label() if pc.get_view_target() else None,
            'auto_manage_camera': pc.get_editor_property('bAutoManageActiveCameraTarget'),
            'camera_location': list(pc.player_camera_manager.get_camera_location().to_tuple()) if pc.player_camera_manager else None,
            'camera_rotation': list(pc.player_camera_manager.get_camera_rotation().to_tuple()) if pc.player_camera_manager else None}
            for pc in controllers],
        'flight_pool_count': len(flights), 'flight_tick_enabled': sum(a.is_actor_tick_enabled() for a in flights),
        'instances': instance_components})
keys = ['t.MaxFPS', 't.IdleWhenNotForeground', 'r.VSync', 'r.ScreenPercentage',
    'r.DynamicRes.OperationMode', 'r.GPUCsvStatsEnabled', 'Slate.bAllowThrottling',
    'r.RayTracing', 'r.Lumen.HardwareRayTracing', 'r.Shadow.Virtual.Enable'] + [
    'sg.' + s + 'Quality' for s in ['ViewDistance', 'AntiAliasing', 'Shadow', 'GlobalIllumination',
    'Reflection', 'PostProcess', 'Texture', 'Effects', 'Foliage', 'Shading']]
settings = {}
for clsname, props in [('LevelEditorPlaySettings', ['PlayNumberOfClients', 'RunUnderOneProcess',
    'PlayNetMode', 'bLaunchSeparateServer', 'NewWindowWidth', 'NewWindowHeight']),
    ('EditorPerformanceSettings', ['bThrottleCPUWhenNotForeground'])]:
    cls = unreal.load_class(None, '/Script/UnrealEd.' + clsname)
    obj = unreal.get_default_object(cls) if cls else None
    for prop in props:
        try: settings[clsname + '/' + prop] = str(obj.get_editor_property(prop))
        except Exception: pass
unreal.MCPythonHelper.submit_result(json.dumps({'success': True,
    'engine': unreal.SystemLibrary.get_engine_version(), 'worlds': rows,
    'settings': settings,
    'cvars': {k: unreal.SystemLibrary.get_console_variable_float_value(k) for k in keys}}))
'''


def run(code: str) -> dict:
    response = call_editor(code, timeout=30)
    result = response.get("result")
    if not response.get("success") or not isinstance(result, dict) or result.get("success") is False:
        raise RuntimeError(json.dumps(response))
    return result


def commands(values: list[str]) -> dict:
    return run("import unreal,json\n"
        "w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
        "assert w, 'An existing PIE session is required'\n"
        + "\n".join(f"unreal.SystemLibrary.execute_console_command(w,{v!r})" for v in values)
        + "\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True}))")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--label", default="baseline")
    args = parser.parse_args()
    if not 1 <= args.seconds <= 60:
        parser.error("Use a capture between 1 and 60 seconds")
    output = ROOT / "outputs/performance" / (datetime.now().strftime("%Y%m%d-%H%M%S-") + args.label)
    output.mkdir(parents=True, exist_ok=False)
    before = run(SNAPSHOT)
    assert before["worlds"], "An existing PIE session is required"
    (output / "context-before.json").write_text(json.dumps(before, indent=2), encoding="utf-8")
    log_path = ROOT / "Saved/Logs/GuLiStrike.log"
    log_offset = log_path.stat().st_size
    original_gpu_csv = before["cvars"]["r.GPUCsvStatsEnabled"]
    csv_path = output / "frames.csv"
    trace_path = output / "session.utrace"
    relative_csv = csv_path.relative_to(ROOT).as_posix()
    print(json.dumps({"output": str(output), "started": True, "seconds": args.seconds}), flush=True)
    started = time.monotonic()
    (output / 'clock.json').write_text(json.dumps({'monotonic_begin': started}), encoding='utf-8')
    try:
        commands(['r.GPUCsvStatsEnabled 1',
            f'Trace.File {trace_path.as_posix()} cpu,gpu,frame,bookmark',
            'Trace.Bookmark PIEBaselineStart',
            f'CsvProfile STARTFILE=../../../{relative_csv}', 'CsvProfile START'])
        time.sleep(args.seconds)
    finally:
        commands(['Trace.Bookmark PIEBaselineEnd', 'CsvProfile STOP', 'Trace.Stop',
            f'r.GPUCsvStatsEnabled {original_gpu_csv:g}'])
    elapsed = time.monotonic() - started
    (output / 'clock.json').write_text(json.dumps({'monotonic_begin': started,
        'monotonic_end': time.monotonic()}), encoding='utf-8')
    after = run(SNAPSHOT)
    (output / "context-after.json").write_text(json.dumps(after, indent=2), encoding="utf-8")
    deadline = time.monotonic() + 15
    complete = False
    while time.monotonic() < deadline:
        if csv_path.is_file():
            try:
                with csv_path.open('rb') as stream:
                    stream.seek(max(0, csv_path.stat().st_size - 65536))
                    complete = b'[hasheaderrowatend]' in stream.read().lower()
            except PermissionError:
                pass
            if complete: break
        time.sleep(0.5)
    with log_path.open('rb') as stream:
        stream.seek(log_offset)
        (output / 'log-window.txt').write_bytes(stream.read())
    result = {'success': complete and trace_path.is_file(), 'output': str(output),
        'requested_seconds': args.seconds, 'wall_seconds': elapsed,
        'csv_complete': complete, 'trace_bytes': trace_path.stat().st_size if trace_path.is_file() else 0,
        'gpu_csv_restored': after['cvars']['r.GPUCsvStatsEnabled'] == original_gpu_csv,
        'scope': 'Existing PIE process; all active worlds and views; no gameplay, quality, camera or asset changes'}
    (output / 'capture.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
