"""Authorized fixed-view one-server/two-client comparisons in the existing saved map.
Every candidate gets three alternating pairs, 10 s warmup and 30 s capture.
Only transient runtime/PIE fixture properties change; production asset references never change.
"""
from __future__ import annotations
import argparse, json, time, subprocess, sys, csv, statistics, traceback
from pathlib import Path
from datetime import datetime
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Scripts'))
from commander_editor_python import call_editor
from Performance.capture_live_pie import SNAPSHOT
OUT=ROOT/'outputs/performance/20261009-implementation'


def run(code,timeout=45):
    r=call_editor('import unreal,json\n'+code,timeout=timeout)
    if not r.get('success') or not isinstance(r.get('result'),dict) or r['result'].get('success') is False:
        raise RuntimeError(json.dumps(r,ensure_ascii=False))
    return r['result']


def result(code):
    return run(code+"\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True}))")


def snapshot():
    r=call_editor(SNAPSHOT,timeout=45)
    if not r.get('success') or not r.get('result',{}).get('success'):raise RuntimeError(str(r))
    return r['result']


def stop():
    result("unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()")
    deadline=time.monotonic()+30
    while time.monotonic()<deadline:
        state=run("unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'worlds':len(unreal.EditorLevelLibrary.get_pie_worlds(True))}))")
        if state['worlds']==0:return
        time.sleep(.5)
    raise RuntimeError('Our capture PIE did not stop')


SETUP="ws=unreal.EditorLevelLibrary.get_pie_worlds(True)\nassert len(ws)==3,'One dedicated server and two clients required'\nserver=ws[0]\npaused=0\nfor a in unreal.GameplayStatics.get_all_actors_of_class(server,unreal.Actor):\n p=a.get_component_by_class(unreal.GuLiBuildingProductionComponent)\n if p and case not in ('runtime','flight'):p.set_component_tick_enabled(False);paused+=1\nunreal.SystemLibrary.execute_console_command(server,'guli.stronghold.CaptureSeconds 1000000000')\nfor w in ws[1:]:\n pc=unreal.GameplayStatics.get_player_controller(w,0)\n assert unreal.GuLiComponentSkillQALibrary.set_performance_viewport_size(pc,1280,720)\n cam=None\n for candidate_camera in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.CameraActor):\n  if candidate_camera.get_actor_label()=='PerfReview_Camera':cam=candidate_camera;break\n assert cam,'Missing saved camera in '+w.get_path_name()\n if case in ('runtime','flight'):\n  marker=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(server,unreal.Actor) if unreal.Name('FlightEventsQAOrigin') in a.tags)\n  o=marker.get_actor_location();cam.set_actor_location(unreal.Vector(o.x+15000,o.y-10000,o.z+22000),False,True)\n  cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(cam.get_actor_location(),unreal.Vector(o.x+15000,o.y,o.z+4500)),True)\n else:\n  cam.set_actor_location(unreal.Vector(-14900,-24500,7000),False,True)\n  cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(cam.get_actor_location(),unreal.Vector(-14900,-19500,1500)),True)\n pc.set_editor_property('bAutoManageActiveCameraTarget',False)\n pc.set_view_target_with_blend(cam,0)\nif case in ('runtime','flight'):\n ship=unreal.GameplayStatics.get_all_actors_of_class(server,unreal.GuLiStrikeShip)[0]\n marker=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(server,unreal.Actor) if unreal.Name('FlightEventsQAOrigin') in a.tags)\n o=marker.get_actor_location();ship.set_actor_location(unreal.Vector(o.x,o.y,o.z+9100),False,True)\n if not ship.get_hangar_capability():\n  reason=unreal.GuLiComponentSkillQALibrary.commit_ship_choice(ship.player_state.get_ship_build(),'08')\n  assert reason is not None,reason\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'paused_producers':paused}))\n"

