"""Capture one explicitly named stage of the local 500-soldier ring fixture.

Run RingBenchSetup in standalone PIE first. This script changes only temporary
PIE/view settings, refuses to overwrite evidence, and waits for a bounded CSV.
Use RingBenchRestore (or end PIE) afterwards and restore the saved CVars.
"""

import argparse
import json
import time
from pathlib import Path

from commander_editor_python import call_editor


OUTPUT = Path(__file__).resolve().parents[1] / "outputs/commander-ring-hud-20260830"


def run_editor(code):
    response = call_editor(code)
    result = response.get("result")
    if not response.get("success") or not isinstance(result, dict) or result.get("success") is False:
        raise RuntimeError(json.dumps(response, ensure_ascii=True))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", choices=("near", "far"), required=True)
    parser.add_argument("--stage", choices=("before", "after"), required=True)
    parser.add_argument("--run", type=int, required=True)
    parser.add_argument("--frames", type=int, default=840)
    args = parser.parse_args()
    if args.run < 1 or args.frames < 720:
        parser.error("run must be positive and frames at least 720")
    name = f"{args.case}-{args.stage}-r{args.run}"
    target = OUTPUT / f"{name}.csv"
    metadata_path = OUTPUT / f"{name}.capture.json"
    if target.exists() or metadata_path.exists():
        raise FileExistsError(f"Refusing to overwrite {name} evidence")
    setup = run_editor(f'''
import unreal, json
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
if not world:
    raise RuntimeError('Standalone PIE is required')
settings = {{'t.MaxFPS': 0, 'r.VSync': 0, 'r.ScreenPercentage': 100,
            'r.DynamicRes.OperationMode': 0, 'r.GPUCsvStatsEnabled': 1}}
for quality in ('ViewDistance', 'AntiAliasing', 'Shadow', 'GlobalIllumination',
                'Reflection', 'PostProcess', 'Texture', 'Effects', 'Foliage', 'Shading'):
    settings['sg.' + quality + 'Quality'] = 3
saved = globals().setdefault('commander_ring_ab_original_cvars', {{
    key: unreal.SystemLibrary.get_console_variable_float_value(key) for key in settings}})
for key, value in settings.items():
    unreal.SystemLibrary.execute_console_command(world, key + ' ' + str(value))
for command in ('CsvCategory RHI enable', 'CsvCategory GuLiCommanderPresentation enable',
                'viewmode lit', 'gs.Commander.RingBenchView {args.case}',
                'gs.Commander.RingBenchStage {args.stage}'):
    unreal.SystemLibrary.execute_console_command(world, command)
pc = unreal.GameplayStatics.get_player_controller(world, 0)
fixture = [actor for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
           if actor.actor_has_tag('CommanderRingBench')]
if len(fixture) != 1:
    raise RuntimeError('Expected exactly one RingBench fixture; run RingBenchSetup first')
components = fixture[0].get_components_by_class(unreal.InstancedStaticMeshComponent)
meshes = [{{'component': item.get_name(), 'count': item.get_instance_count(),
           'mesh': item.get_editor_property('static_mesh').get_path_name(),
           'material': item.get_material(0).get_path_name()}} for item in components]
if len(meshes) != 2 or any(item['count'] != 500 for item in meshes):
    raise RuntimeError('Fixture does not contain 500 units and 500 rings')
unreal.MCPythonHelper.submit_result(json.dumps({{
    'success': True, 'world': world.get_path_name(), 'viewport': list(pc.get_viewport_size()),
    'fixture': fixture[0].get_name(), 'meshes': meshes, 'original_cvars': saved,
    'cvars': {{key: unreal.SystemLibrary.get_console_variable_float_value(key) for key in settings}},
    'camera_location': str(pc.player_camera_manager.get_camera_location()),
    'camera_rotation': str(pc.player_camera_manager.get_camera_rotation())
}}))
''')
    # Settle view and shader/streaming transitions outside the measured capture.
    time.sleep(4.0)
    started = run_editor(f'''
import unreal, json
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc = unreal.GameplayStatics.get_player_controller(world, 0)
unreal.SystemLibrary.execute_console_command(world, 'CsvProfile STARTFILE=../../../outputs/commander-ring-hud-20260830/{name}.csv')
unreal.SystemLibrary.execute_console_command(world, 'CsvProfile FRAMES={args.frames}')
unreal.MCPythonHelper.submit_result(json.dumps({{'success': True, 'frames': {args.frames},
    'settled_viewport': list(pc.get_viewport_size()),
    'settled_camera_location': str(pc.player_camera_manager.get_camera_location()),
    'settled_camera_rotation': str(pc.player_camera_manager.get_camera_rotation())}}))
''')
    print(f"Capturing {name}: {args.frames} frames", flush=True)
    deadline = time.monotonic() + 55.0
    complete = False
    while time.monotonic() < deadline:
        if target.exists():
            try:
                with target.open("rb") as stream:
                    stream.seek(max(0, target.stat().st_size - 32768))
                    complete = b"[hasheaderrowatend]" in stream.read().lower()
            except PermissionError:
                # Windows keeps UE's output handle exclusive until finalization.
                complete = False
            if complete:
                break
        time.sleep(0.5)
    metadata_path.write_text(json.dumps({
        "case": args.case, "stage": args.stage, "run": args.run,
        "requested_frames": args.frames, "csv_completed": complete,
        "fixture_scope": "500 transient render instances; real 500-soldier authority and hidden original presentation continue ticking",
        "setup": setup, "capture_dispatch": started,
    }, ensure_ascii=False, indent=2), encoding="utf-8")
    if not complete:
        raise TimeoutError(f"CSV did not finish within 55 seconds: {target}")
    print(f"Completed {name}: {target.stat().st_size} bytes", flush=True)


if __name__ == "__main__":
    main()
