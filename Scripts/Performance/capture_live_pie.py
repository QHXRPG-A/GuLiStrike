"""Capture an existing PIE session, or native hotspot A/B windows in fresh rooms.

Normal capture preserves the existing session. --hotspot and --paired-hotspots
explicitly create and stop owned three-process sessions with frozen workloads.
All modes preserve game quality and write to fresh evidence directories.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import json
from pathlib import Path
import sys
import time
import hashlib
import shutil

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Scripts"))
from commander_editor_python import call_editor
from hybrid_workload import HIGH_LOAD_MINIMUM, validate_minimum, combat_inputs, camera_comparison

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
            'camera_tier': str(pc.get_view_target().get_camera_tier()) if hasattr(pc.get_view_target(),'get_camera_tier') else None,
            'camera_target_height_m': pc.get_view_target().get_target_height_meters() if hasattr(pc.get_view_target(),'get_target_height_meters') else None,
            'camera_transitioning': pc.get_view_target().is_camera_transitioning() if hasattr(pc.get_view_target(),'is_camera_transitioning') else None,
            'camera_location': list(pc.player_camera_manager.get_camera_location().to_tuple()) if pc.player_camera_manager else None,
            'camera_rotation': list(pc.player_camera_manager.get_camera_rotation().to_tuple()) if pc.player_camera_manager else None}
            for pc in controllers],
        'flight_pool_count': len(flights), 'flight_tick_enabled': sum(a.is_actor_tick_enabled() for a in flights),
        'instances': instance_components})
keys = ['t.MaxFPS', 't.IdleWhenNotForeground', 'r.VSync', 'r.ScreenPercentage',
    'r.DynamicRes.OperationMode', 'r.GPUCsvStatsEnabled', 'Slate.bAllowThrottling', 'Slate.EnableGlobalInvalidation', 'gs.SceneUI.IndexedGeometry',
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
    parser.add_argument("--output", type=Path, help="Fresh evidence directory, including outside the project")
    parser.add_argument("--hotspot", choices=["static", "interaction", "combat"], help="Fresh three-process PIE and a native 30-second workload")
    parser.add_argument("--high-load", action="store_true", help="Rebuild the local-authority high load through normal capture, production and summon rules")
    parser.add_argument("--paired-hotspots", action="store_true", help="Three A/B, B/A, A/B pairs per scenario, each in a fresh PIE room")
    parser.add_argument("--long-hotspots", action="store_true", help="Fresh matched A/B rooms; 45 minutes at the selected local simulation load, then aged dense combat")
    parser.add_argument("--continuous-seconds",type=int,default=0,
                        help="Native sustained business after a high-load static window; 60..2700 seconds, only 2700 qualifies as long acceptance")
    parser.add_argument("--mask", type=int, choices=range(512), default=31)
    parser.add_argument("--workers", type=int, choices=(1,2,4), default=2,
                        help="Process-shared compute pool size, latched for both clients")
    parser.add_argument("--baseline-mask", type=int, choices=range(512), default=31,
                        help="Paired control; retains the previously validated optimizations")
    parser.add_argument("--recipe", type=Path, help="Frozen recipe from this scenario's first valid window")
    parser.add_argument("--warmup", type=float, default=15)
    args = parser.parse_args()
    if args.continuous_seconds and (not 60<=args.continuous_seconds<=2700 or args.hotspot!='static'
            or not args.high_load or args.long_hotspots or args.paired_hotspots):
        parser.error('--continuous-seconds requires --hotspot static --high-load and a 60..2700 second native schedule')
    if args.long_hotspots:
        if args.paired_hotspots or args.hotspot or args.recipe or not args.output or not 5 <= args.warmup <= 45:
            parser.error("--long-hotspots requires a fresh --output, 5..45 second warmup and no other capture mode")
        if args.baseline_mask & 31 != 31 or args.mask & args.baseline_mask != args.baseline_mask or args.mask == args.baseline_mask:
            parser.error("Both variants retain validated mask 31; B adds the new options")
        args.output.mkdir(parents=True,exist_ok=False)
        identities=[];preparations=[];report={'variants':[],'complete':False}
        recipe=None
        for name,mask in [('A',args.baseline_mask),('B',args.mask)]:
            directory=args.output/name
            try:
                capture_hotspot(directory,'static',mask,recipe,args.warmup,soak=True,workers=args.workers,high_load=args.high_load)
                identities.append(json.loads((directory/'capture.json').read_text())['identity'])
                preparation_file=directory/('high-load-preparation.json' if args.high_load else 'long-preparation-request.json')
                preparations.append(json.loads(preparation_file.read_text())['inputs' if args.high_load else 'source_ids'])
                if recipe is None:recipe=directory/'recipe.json'
                elif identities[0]!=identities[-1] or preparations[0]!=preparations[-1]:
                    raise RuntimeError('Long pair module identity or preparation input changed')
                if args.high_load and name=='B':
                    a=json.loads((args.output/'A'/'aged-combat-preparation.json').read_text())
                    b=json.loads((directory/'aged-combat-preparation.json').read_text())
                    if combat_inputs(a)!=combat_inputs(b):
                        raise RuntimeError('Aged combat participants, types or applied positions differ')
                report['variants'].append({'variant':name,'mask':mask,'complete':True})
            except Exception as error:
                report['variants'].append({'variant':name,'mask':mask,'complete':False,'error':str(error)})
                print(json.dumps(report['variants'][-1]),flush=True)
                (args.output/'long-paired.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
                raise
            (args.output/'long-paired.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        report['complete']=len(identities)==2 and identities[0]==identities[1] and preparations[0]==preparations[1]
        (args.output/'long-paired.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        if not report['complete']:raise RuntimeError('Long pair incomplete or frozen identity/preparation differs; see long-paired.json')
        return
    if args.paired_hotspots:
        if args.hotspot or args.recipe or not args.output or not 5 <= args.warmup <= 45:
            parser.error("--paired-hotspots needs a fresh --output, 5..45 second warmup, and no --hotspot/--recipe")
        capture_pairs(args.output, args.mask, args.warmup, args.baseline_mask, args.workers, args.high_load)
        return
    if args.hotspot:
        if not args.output or not 5 <= args.warmup <= 45:
            parser.error("--hotspot requires --output and a 5..45 second warmup")
        capture_hotspot(args.output, args.hotspot, args.mask, args.recipe, args.warmup, workers=args.workers,high_load=args.high_load,
                        continuous_seconds=args.continuous_seconds)
        return
    if not 1 <= args.seconds <= 60:
        parser.error("Use a capture between 1 and 60 seconds")
    output = args.output or ROOT / "outputs/performance" / (datetime.now().strftime("%Y%m%d-%H%M%S-") + args.label)
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


SESSION = "json.loads(unreal.get_editor_subsystem(unreal.GuLiLocalPIESubsystem).get_session_status())"
PERF = "next(s for s in unreal.ObjectIterator(unreal.GuLiPerformanceSubsystem) if s.get_outer()==w)"

def editor(code):
    return run("import unreal,json\n" + code + "\nunreal.MCPythonHelper.submit_result(json.dumps(r))")

def status():
    return editor("r=" + SESSION)

def stop_owned_pie():
    s=status()
    had_children=bool(s.get('active') or s.get('shutdown'))
    if s.get("active"):
        editor("unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()\nr={'success':True}")
    deadline=time.monotonic()+15
    while time.monotonic()<deadline:
        s=status()
        if not s.get("active") and not s.get("logic_running") and not s.get("coordinator_running"):
            if had_children:
                exits=s.get('shutdown',[])
                if (sorted(row['role'] for row in exits)!=['coordinator','logic']
                        or any(row.get('forced') or not row.get('has_exit_code') or row.get('exit_code')!=0 for row in exits)
                        or not s.get('cleanup_complete')):
                    raise RuntimeError('Owned PIE shutdown was not normal and complete: '+json.dumps(s))
            return s
        time.sleep(.5)
    raise RuntimeError("Owned PIE processes did not terminate")

def capture_hotspot(output, scenario, mask, recipe_path, warmup, soak=False, workers=2,high_load=False,slate_diagnostics=False,slate_windows=False,continuous_seconds=0,start_immediately=False,quick_preparation=False):
    """No Python calls, snapshots, JSON or file serialization inside the native window."""
    output=output.resolve();output.mkdir(parents=True,exist_ok=False)
    if slate_windows and ' ' in output.as_posix():
        raise ValueError('Native console capture paths must not contain spaces')
    def save(name,value):
        (output/name).write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding="utf-8")
    def profile():
        return editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="+PERF+"\nr=json.loads(p.get_capture_json())")
    def scene():
        return editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\nr={'widgets':[json.loads(x.get_frame_stats_json()) for x in unreal.ObjectIterator(unreal.GuLiSceneUIWidget) if x.get_world()==w]}")
    first=status()
    if first.get("active"):
        raise RuntimeError("Stop the existing PIE explicitly before requesting a fresh capture")
    original=editor("r={k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in ['gs.Perf.HotspotMask','gs.Perf.GameplayWorkers','t.IdleWhenNotForeground','Slate.bAllowThrottling']}")
    evidence={"scenario":scenario,"mask":mask,"workers":workers,"high_load":high_load,"valid":False,"output":str(output)}
    with (ROOT/'Content/Maps/LVL_CommanderMassPrototype.umap').open('rb') as stream:
        loaded_map_sha256=hashlib.file_digest(stream,'sha256').hexdigest()
    save("request.json",evidence)
    try:
        values=[f"gs.Perf.HotspotMask {mask}",f"gs.Perf.GameplayWorkers {workers}","t.IdleWhenNotForeground 0","Slate.bAllowThrottling 0",
                "gs.Commander.QA.StartPIE standalone 2359 1748"]
        editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"+
            "\n".join(f"unreal.SystemLibrary.execute_console_command(w,{v!r})" for v in values)+"\nr={'success':True}")
        deadline=time.monotonic()+45
        while time.monotonic()<deadline:
            s=status()
            if s.get("error") or s.get("network_error"):
                raise RuntimeError(s.get("error") or s["network_error"])
            if s.get("world_ready") and len(s.get("members",[]))==2 and all(m.get("ready") for m in s["members"]) and s.get("units")==600 and s.get("peer_owned_objects")==300 and s.get("can_issue_orders"):
                break
            time.sleep(1)
        else:raise RuntimeError("Preparation exceeded 45 seconds: room readiness")
        assert s['standalone'] and s['logic_running'] and s['coordinator_running'] and s['can_present']
        assert len({s['editor_pid'],s['logic_pid'],s['coordinator_pid']})==3
        save("session.json",s)
        print(json.dumps({"window":output.name,"phase":"warmup","scenario":scenario,"mask":mask}),flush=True)
        editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"+
            "pc=unreal.GameplayStatics.get_player_controller(w,0)\ncam=pc.get_view_target()\n"+
            "unreal.SystemLibrary.execute_console_command(w,'gs.GM.Commander.Camera.Height 133.4')\n"+
            "cam.set_actor_rotation(unreal.Rotator(pitch=0,yaw=0,roll=0),False)\n"+
            "cam.jump_to_world_location(unreal.Vector(6000,69000,930.525390625))\nr={'success':True}")
        time.sleep(warmup)
        recipe=json.loads(recipe_path.read_text(encoding='utf-8')) if recipe_path else {}
        if high_load:
            preparation=prepare_high_load(save,profile,quick_preparation)
            if recipe.get('high_load_inputs') and recipe['high_load_inputs']!=preparation['inputs']:
                raise RuntimeError('Frozen high-load preparation inputs differ')
            recipe.update(minimum_local_simulation=HIGH_LOAD_MINIMUM,high_load_inputs=preparation['inputs'])
            recipe.setdefault('start_logical_tick',9000)
        recipe.setdefault('start_logical_tick',3600)
        if start_immediately:recipe['start_logical_tick']=0
        recipe['scenario']=scenario
        if scenario=='combat':
            # Keep the actual firefight in the rendered view. The combat anchor
            # is the first living opposing WM01; use its frozen manifest region
            # for camera preparation without moving or simulating that peer unit.
            manifest=json.loads((ROOT/'Config/Hybrid/RuntimeManifest-600.json').read_text(encoding='utf-8-sig'))
            anchor=min((o for o in manifest['objects'] if o['definition']=='unit/2' and o['team']==2),key=lambda o:o['ordinal'])
            recipe.setdefault('combat_camera_focus',[float(anchor['x']),float(anchor['y'])+3500,float(anchor['z'])])
            focus=recipe['combat_camera_focus']
            editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"+
                f"unreal.GameplayStatics.get_player_controller(w,0).get_view_target().jump_to_world_location(unreal.Vector({focus[0]},{focus[1]},{focus[2]}))\nr={{'success':True}}")
            time.sleep(5)
        before=run(SNAPSHOT);save("context-before.json",before)
        assert len(before['worlds'])==1, 'A single PIE world is required'
        frozen_camera=before['worlds'][0]['controllers'][0]
        if slate_windows:
            assert not frozen_camera['camera_transitioning'] and abs(frozen_camera['camera_target_height_m']-133.4)<.01, 'Camera left the prescribed settled tactical view during preparation'
            if recipe.get('_camera'):assert camera_comparison(recipe['_camera'],frozen_camera)['comparable'], 'Paired camera tier/pose differs'
        view=before['worlds'][0]['controllers'][0]['viewport']
        assert view[0]==2359 and view[1] in (1748,1751), 'Unexpected actual DPI-adjusted viewport: '+str(view)
        if recipe and '_viewport' in recipe: assert recipe['_viewport']==view, 'Paired actual viewports differ'
        frozen=json.dumps(recipe,separators=(',',':')) if recipe else ''
        prep=editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="+PERF+
            f"\nr=json.loads(p.prepare_hotspot_workload({frozen!r},{scenario=='interaction'}))")
        save("preparation.json",prep)
        assert prep.get('hotspot_mask')==mask
        assert prep.get('gameplay_workers')==workers
        if prep.get('hotspot_workload',{}).get('error'):
            raise RuntimeError(prep['hotspot_workload']['error'])
        frozen_recipe=prep['hotspot_workload']['frozen_recipe'];frozen_recipe['_viewport']=view
        if slate_windows:frozen_recipe['_camera']=frozen_camera
        save("recipe.json",frozen_recipe)
        deadline=time.monotonic()+45
        while time.monotonic()<deadline:
            latest=profile();work=latest.get('hotspot_workload',{})
            if work.get('error'):raise RuntimeError(work['error'])
            if scenario!='interaction' or (len(work.get('damage_receipts',[]))==24 and all(h['replies']==1 and h['status'] in (0,1) for h in work['damage_receipts'])):
                break
            time.sleep(.5)
        else:raise RuntimeError("Preparation exceeded 45 seconds: damage receipts")
        if scenario=='interaction':
            # Warm both views outside the measured window, identically for A/B.
            # Otherwise the first view of the wounded group measures asset/PSO
            # creation rather than steady-state marker and HUD work.
            focus=frozen_recipe['wounded_focus']
            editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"+
                f"unreal.GameplayStatics.get_player_controller(w,0).get_view_target().jump_to_world_location(unreal.Vector({focus[0]},{focus[1]},{focus[2]}))\nr={{'success':True}}")
            time.sleep(5)
            editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"+
                "unreal.GameplayStatics.get_player_controller(w,0).get_view_target().jump_to_world_location(unreal.Vector(6000,69000,930.525390625))\nr={'success':True}")
            time.sleep(5)
        # Wait outside the capture until close to the fixed logical start. The native
        # subsystem owns the final start boundary; no bridge traffic during the window.
        scheduled=frozen_recipe.get('start_logical_tick',0)
        if scheduled:
            while True:
                current=profile()['simulation_census']['logical_tick']
                if current>=scheduled:raise RuntimeError('Preparation missed the frozen logical start')
                if scheduled-current<=90:break
                time.sleep(min(10,(scheduled-current-60)/60))
        before_profile=profile();save("profile-before.json",before_profile);save("scene-before.json",scene())
        if slate_windows:
            window_context=run(SNAPSHOT);save('context-window-start.json',window_context)
            assert camera_comparison(frozen_camera,window_context['worlds'][0]['controllers'][0])['comparable'], 'Camera changed before the native window'
        trace_commands=["gs.Slate.Prepass.Stop",f"Trace.File { (output/'session.utrace').as_posix() } cpu,frame,bookmark"]
        if slate_windows:
            scope_path=(output/'slate-window-scope.json').as_posix()
            trace_commands += [f'gs.Slate.Windows.Start {scope_path}']
        commands(trace_commands)
        started=editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="+PERF+"\nr={'started':p.start_hotspot_window()}")
        if not started.get('started'):raise RuntimeError("Native window rejected its actual preparation readback")
        print(json.dumps({"window":output.name,"phase":"native-30s"}),flush=True)
        remaining=max(0,(scheduled-before_profile['simulation_census']['logical_tick'])/60)
        time.sleep(31+remaining)
        # The native subsystem has ended its window before any bridge query resumes.
        after=profile();commands((["gs.Slate.Windows.Stop"] if slate_windows else [])+["Trace.Stop"])
        evidence['sync_identity']=after['simulation_census']['sync_identity']
        module_manifest=json.loads((ROOT/'Binaries/Win64/UnrealEditor.modules').read_text())
        evidence['rendered_modules_verified']=all(
            Path(evidence['sync_identity']['modules'][name]['path']).resolve()==
                (ROOT/'Binaries/Win64'/('UnrealEditor-'+name+'.dll')).resolve()
            and evidence['sync_identity']['modules'][name]['build_id']==module_manifest['BuildId']
            for name in ('GuLiStrike','GuLiNetCore'))
        save("profile-after.json",after)
        if slate_windows:
            scope=json.loads((output/'slate-window-scope.json').read_text(encoding='utf-8-sig'))
            if not scope.get('valid'):raise RuntimeError('Native Slate window identity/trace pairing changed')
        save("scene-after.json",scene());save("context-after.json",run(SNAPSHOT))
        work=after.get('hotspot_workload',{});save("workload.json",work)
        if work.get('frozen_recipe'):
            final_recipe=work['frozen_recipe'];final_recipe['_viewport']=view
            if slate_windows:final_recipe['_camera']=frozen_camera
            save('recipe.json',final_recipe)
        logic=Path(s['session_directory'])/'logic.log'
        if logic.is_file():
            text=logic.read_text(encoding='utf-8',errors='replace')
            evidence['logic_mask_verified']=any('[GuLiHotspots]' in line and f'mask={mask} workers={workers}' in line for line in text.splitlines())
            evidence['logic_sync_lines']=[line for line in text.splitlines() if '[GuLiHybridSync]' in line]
            evidence['logic_module_lines']=[line for line in text.splitlines() if '[GuLiHybridSyncModule]' in line]
            evidence['logic_modules_verified']=all(any(
                ('name='+name+' path='+str(ROOT/'Binaries/Win64'/('UnrealEditor-'+name+'.dll'))+
                 ' build_id='+module_manifest['BuildId']).replace('\\','/').lower() in line.replace('\\','/').lower()
                for line in evidence['logic_module_lines']) for name in ('GuLiStrike','GuLiNetCore'))
            evidence['logic_sync_verified']=any(
                'implementation='+evidence['sync_identity']['implementation'] in line
                for line in evidence['logic_sync_lines'])
            shutil.copyfile(logic,output/'logic.log')
        evidence.update({"valid":bool(work.get('valid') and after.get('capture_valid') and evidence.get('logic_mask_verified')
            and evidence.get('logic_sync_verified') and evidence.get('rendered_modules_verified') and evidence.get('logic_modules_verified')),
            "seconds":work.get('seconds'),"native_frames":after.get('engine_frames'),"error":work.get('error'),
            "trace_bytes":(output/'session.utrace').stat().st_size})
        paths={'map':ROOT/'Content/Maps/LVL_CommanderMassPrototype.umap','manifest':ROOT/'Config/Hybrid/RuntimeManifest-600.json',
               'game_dll':ROOT/'Binaries/Win64/UnrealEditor-GuLiStrike.dll','editor_dll':ROOT/'Binaries/Win64/UnrealEditor-GuLiStrikeEditor.dll',
               'core_dll':ROOT/'Binaries/Win64/UnrealEditor-GuLiNetCore.dll',
               'coordinator':ROOT/'Tools/GuLiCoordinator/build/windows/Release/GuLiCoordinator.exe'}
        evidence['identity']={}
        for key,path in paths.items():
            with path.open('rb') as stream:
                evidence['identity'][key]=hashlib.file_digest(stream,'sha256').hexdigest()
        evidence['map_after_preparation_sha256']=evidence['identity']['map']
        evidence['identity']['map']=loaded_map_sha256
        save("capture.json",evidence)
        if not evidence['valid']:raise RuntimeError("Window is invalid: "+str(work.get('error')))
        if slate_diagnostics:
            # Per-widget callbacks perturb timing. Resolve window ownership only
            # after the native window and Trace have both finished.
            diagnostic_path=(output/'slate-diagnostics.json').as_posix()
            if ' ' in diagnostic_path:raise ValueError('The diagnostic console path must not contain spaces')
            commands([f'gs.Slate.Prepass.Start {diagnostic_path}'])
            time.sleep(3)
            commands(['gs.Slate.Prepass.Stop'])
        if soak:
            capture_long_load(output,mask,save,profile,high_load)
        if continuous_seconds:
            capture_continuous_load(output,continuous_seconds,save)
    except Exception as error:
        evidence['error']=type(error).__name__+': '+str(error);save("capture.json",evidence)
        raise
    finally:
        try:
            if status().get('active'):commands(["gs.Slate.Prepass.Stop","gs.Slate.Windows.Stop","Trace.Stop"])
            save("stopped.json",stop_owned_pie())
        finally:
            editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"+
                "\n".join(f"unreal.SystemLibrary.execute_console_command(w,{(k+' '+str(v))!r})" for k,v in original.items())+"\nr={'success':True}")
    print(json.dumps({k:evidence.get(k) for k in ('scenario','valid','seconds','native_frames','error','output')}
                     | {'implementation':evidence.get('sync_identity',{}).get('implementation')}),flush=True)


def prepare_high_load(save,profile,quick=False):
    """Only positions are prepared directly. Ownership, gifts, production and
    summons use their unchanged gameplay rules and must pass live census checks.
    """
    territories=editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="+PERF+
        "\nr=json.loads(p.prepare_hotspot_territories(32))")
    save('high-load-territories.json',territories)
    if territories.get('error'):raise RuntimeError(territories['error'])
    # The unchanged ordinary-army cap is 300. Fifteen heavy defenders fight the
    # existing opposing army; ordinary barracks refill those slots with pioneers.
    # Keep the 32 capture participants out of this independent preparation group.
    combat=editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="+PERF+
        "\nr=json.loads(p.prepare_hotspot_combat(15,2,32))")
    save('high-load-production-combat.json',combat)
    if combat.get('error'):raise RuntimeError(combat['error'])
    sources=editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
        "entry=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiHybridNetworkReviewActor)[0]\n"
        "objects=json.loads(entry.get_ownership_status())['objects']\n"
        f"with open({str(ROOT/'Config/Hybrid/RuntimeManifest-600.json')!r},encoding='utf-8-sig') as stream: manifest=json.load(stream)\n"
        "manifest={o['id'].upper():o for o in manifest['objects'] if o['definition']=='unit/1'}\n"
        "units=sorted([manifest[o['id']] for o in objects if o['id'] in manifest and o['life']==1 and o['local_simulation'] and (manifest[o['id']]['ordinal']-1)%25!=12],key=lambda o:o['ordinal'])[:73]\n"
        "assert len(units)==73\nr={'units':[{'ordinal':u['ordinal'],'id':u['id']} for u in units]}")['units']
    receipts=[]
    for batch in range(0,len(sources),25):
        selected=sources[batch:batch+25];ids=[u['ordinal'] for u in selected]
        request=editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="+PERF+
            f"\nselection=json.loads(p.prepare_hotspot_selection({ids!r}))\nassert not selection.get('error'),selection\n"
            "skills=unreal.GameplayStatics.get_player_controller(w,0).player_state.get_component_by_class(unreal.GuLiCommanderSkillComponent)\n"
            "request=skills.request_hybrid_unit_skills(False,unreal.Vector(0,0,0))\n"
            "r={'request':unreal.GuidLibrary.conv_guid_to_string(request),'selection':selection}")
        save(f'high-load-summon-{batch//25}-request.json',request)
        deadline=time.monotonic()+45
        while time.monotonic()<deadline:
            reply=editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
                "skills=unreal.GameplayStatics.get_player_controller(w,0).player_state.get_component_by_class(unreal.GuLiCommanderSkillComponent)\n"
                "reply=skills.get_last_reply()\n"
                "r={'request':unreal.GuidLibrary.conv_guid_to_string(reply.request_id),'code':str(reply.code),'error':reply.error,'units':[{'soldier':u.soldier_id.get_editor_property('Value'),'succeeded':u.execution.succeeded,'pending':u.execution.pending_commit,'error':u.execution.error} for u in reply.units]}")
            if reply['request']==request['request'] and len(reply['units'])==len(selected) and not any(u['pending'] for u in reply['units']):break
            time.sleep(.5)
        else:raise RuntimeError('High-load summon final receipt timed out')
        save(f'high-load-summon-{batch//25}-receipt.json',reply);receipts.append(reply)
        if not all(u['succeeded'] for u in reply['units']):raise RuntimeError('High-load summon was rejected: '+str(reply))
    deadline=time.monotonic()+(45 if quick else 110);last_error='';primed=set()
    while time.monotonic()<deadline:
        if quick:
            # Test preparation only. Prime one ordinary production cycle on each
            # ready producer; population, payment, ownership and births still use
            # normal commits. Do not carry an accelerated timer into the window.
            result=editor(f"r=json.loads(unreal.get_editor_subsystem(unreal.GuLiLocalPIESubsystem).prepare_performance_production({sorted(primed)!r}))")
            primed.update(result['primed'])
        census=profile()['simulation_census']
        try:validate_minimum(census)
        except ValueError as error:last_error=str(error)
        else:break
        time.sleep(2)
    else:
        save('high-load-census-failed.json',census)
        raise RuntimeError(last_error)
    result={'inputs':{'territories':[{k:v for k,v in row.items() if k in ('ordinal','object','territory','applied')} for row in territories['positions']],
                      'production_combat':[{k:v for k,v in row.items() if k in ('ordinal','object','applied')} for row in combat['positions']],
                      'summon_sources':sources},'census':census,'receipts':receipts}
    save('high-load-preparation.json',result)
    if quick:save('high-load-timer-preparation.json',{'primed':sorted(primed),'scope':'Preparation only; one existing 10-second barracks cycle, normal birth commits and population limits.'})
    print(json.dumps({'phase':'high-load-ready','local_soldiers':census['local_simulating_soldiers'],
        'by_definition':{k:v['local_simulation'] for k,v in census['by_definition'].items() if k in HIGH_LOAD_MINIMUM}}),flush=True)
    return result


def prepare_long_load(save):
    """Real selection and summon inputs, matching receipt and 725-unit readback."""
    selected=editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
            "entry=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiHybridNetworkReviewActor)[0]\n"
            "objects=json.loads(entry.get_ownership_status())['objects']\n"
            f"with open({str(ROOT/'Config/Hybrid/RuntimeManifest-600.json')!r},encoding='utf-8-sig') as stream: manifest=json.load(stream)\n"
            "manifest={o['id'].upper():o for o in manifest['objects'] if o['definition']=='unit/1'}\n"
            # The centre of each initial 25-unit square has no complete summon space.
            # Freeze 25 other source identities; do not bypass clearance or births.
            "units=sorted([manifest[o['id']] for o in objects if o['id'] in manifest and o['life']==1 and o['local_simulation'] and (manifest[o['id']]['ordinal']-1)%25!=12],key=lambda o:o['ordinal'])[:25]\n"
            "assert len(units)==25\np="+PERF+"\n"
            "r=json.loads(p.prepare_hotspot_selection([u['ordinal'] for u in units]))\nassert not r.get('error'),r")
    save('long-preparation-selection.json',selected)
    prepared=editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
            "entry=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiHybridNetworkReviewActor)[0]\n"
            "units=[o for o in json.loads(entry.get_ownership_status())['objects'] if o['selected']]\n"
            "assert len(units)==25 and all(o['life']==1 and o['definition']=='unit/1' and o['local_simulation'] for o in units), 'Formal selection must contain exactly 25 live pioneers'\n"
            "pc=unreal.GameplayStatics.get_player_controller(w,0)\n"
            "skills=pc.player_state.get_component_by_class(unreal.GuLiCommanderSkillComponent)\n"
            "request=skills.request_hybrid_unit_skills(False,unreal.Vector(0,0,0))\n"
            "r={'request':unreal.GuidLibrary.conv_guid_to_string(request),'source_ids':sorted(o['id'] for o in units)}")
    save('long-preparation-request.json',prepared)
    deadline=time.monotonic()+45
    while time.monotonic()<deadline:
        s=status()
        if s.get('error') or s.get('network_error'):raise RuntimeError(s.get('error') or s['network_error'])
        if s.get('units')==725:break
        time.sleep(1)
    else:
        save('long-preparation-readback.json',s)
        raise RuntimeError('Long preparation did not reach exactly 725 units within 45 seconds')
    save('long-preparation-readback.json',s)
    reply=editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
        "skills=unreal.GameplayStatics.get_player_controller(w,0).player_state.get_component_by_class(unreal.GuLiCommanderSkillComponent)\n"
        "reply=skills.get_last_reply()\n"
        "r={'request':unreal.GuidLibrary.conv_guid_to_string(reply.request_id),'code':str(reply.code),'error':reply.error,'units':[{'soldier':u.soldier_id.get_editor_property('Value'),'code':str(u.code),'succeeded':u.execution.succeeded,'pending':u.execution.pending_commit,'error':u.execution.error} for u in reply.units]}")
    save('long-preparation-receipt.json',reply)
    if reply['request']!=prepared['request'] or len(reply['units'])!=25 or not all(u['succeeded'] and not u['pending'] for u in reply['units']):
        raise RuntimeError('Long preparation lacks its matching 25-member skill receipt')
    return prepared,s,reply


def capture_continuous_load(output,seconds,save):
    """UE owns every business input, checkpoint and timing boundary after start."""
    directory=output/'continuous'
    accepted=editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="+PERF+
        f"\nr={{'success':p.begin_continuous_hotspot({directory.as_posix()!r},{seconds})}}")
    if not accepted.get('success'):raise RuntimeError('Native continuous workload preparation rejected')
    started=time.monotonic();deadline=started+seconds+60
    # No editor/PIE calls, screenshots or runtime Python queries. Read only the
    # files written by the native scheduler, between its sparse trace windows.
    while time.monotonic()<deadline and not (directory/'observation.json').exists():
        time.sleep(30)
    path=directory/'observation.json'
    if not path.is_file():raise RuntimeError('Native continuous workload did not save completion evidence')
    data=json.loads(path.read_text(encoding='utf-8-sig'))
    work=data.get('continuous_workload',{})
    if not data.get('complete') or not work.get('complete') or work.get('error') or data.get('dropped_samples'):
        raise RuntimeError('Native continuous workload failed: '+str(work.get('error')))
    # Trace analysis runs after the whole native schedule. Each window keeps
    # this exact module/map identity, not a later rebuilt DLL identity.
    shutil.copyfile(output/'capture.json',directory/'capture.json')
    from analyze_continuous_hotspot import summarize
    report=summarize(data)
    (directory/'business-analysis.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    save('continuous-summary.json',{'directory':directory.name,'seconds':seconds,
         'formal_45_minute_coverage':report['formal_45_minute_coverage'],
         'sustained_successful_business':report['sustained_successful_business'],
         'elapsed_seconds':time.monotonic()-started})
    if not report['sustained_successful_business']:raise RuntimeError('Continuous workload had an interval without successful required business')


def capture_long_load(output,mask,save,profile,high_load=False):
    """45 minutes at full load, followed by one minute of aged dense combat.

    Normal combat casualties are recorded separately after the full-load soak;
    they cannot turn a reduced population into a passing 5 ms sample.
    """
    if not high_load:prepare_long_load(save)
    def observation(name):
        path=output/name
        # Serialize once outside timing and return only metadata over the bridge.
        editor("from pathlib import Path\nw=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="+PERF+
            f"\nPath({path.as_posix()!r}).write_text(p.get_long_observation_json(),encoding='utf-8')\nr={{'saved':True}}")
        return json.loads(path.read_text(encoding='utf-8'))
    # The long-run check is about age/backlog, not another short-window gain test.
    # Five seconds stays within the fixed individual-event recording capacity even
    # after autonomous construction adds objects. The paired gain windows stay 30 s.
    started=time.monotonic();report={'mask':mask,'high_load':high_load,'units':None if high_load else 725,
        'minimum_local_simulation':HIGH_LOAD_MINIMUM if high_load else None,
        'target_seconds':2700,'native_window_seconds':5,'samples':[],'complete':False}
    observed=editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="+PERF+"\nr={'success':p.begin_long_observation(2700)}")
    if not observed.get('success'):raise RuntimeError('Native long observation rejected')
    save('long-summary.json',report)
    for index,at in enumerate([0,300,600,900,1200,1500,1800,2100,2400,2695]):
        while time.monotonic()-started<at:
            time.sleep(min(30,at-(time.monotonic()-started)))
        directory=output/f'long-{index:02d}';directory.mkdir()
        census=status()
        if not census.get('world_ready') or (not high_load and census.get('units')!=725):
            save('long-interrupted.json',census);raise RuntimeError('Long workload census or session changed')
        simulation=profile()['simulation_census']
        if high_load:validate_minimum(simulation)
        # Retain native traces at the beginning, middle and end; all ten
        # windows retain native frame and queue counters.
        trace=index in (0,5,9)
        if trace:commands([f'Trace.File {(directory/"session.utrace").as_posix()} cpu,frame,bookmark'])
        editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="+PERF+"\nr={'success':p.begin_timed_capture(5)}")
        print(json.dumps({'long_window':index,'mask':mask,'age_seconds':time.monotonic()-started}),flush=True)
        time.sleep(6)
        if trace:commands(['Trace.Stop'])
        data=profile()
        (directory/'profile.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
        if not data.get('capture_valid') or data.get('capturing'):raise RuntimeError('Native long window overflow or missing stop boundary')
        if high_load:validate_minimum(data['simulation_census'])
        report['samples'].append({'directory':directory.name,'age_seconds':time.monotonic()-started,'frames':data['engine_frames'],'trace':trace,
            'simulation_census':data['simulation_census']})
        save('long-summary.json',report)
    observations=observation('long-observation.json')
    if not observations.get('complete') or observations.get('dropped_samples') or observations.get('sampled_seconds',0)<2700:
        raise RuntimeError('Native long observation incomplete or overflowed')
    if high_load:
        for row in observations['network_samples']:validate_minimum(row['simulation_census'])
    report.update(complete=True,elapsed_seconds=time.monotonic()-started,final_session=status())
    if not high_load and report['final_session'].get('units')!=725:raise RuntimeError('Final 725-unit readback changed')
    if high_load:validate_minimum(profile()['simulation_census'])
    save('long-summary.json',report)
    if high_load:
        before=profile();validate_minimum(before['simulation_census'])
        prepared=editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="+PERF+"\nr=json.loads(p.prepare_hotspot_combat(150))")
        save('aged-combat-preparation.json',prepared)
        if prepared.get('error') or len(prepared.get('positions',[]))!=150:
            raise RuntimeError('Aged combat preparation failed: '+str(prepared))
        anchor=prepared['enemy_position'];focus=[anchor[0],anchor[1]+3500,anchor[2]]
        editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"+
            f"unreal.GameplayStatics.get_player_controller(w,0).get_view_target().jump_to_world_location(unreal.Vector({focus[0]},{focus[1]},{focus[2]}))\nr={{'success':True}}")
        save('aged-combat-camera.json',{'focus':focus})
        report['aged_combat']={'initial_census':before['simulation_census'],'preparation':prepared,'samples':[],
            'scope':'Normal casualties after 45 full-load minutes. These samples test aged combat/queues, not the full-population 5 ms target.',
            'complete':False}
        save('long-summary.json',report)
        observed=editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="+PERF+"\nr={'success':p.begin_long_observation(60)}")
        if not observed.get('success'):raise RuntimeError('Aged combat observation rejected')
        combat_started=time.monotonic()
        for index,at in enumerate((0,30,55)):
            while time.monotonic()-combat_started<at:
                time.sleep(min(10,at-(time.monotonic()-combat_started)))
            directory=output/f'aged-combat-{index:02d}';directory.mkdir()
            commands([f'Trace.File {(directory/"session.utrace").as_posix()} cpu,frame,bookmark'])
            accepted=editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="+PERF+"\nr={'success':p.begin_timed_capture(5)}")
            if not accepted.get('success'):raise RuntimeError('Aged native combat window rejected')
            print(json.dumps({'aged_combat_window':index,'mask':mask,'age_seconds':time.monotonic()-started}),flush=True)
            time.sleep(6)
            commands(['Trace.Stop']);data=profile()
            (directory/'profile.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
            if not data.get('capture_valid') or data.get('capturing') or not data['simulation_census'].get('ready'):
                raise RuntimeError('Aged combat capture or session invalid')
            report['aged_combat']['samples'].append({'directory':directory.name,'age_seconds':time.monotonic()-started,
                'frames':data['engine_frames'],'simulation_census':data['simulation_census']})
            save('long-summary.json',report)
        observed=observation('aged-combat-observation.json')
        if not observed.get('complete') or observed.get('dropped_samples') or observed.get('sampled_seconds',0)<60:
            raise RuntimeError('Aged combat native observation incomplete')
        report['aged_combat']['complete']=True
        report['elapsed_with_combat_seconds']=time.monotonic()-started
        save('long-summary.json',report)


def capture_pairs(output, enabled_mask, warmup, baseline_mask=31, workers=2,high_load=False):
    if baseline_mask & 31 != 31 or enabled_mask & baseline_mask != baseline_mask or enabled_mask == baseline_mask:
        raise ValueError('Both variants must retain validated mask 31; B must add at least one optimization to A')
    output=output.resolve();output.mkdir(parents=True,exist_ok=False)
    scenarios=['static','interaction','combat'] if high_load else ['static','interaction']
    report={'baseline_mask':baseline_mask,'enabled_mask':enabled_mask,'workers':workers,'high_load':high_load,'scenarios':scenarios,'order':['A/B','B/A','A/B'],'windows':[],'complete':False}
    identity=None
    for scenario in scenarios:
        recipe=None
        for pair,order in enumerate(((baseline_mask,enabled_mask),(enabled_mask,baseline_mask),(baseline_mask,enabled_mask)),1):
            for mask in order:
                variant='A' if mask==baseline_mask else 'B'
                name=f'{scenario}-{pair:02d}-'+variant
                directory=output/name
                try:
                    capture_hotspot(directory,scenario,mask,recipe,warmup,workers=workers,high_load=high_load)
                    result=json.loads((directory/'capture.json').read_text(encoding='utf-8'))
                    if result.get('sync_identity',{}).get('implementation')!='hybrid-sync-selectable-v1':
                        raise RuntimeError('This build has no selectable old sync path; use frozen-build comparison, never mask 31/223 A/B')
                    if identity is None:identity=result['identity']
                    if result['identity']!=identity:raise RuntimeError('Frozen map/manifest/module identity changed; stop dependent paired captures')
                    if recipe is None:recipe=directory/'recipe.json'
                    report['windows'].append({'directory':name,'pair':pair,'variant':variant,'scenario':scenario,'valid':True})
                except Exception as error:
                    report['windows'].append({'directory':name,'pair':pair,'variant':variant,'scenario':scenario,'valid':False,'error':str(error)})
                    (output/'paired.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
                    # Preserve the first failure. A changed shared baseline cannot be
                    # repaired by silently continuing or relabelling old windows.
                    raise
                (output/'paired.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    report.update(complete=True,identity=identity)
    (output/'paired.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')


if __name__ == '__main__':
    main()