COUNTERS="ws=unreal.EditorLevelLibrary.get_pie_worlds(True)\nr={'success':True,'worlds':[]}\nfields=['client_flight_actor_capacity','client_flight_actor_active','client_flight_data_capacity','client_flight_data_active','client_flight_data_reuses','client_flight_data_releases','client_flight_data_epoch','pose_cache_hits','pose_cache_misses','niagara_array_uploads','component_count','last_update_milliseconds']\nfor w in ws:\n row={'world':w.get_path_name()}\n p=next((s for s in unreal.ObjectIterator(unreal.GuLiPerformanceSubsystem) if s.get_outer()==w),None)\n if p:row['profile']=json.loads(p.get_capture_json())\n reg=next((s for s in unreal.ObjectIterator(unreal.GuLiSceneUISourceRegistry) if s.get_outer()==w),None)\n if reg:row['registry']=json.loads(reg.get_registry_stats_json())\n sub=next((s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if s.get_outer()==w),None)\n if sub:\n  c=sub.get_counters();row['flight']={f:c.get_editor_property(f) for f in fields}\n  states=sub.get_effect_states();row['flight_by_source']={}\n  for e in states:\n   k=str(e.source.kind);row['flight_by_source'][k]=row['flight_by_source'].get(k,0)+1\n fixture=next((s for s in unreal.ObjectIterator(unreal.GuLiFlightAcceptanceSubsystem) if s.get_outer()==w),None)\n if fixture:row['load']=json.loads(fixture.get_load_stats_json())\n if w==ws[0]:\n  rep=unreal.GameplayStatics.get_game_state(w).get_component_by_class(unreal.GuLiCombatEffectReplicationComponent)\n  if rep:row['flight_delivery']=rep.get_flight_diagnostics()\n row['ui']=[json.loads(x.get_frame_stats_json()) for x in unreal.ObjectIterator(unreal.GuLiSceneUIWidget) if x.get_world()==w]\n r['worlds'].append(row)\nunreal.MCPythonHelper.submit_result(json.dumps(r))\n"

CANDIDATES={
 'width':('Source','Wide'), 'opaque':('Wide','Opaque'), 'nodes16':('Opaque','Nodes16'),
 'nodes8':('Opaque','Nodes8'), 'collision':('Opaque','NoCollision'), 'solver':('Opaque','NoSolver'),
 'gpu':('Opaque','GPU'), 'flash':('Source','MuzzleNoCollision'), 'runtime':('legacy_controls','optimized_controls'), 'flight':('actor_pool','data_pool')}

LOCK_VIEW="for w in unreal.EditorLevelLibrary.get_pie_worlds(True)[1:]:\n pc=unreal.GameplayStatics.get_player_controller(w,0)\n cam=next(x for x in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.CameraActor) if x.get_actor_label()=='PerfReview_Camera')\n pc.set_editor_property('bAutoManageActiveCameraTarget',False)\n pc.set_view_target_with_blend(cam,0)"


