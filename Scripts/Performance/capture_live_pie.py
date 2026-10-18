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
    parser.add_argument("--output", type=Path, help="Fresh evidence directory, including outside the project")
    parser.add_argument("--hotspot", choices=["static", "interaction"], help="Fresh three-process PIE and a native 30-second workload")
    parser.add_argument("--paired-hotspots", action="store_true", help="Three A/B, B/A, A/B pairs per scenario, each in a fresh PIE room")
    parser.add_argument("--long-hotspots", action="store_true", help="Fresh A/B rooms, formal 125-unit summon preparation and 45 minutes at 725 units each")
    parser.add_argument("--mask", type=int, choices=range(512), default=31)
    parser.add_argument("--workers", type=int, choices=(1,2,4), default=2,
                        help="Process-shared compute pool size, latched for both clients")
    parser.add_argument("--baseline-mask", type=int, choices=range(512), default=31,
                        help="Paired control; retains the previously validated optimizations")
    parser.add_argument("--recipe", type=Path, help="Frozen recipe from this scenario's first valid window")
    parser.add_argument("--warmup", type=float, default=15)
    args = parser.parse_args()
    if args.long_hotspots:
        if args.paired_hotspots or args.hotspot or args.recipe or not args.output or not 5 <= args.warmup <= 45:
            parser.error("--long-hotspots requires a fresh --output, 5..45 second warmup and no other capture mode")
        if args.baseline_mask & 31 != 31 or args.mask & args.baseline_mask != args.baseline_mask or args.mask == args.baseline_mask:
            parser.error("Both variants retain validated mask 31; B adds the new options")
        args.output.mkdir(parents=True,exist_ok=False)
        identities=[];preparations=[];report={'variants':[],'complete':False}
        for name,mask in [('A',args.baseline_mask),('B',args.mask)]:
            directory=args.output/name
            try:
                capture_hotspot(directory,'static',mask,None,args.warmup,soak=True,workers=args.workers)
                identities.append(json.loads((directory/'capture.json').read_text())['identity'])
                preparations.append(json.loads((directory/'long-preparation-request.json').read_text())['source_ids'])
                report['variants'].append({'variant':name,'mask':mask,'complete':True})
            except Exception as error:
                report['variants'].append({'variant':name,'mask':mask,'complete':False,'error':str(error)})
                print(json.dumps(report['variants'][-1]),flush=True)
            (args.output/'long-paired.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        report['complete']=len(identities)==2 and identities[0]==identities[1] and preparations[0]==preparations[1]
        (args.output/'long-paired.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        if not report['complete']:raise RuntimeError('Long pair incomplete or frozen identity/preparation differs; see long-paired.json')
        return
    if args.paired_hotspots:
        if args.hotspot or args.recipe or not args.output or not 5 <= args.warmup <= 45:
            parser.error("--paired-hotspots needs a fresh --output, 5..45 second warmup, and no --hotspot/--recipe")
        capture_pairs(args.output, args.mask, args.warmup, args.baseline_mask, args.workers)
        return
    if args.hotspot:
        if not args.output or not 5 <= args.warmup <= 45:
            parser.error("--hotspot requires --output and a 5..45 second warmup")
        capture_hotspot(args.output, args.hotspot, args.mask, args.recipe, args.warmup, workers=args.workers)
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
    if s.get("active"):
        editor("unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()\nr={'success':True}")
    deadline=time.monotonic()+15
    while time.monotonic()<deadline:
        s=status()
        if not s.get("active") and not s.get("logic_running") and not s.get("coordinator_running"):
            return s
        time.sleep(.5)
    raise RuntimeError("Owned PIE processes did not terminate")

def capture_hotspot(output, scenario, mask, recipe_path, warmup, soak=False, workers=2):
    """No Python calls, snapshots, JSON or file serialization inside the native window."""
    output=output.resolve();output.mkdir(parents=True,exist_ok=False)
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
    evidence={"scenario":scenario,"mask":mask,"workers":workers,"valid":False,"output":str(output)}
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
        before=run(SNAPSHOT);save("context-before.json",before)
        assert len(before['worlds'])==1, 'A single PIE world is required'
        view=before['worlds'][0]['controllers'][0]['viewport']
        assert view[0]==2359 and view[1] in (1748,1751), 'Unexpected actual DPI-adjusted viewport: '+str(view)
        recipe=json.loads(recipe_path.read_text(encoding='utf-8')) if recipe_path else None
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
        save("recipe.json",frozen_recipe)
        deadline=time.monotonic()+45
        while time.monotonic()<deadline:
            latest=profile();work=latest.get('hotspot_workload',{})
            if work.get('error'):raise RuntimeError(work['error'])
            if scenario=='static' or (len(work.get('damage_receipts',[]))==24 and all(h['replies']==1 and h['status'] in (0,1) for h in work['damage_receipts'])):
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
        save("profile-before.json",profile());save("scene-before.json",scene())
        commands(["gs.Slate.Prepass.Stop",f"Trace.File { (output/'session.utrace').as_posix() } cpu,frame,bookmark"])
        started=editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="+PERF+"\nr={'started':p.start_hotspot_window()}")
        if not started.get('started'):raise RuntimeError("Native window rejected its actual preparation readback")
        print(json.dumps({"window":output.name,"phase":"native-30s"}),flush=True)
        time.sleep(31)
        # The native subsystem has ended its window before any bridge query resumes.
        after=profile();commands(["Trace.Stop"])
        save("profile-after.json",after);save("scene-after.json",scene());save("context-after.json",run(SNAPSHOT))
        work=after.get('hotspot_workload',{});save("workload.json",work)
        logic=Path(s['session_directory'])/'logic.log'
        if logic.is_file():
            text=logic.read_text(encoding='utf-8',errors='replace')
            evidence['logic_mask_verified']=any('[GuLiHotspots]' in line and f'mask={mask} workers={workers}' in line for line in text.splitlines())
            shutil.copyfile(logic,output/'logic.log')
        evidence.update({"valid":bool(work.get('valid') and after.get('capture_valid') and evidence.get('logic_mask_verified')),
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
        save("capture.json",evidence)
        if not evidence['valid']:raise RuntimeError("Window is invalid: "+str(work.get('error')))
        if soak:
            capture_long_load(output,mask,save,profile)
    except Exception as error:
        evidence['error']=type(error).__name__+': '+str(error);save("capture.json",evidence)
        raise
    finally:
        try:
            if status().get('active'):commands(["gs.Slate.Prepass.Stop","Trace.Stop"])
            save("stopped.json",stop_owned_pie())
        finally:
            editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"+
                "\n".join(f"unreal.SystemLibrary.execute_console_command(w,{(k+' '+str(v))!r})" for k,v in original.items())+"\nr={'success':True}")
    print(json.dumps(evidence),flush=True)


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


def capture_long_load(output,mask,save,profile):
    """Keep one room alive for 45 minutes; ten native 5 s windows, no in-window polling.

    The only added load is a real Q summon by 25 existing pioneers. Sparse
    readback outside windows checks the census; no performance state enters the map.
    """
    prepare_long_load(save)
    # The long-run check is about age/backlog, not another short-window gain test.
    # Five seconds stays within the fixed individual-event recording capacity even
    # after autonomous construction adds objects. The paired gain windows stay 30 s.
    started=time.monotonic();report={'mask':mask,'units':725,'target_seconds':2700,'native_window_seconds':5,'samples':[],'complete':False}
    save('long-summary.json',report)
    for index,at in enumerate([0,300,600,900,1200,1500,1800,2100,2400,2695]):
        while time.monotonic()-started<at:
            time.sleep(min(30,at-(time.monotonic()-started)))
        directory=output/f'long-{index:02d}';directory.mkdir()
        census=status()
        if census.get('units')!=725 or not census.get('world_ready'):
            save('long-interrupted.json',census);raise RuntimeError('Long workload census or session changed')
        # Retain native traces at the beginning, middle and end; all ten
        # windows retain native frame and queue counters.
        trace=index in (0,5,9)
        if trace:commands([f'Trace.File {(directory/"session.utrace").as_posix()} cpu,frame,bookmark'])
        editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="+PERF+"\np.begin_capture()\nr={'success':True}")
        print(json.dumps({'long_window':index,'mask':mask,'age_seconds':time.monotonic()-started}),flush=True)
        time.sleep(5)
        editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\np="+PERF+"\np.end_capture()\nr={'success':True}")
        if trace:commands(['Trace.Stop'])
        data=profile()
        (directory/'profile.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
        if not data.get('capture_valid'):raise RuntimeError('Native long-window counter overflow')
        report['samples'].append({'directory':directory.name,'age_seconds':time.monotonic()-started,'frames':data['engine_frames'],'trace':trace})
        save('long-summary.json',report)
    report.update(complete=True,elapsed_seconds=time.monotonic()-started,final_session=status())
    if report['final_session'].get('units')!=725:raise RuntimeError('Final 725-unit readback changed')
    save('long-summary.json',report)


def capture_pairs(output, enabled_mask, warmup, baseline_mask=31, workers=2):
    if baseline_mask & 31 != 31 or enabled_mask & baseline_mask != baseline_mask or enabled_mask == baseline_mask:
        raise ValueError('Both variants must retain validated mask 31; B must add at least one optimization to A')
    output=output.resolve();output.mkdir(parents=True,exist_ok=False)
    report={'baseline_mask':baseline_mask,'enabled_mask':enabled_mask,'workers':workers,'order':['A/B','B/A','A/B'],'windows':[],'complete':False}
    identity=None
    for scenario in ('static','interaction'):
        recipe=None
        for pair,order in enumerate(((baseline_mask,enabled_mask),(enabled_mask,baseline_mask),(baseline_mask,enabled_mask)),1):
            for mask in order:
                variant='A' if mask==baseline_mask else 'B'
                name=f'{scenario}-{pair:02d}-'+variant
                directory=output/name
                try:
                    capture_hotspot(directory,scenario,mask,recipe,warmup,workers=workers)
                    result=json.loads((directory/'capture.json').read_text(encoding='utf-8'))
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