def launch(case,variant,start_flight_load=True):
    # The editor's existing PIE authorizer depends on completed asset/navigation
    # preparation. Resolve it explicitly before queuing the first startup request.
    prepared=run("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\nn=unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(w,True)\nr=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()\nunreal.MCPythonHelper.submit_result(json.dumps({'success':r.success and n.success,'resource':r.message,'navigation':n.message}))",timeout=180)
    # Controls are chosen before any flight recipe arrives.
    v=0 if variant=='legacy_controls' else 1
    pool=0 if variant in ('legacy_controls','actor_pool') else 1
    result("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"+
        "\n".join(f"unreal.SystemLibrary.execute_console_command(w,{cmd!r})" for cmd in [
          f'gs.Flights.DataPool {pool}',f'gs.Avoidance.QueryOptimizations {v}',f'gs.SceneUI.ProjectionCache {v}',f'gs.Effects.BoundsCull {v}', 'gs.Avoidance.SparseCells 1',
          'gs.Avoidance.VerifyPrefix 0','t.IdleWhenNotForeground 0','Slate.bAllowThrottling 0']))
    st=run(f"unreal.MCPythonHelper.submit_result(json.dumps({{'success':unreal.GuLiComponentSkillQALibrary.start_performance_pie({case in ('runtime','flight')},1280,720)}}))")
    time.sleep(2)
    deadline=time.monotonic()+120
    while time.monotonic()<deadline:
        state=snapshot()
        if len(state['worlds'])==3 and all(w['controllers'] and w['classes'].get('GuLiPerformanceReviewActor',0)>=20 for w in state['worlds']):break
        time.sleep(.5)
    else:raise RuntimeError('PIE clients not ready: '+str(state.get('worlds')))
    setup=run(f'case={case!r}\n'+SETUP)
    deadline=time.monotonic()+120
    wanted=600 if case=='runtime' else 200
    while time.monotonic()<deadline:
        population=run(f"w=unreal.EditorLevelLibrary.get_pie_worlds(True)[0]\nf=json.loads(unreal.GuLiComponentSkillQALibrary.prepare_performance_population(w,{wanted},True))\nunreal.MCPythonHelper.submit_result(json.dumps(f))",timeout=180)
        if population.get('requested')==wanted and population.get('alive')==wanted:
            if population.get('rejected_completed_plans',0):raise RuntimeError('Native move plans rejected: '+str(population))
            if population.get('pending_plans')==0 and population.get('moving',0)>wanted*.90:break
        time.sleep(.5)
    else:raise RuntimeError('Fixed population/motion not ready: '+str(population))
    setup['population']=population
    if case in ('runtime','flight'):
        deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            ready=run("ws=unreal.EditorLevelLibrary.get_pie_worlds(True)\nrows=json.loads(unreal.GuLiComponentSkillQALibrary.wingman_snapshot(ws[0]))\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'ready':any(r['lifecycle']==2 and r['config_usable'] for r in rows)}))")
            if ready['ready']:break
            time.sleep(.3)
        else:raise RuntimeError('Real hangar/Wingman source not ready')
        if start_flight_load:
            deadline=time.monotonic()+90
            while time.monotonic()<deadline:
                available=snapshot()
                if any(sum(c['count'] for c in w.get('instances',[]))>0 for w in available['worlds'][1:]):
                    started=run("w=unreal.EditorLevelLibrary.get_pie_worlds(True)[0]\nf=next(s for s in unreal.ObjectIterator(unreal.GuLiFlightAcceptanceSubsystem) if s.get_outer()==w)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'started':f.start_load(500,45)}))")
                    if started['started']:break
                time.sleep(.5)
            else:raise RuntimeError('No live three-domain load source after setup')
        result("w=unreal.EditorLevelLibrary.get_pie_worlds(True)[0]\nfor a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor):\n p=a.get_component_by_class(unreal.GuLiBuildingProductionComponent)\n if p:p.set_component_tick_enabled(False)")
    else:
        result(f"variant={variant!r}\ncase={case!r}\n"+"for w in unreal.EditorLevelLibrary.get_pie_worlds(True)[1:]:\n for role,y in [('Mining',-20500),('Construction',-18500)]:\n  if case=='flash':continue\n  label='PerfReview_'+role+'_'+variant\n  a=next(x for x in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiPerformanceReviewActor) if x.get_actor_label()==label)\n  a.set_actor_location(unreal.Vector(-16000,y,1500),False,True)\n  a.set_editor_property('EffectCount',16)\n  a.start_comparison()\n if case=='flash':\n  label='PerfReview_Flash_'+variant\n  a=next(x for x in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiPerformanceReviewActor) if x.get_actor_label()==label)\n  a.set_actor_location(unreal.Vector(-14900,-19500,1500),False,True);a.set_editor_property('EffectCount',32);a.start_comparison()\n")
    result(LOCK_VIEW)
    return setup


def summarize_csv(path):
    with path.open(encoding='utf-8-sig') as f:
        rows=list(csv.reader(f))
    header=rows[0];body=rows[1:];r={}
    for name in ['FrameTime','GameThreadTime','GPUTime','GPUFrameTime','PhysicalUsedMB','PhysicalUsed','VirtualUsedMB','VirtualUsed','GPUMemTotalMB','GPUMemUsedMB']:
        if name not in header:continue
        i=header.index(name);vals=[]
        for row in body:
            try:vals.append(float(row[i]))
            except (ValueError,IndexError):pass
        if vals:
            vals.sort();r[name]={'mean':statistics.fmean(vals),'p95':vals[min(len(vals)-1,int(len(vals)*.95))],'n':len(vals)}
    return r


def capture(case,pair,variant):
    output=OUT/'paired'/f'{case}-p{pair}-{variant}'
    if output.exists():output=output.with_name(output.name+'-retry-'+datetime.now().strftime('%H%M%S'))
    output.mkdir(parents=True,exist_ok=False)
    print(json.dumps({'stage':'setup','case':case,'pair':pair,'variant':variant}),flush=True)
    setup=launch(case,variant)
    time.sleep(10)
    # Ship configuration can replace its pawn/camera once during the warmup.
    # Lock again after readiness, and allow the camera manager to publish it.
    result(LOCK_VIEW);time.sleep(.2)
    before=snapshot();counter_before=run(COUNTERS)
    sizes=[c['viewport'] for w in before['worlds'] for c in w['controllers'] if c['local']]
    if sizes!=[[1280,720],[1280,720]]:raise RuntimeError('Actual render viewports differ: '+str(sizes))
    population_before=run("w=unreal.EditorLevelLibrary.get_pie_worlds(True)[0]\nunreal.MCPythonHelper.submit_result(unreal.GuLiComponentSkillQALibrary.performance_population_snapshot(w))")
    views_before=[c for w in before['worlds'] for c in w['controllers'] if c['local']]
    assert all(c['view_target']=='PerfReview_Camera' and not c['auto_manage_camera'] for c in views_before),views_before
    profiles="ws=list(unreal.EditorLevelLibrary.get_pie_worlds(True))\nfor s in unreal.ObjectIterator(unreal.GuLiPerformanceSubsystem):\n if s.get_outer() in ws:s.begin_capture()"
    result(profiles)
    original=before['cvars']['r.GPUCsvStatsEnabled'];csvpath=output/'frames.csv';tracepath=output/'session.utrace'
    cmd=[f'r.GPUCsvStatsEnabled 1',f'Trace.File {tracepath.as_posix()} cpu,gpu,frame,bookmark',f'CsvProfile STARTFILE=../../../{csvpath.relative_to(ROOT).as_posix()}','CsvProfile START','Trace.Bookmark PIEPerfStart']
    begin_before=time.monotonic()
    result("w=unreal.EditorLevelLibrary.get_pie_worlds(True)[1]\n"+"\n".join(f'unreal.SystemLibrary.execute_console_command(w,{x!r})' for x in cmd))
    begin_after=time.monotonic()
    print(json.dumps({'stage':'capture','case':case,'pair':pair,'variant':variant,'seconds':30,'active':[w.get('load',w.get('flight',{})).get('domains',w.get('flight',{}).get('client_flight_data_active')) for w in counter_before['worlds']]}),flush=True)
    log=ROOT/'Saved/Logs/GuLiStrike.log';offset=log.stat().st_size
    time.sleep(30)
    end_before=time.monotonic()
    result("w=unreal.EditorLevelLibrary.get_pie_worlds(True)[1]\n"+"\n".join(f'unreal.SystemLibrary.execute_console_command(w,{x!r})' for x in ['Trace.Bookmark PIEPerfEnd','CsvProfile STOP','Trace.Stop',f'r.GPUCsvStatsEnabled {original:g}']))
    end_after=time.monotonic()
    (output/'clock.json').write_text(json.dumps({'window_monotonic_seconds':[begin_after,end_before],
        'begin_command_bounds':[begin_before,begin_after],'end_command_bounds':[end_before,end_after],
        'clock_basis':'Windows Python time.monotonic / raw QPC seconds',
        'clock_implementation':time.get_clock_info('monotonic').implementation,
        'python_version':sys.version,
        'scope':'Host monotonic 30-second window excludes bridge start/stop command frames.'},indent=2),encoding='utf-8')
    result(profiles.replace('begin_capture','end_capture'))
    counters=run(COUNTERS);after=snapshot()
    population_after=run("w=unreal.EditorLevelLibrary.get_pie_worlds(True)[0]\nunreal.MCPythonHelper.submit_result(unreal.GuLiComponentSkillQALibrary.performance_population_snapshot(w))")
    views_after=[c for w in after['worlds'] for c in w['controllers'] if c['local']]
    fixed_camera_valid=views_before==views_after
    wanted=600 if case=='runtime' else 200
    population_valid=population_before.get('alive')==population_after.get('alive')==wanted
    eligible=fixed_camera_valid and population_valid
    with log.open('rb') as stream:stream.seek(offset);(output/'log-window.txt').write_bytes(stream.read())
    (output/'context-before.json').write_text(json.dumps(before,indent=2),encoding='utf-8')
    (output/'context-after.json').write_text(json.dumps(after,indent=2),encoding='utf-8')
    (output/'counters-before.json').write_text(json.dumps(counter_before,indent=2),encoding='utf-8')
    (output/'counters-after.json').write_text(json.dumps(counters,indent=2),encoding='utf-8')
    deadline=time.monotonic()+10
    while time.monotonic()<deadline:
        if csvpath.is_file() and '[hasheaderrowatend]' in csvpath.read_text(encoding='utf-8',errors='replace')[-65536:].lower():break
        time.sleep(.25)
    stats=summarize_csv(csvpath)
    record={'adoption_eligible':eligible,'fixed_camera_valid':fixed_camera_valid,'population_valid':population_valid,'population_before':population_before,'population_after':population_after,'case':case,'pair':pair,'variant':variant,'setup':setup,'warmup_seconds':10,'capture_seconds':30,'csv':stats,'counters':counters,'actual_views':sizes,
      'scope':'Same process dedicated server plus two clients. Runtime compares specified local rollback controls; it is not a full old-build baseline.'}
    (output/'result.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
    print(json.dumps({'stage':'done','case':case,'pair':pair,'variant':variant,'csv':stats}),flush=True)
    stop();return record


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cases',nargs='+',default=['runtime']);ap.add_argument('--pairs',type=int,default=3);ap.add_argument('--first-pair',type=int,default=1);args=ap.parse_args()
    results=[]
    initial=run("keys=['guli.stronghold.CaptureSeconds','guli.stronghold.TeamUnitCap','t.IdleWhenNotForeground','Slate.bAllowThrottling','gs.Flights.DataPool','gs.Avoidance.QueryOptimizations','gs.SceneUI.ProjectionCache','gs.Effects.BoundsCull','gs.Avoidance.VerifyPrefix','gs.Avoidance.SparseCells']\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'values':{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in keys}}))")['values']
    (OUT/'benchmark-original-cvars.json').write_text(json.dumps(initial,indent=2),encoding='utf-8')
    try:
        for case in args.cases:
            for pair in range(args.first_pair,args.first_pair+args.pairs):
                variants=list(CANDIDATES[case]);
                if pair%2==0:variants.reverse()
                for variant in variants:results.append(capture(case,pair,variant))
    finally:
        (OUT/'paired-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
        try:
            stop()
            result("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"+"\n".join(f'unreal.SystemLibrary.execute_console_command(w,{x!r})' for x in [f'{k} {v:g}' for k,v in initial.items()]))
        except Exception:pass


if __name__=='__main__':main()
