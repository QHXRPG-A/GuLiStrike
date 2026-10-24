"""Opt-in local ore projection cost capture; not a Hybrid extraction acceptance.

Uses the production 600-unit local PIE room and native timers. No bridge calls,
screenshots or serialization inside each timing window. Resync restores the
unchanged committed ledger outside every window. Quality CVars stay unchanged.
"""
from __future__ import annotations
import argparse
import json
import math
import statistics
import time
from pathlib import Path
from capture_live_pie import editor, commands, status, stop_owned_pie, PERF

WORLD = "w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
RESOURCE = "s=next(s for s in unreal.ObjectIterator(unreal.GuLiResourceWorldSubsystem) if s.get_outer()==w)\n"


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def context():
    return editor(WORLD + RESOURCE + """
pc=unreal.GameplayStatics.get_player_controller(w,0)
cam=pc.get_view_target()
pose=cam.get_actor_location();rotation=cam.get_actor_rotation()
diag=s.get_ore_obstacle_diagnostics()
r={'ore':s.get_ore_obstacle_diagnostics(),'fatal':s.has_fatal_initialization_error(),
   'error':s.get_initialization_error(),'ready':s.is_hybrid_query_scene_ready(),
   'nav_busy':not all(x in diag.split() for x in ['nav_remaining=0','nav_running=0','nav_dirty=0']),
   'world':w.get_path_name(),'viewport':list(pc.get_viewport_size()),
   'camera':[pose.x,pose.y,pose.z,rotation.pitch,rotation.yaw,rotation.roll],'view_target':cam.get_name(),
   'cvars':{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in
   ['t.MaxFPS','r.VSync','r.ScreenPercentage','sg.ViewDistanceQuality','sg.ShadowQuality',
    'sg.GlobalIlluminationQuality','sg.ReflectionQuality','sg.PostProcessQuality',
    'sg.TextureQuality','sg.EffectsQuality','sg.FoliageQuality','sg.ShadingQuality']}}
""")


def distribution(values):
    if not values:
        return None
    ordered = sorted(values)
    q = lambda p: ordered[min(len(ordered)-1, math.ceil(len(ordered)*p)-1)]
    mean = statistics.mean(ordered)
    return dict(n=len(values), mean_ms=mean, median_ms=q(.5), p95_ms=q(.95),
                p99_ms=q(.99), max_ms=ordered[-1], fps=1000/mean if mean else 0,
                over_16_67_pct=100*sum(x>1000/60 for x in ordered)/len(ordered),
                over_33_33_pct=100*sum(x>1000/30 for x in ordered)/len(ordered))


def summarize(profile, count, diagnostic_trace=False):
    c = profile['counters']
    rows = lambda key: c.get(key, {}).get('timed_frames', [])
    trigger = rows('Ore.ProbeRequestedNodes')
    assert len(trigger) == 1 and trigger[0]['value'] == count, 'Probe did not execute exactly once'
    assert c['Ore.ProbeAppliedNodes']['total'] == count
    frames = rows('Capture.FrameIntervalMs')
    freq = profile['qpc_frequency']
    start = trigger[0]['qpc']
    # BeginFrame's interval describes the PREVIOUS frame, while batch metrics
    # describe the current frame. Align batch frame N with interval frame N+1.
    intervals = {r['frame']-1: r['value'] for r in frames}
    batches = rows('Ore.BatchMainThreadMs')
    batch_nodes = {r['frame']:r['value'] for r in rows('Ore.BatchNodes')}
    assert sum(batch_nodes.values()) >= count
    assert len({r['value'] for r in rows('Ore.WorldGeneration')})==1, 'Directory resync contaminated capture'
    assert not batch_nodes or max(batch_nodes.values()) <= 64
    assert profile['capture_valid'] and not profile['capturing']
    assert profile['trace_connected']==diagnostic_trace
    def window(lo, hi):
        return distribution([r['value'] for r in frames if lo <= (r['qpc']-start)/freq < hi])
    nav = rows('Ore.NavRemainingTasks')
    nav_active = [r for r in nav if r['value'] > 0 and r['qpc'] >= start]
    foreground = c.get('Capture.ViewportForegroundFrames', {}).get('total', 0)
    background = c.get('Capture.ViewportBackgroundFrames', {}).get('total', 0)
    result = dict(count=count, capture_valid=profile['capture_valid'],
        diagnostic_trace=diagnostic_trace,performance_eligible=not diagnostic_trace,
        total_removed=sum(batch_nodes.values()),background_removed=sum(batch_nodes.values())-count,
        foreground_frames=foreground, background_frames=background,
        editor_states={k:v['total'] for k,v in c.items() if k.startswith('Capture.EditorRoot')},
        pre=window(-3,-.1), event_2s=window(0,2), post=window(3,9),
        whole=distribution([r['value'] for r in frames[2:-2]]),
        batch_ms=distribution([r['value'] for r in batches]),
        batches=[dict(nodes=batch_nodes[r['frame']],gt_ms=r['value'],
            frame_ms=intervals.get(r['frame']),after_trigger_ms=(r['qpc']-start)*1000/freq)
            for r in batches],
        dispatch_ms=c['Ore.ProbeDispatchMs']['total'],
        batch_wait_ms=[r['value'] for r in rows('Ore.OldestBatchWaitMs')],
        nav_active_frames=len(nav_active), nav_peak=max((r['value'] for r in nav), default=0),
        nav_last_busy_ms=(nav_active[-1]['qpc']-start)*1000/freq if nav_active else None,
        nav_frame_ms=distribution([intervals[r['frame']] for r in nav_active if r['frame'] in intervals]),
        resource_tick_ms=distribution([r['value'] for r in rows('Ore.ResourceTickMs')]))
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--counts',default='0,1,64,234')
    parser.add_argument('--rounds',type=int,default=3)
    parser.add_argument('--seconds',type=float,default=15)
    parser.add_argument('--existing-pie',action='store_true')
    parser.add_argument('--stop-miners',action='store_true',help='Issue persistent normal Stop orders to the 16 locally simulated miners before timing; owned fresh sessions only')
    parser.add_argument('--trace',action='store_true',help='CPU/GPU trace diagnosis; excluded from final performance comparisons')
    args=parser.parse_args()
    assert args.seconds>=14
    assert not (args.stop_miners and args.existing_pie), 'Keep explicit Stop preparation inside an owned temporary session'
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    initial=status();save(out/'initial-session.json',initial)
    assert args.existing_pie or not initial.get('active'), 'Do not replace an existing session'
    settings=editor("s=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrikeEditor.GuLiLocalPIESettings'))\n"
        "r={'role':s.get_editor_property('MainRole').name,'idle':unreal.SystemLibrary.get_console_variable_float_value('t.IdleWhenNotForeground'),'throttle':unreal.SystemLibrary.get_console_variable_float_value('Slate.bAllowThrottling')}")
    save(out/'original-settings.json',settings)
    reports=[]
    try:
        editor("s=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrikeEditor.GuLiLocalPIESettings'))\ns.set_editor_property('MainRole',unreal.GuLiCommanderRole.COMMANDER)\n"
            "w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
            "unreal.SystemLibrary.execute_console_command(w,'t.IdleWhenNotForeground 0')\n"
            "unreal.SystemLibrary.execute_console_command(w,'Slate.bAllowThrottling 0')\nr={'success':True}")
        if not args.existing_pie:
            editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
                "unreal.SystemLibrary.execute_console_command(w,'gs.Commander.QA.StartPIE standalone 1920 1077')\nr={'success':True}")
        deadline=time.monotonic()+120
        while time.monotonic()<deadline:
            s=status()
            if s.get('error') or s.get('network_error'):raise RuntimeError(s)
            if s.get('world_ready') and s.get('units')==600 and s.get('can_issue_orders'):break
            time.sleep(1)
        else:raise RuntimeError('PIE readiness timeout')
        save(out/'session.json',s)
        if args.stop_miners:
            preparation=editor(WORLD+"""
miners=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiMiningVehiclePawn)
r={'stops':[{'actor':m.get_name(),'accepted':m.issue_player_command_by_value(1000000+i,unreal.GuLiMiningOrderType.CANCEL,m.get_actor_location(),0,1,m.get_team())} for i,m in enumerate(miners)]}
""")
            save(out/'mining-preparation.json',preparation)
            assert len(preparation['stops'])==16 and all(r['accepted'] for r in preparation['stops'])
        commands(['gs.Resources.OreReview.Focus batch','gs.Commander.QA.FocusPIE','Trace.Stop','Trace.Disable'])
        print(json.dumps({'phase':'warmup','seconds':15}),flush=True)
        time.sleep(15)
        counts=[int(x) for x in args.counts.split(',')]
        for round_index in range(args.rounds):
            order=counts if round_index%2==0 else list(reversed(counts))
            for count in order:
                directory=out/f'r{round_index+1}-n{count}';directory.mkdir()
                commands(['gs.Resources.OreReview.Resync','gs.Commander.QA.FocusPIE'])
                # Initial scene reconstruction is deliberately outside the sample.
                time.sleep(2)
                deadline=time.monotonic()+60
                while time.monotonic()<deadline:
                    before=context()
                    if before['fatal']:raise RuntimeError(before)
                    if before['ready'] and not before['nav_busy']:break
                    time.sleep(1)
                else:raise RuntimeError('Navigation warmup timeout')
                time.sleep(5)
                before=context();save(directory/'before.json',before)
                if args.trace:
                    commands([f'Trace.File {(directory/"session.utrace").as_posix()} cpu,gpu,frame,bookmark'])
                commands(['gs.Commander.QA.FocusPIE'])
                time.sleep(.5)
                print(json.dumps({'phase':'native-capture','round':round_index+1,'nodes':count,'seconds':args.seconds}),flush=True)
                started=editor(WORLD+f'p={PERF}\n'+
                    f"ok=p.begin_timed_capture({args.seconds})\nassert ok\n"
                    f"unreal.SystemLibrary.execute_console_command(w,'gs.Resources.OreReview.PerfProbe {count} 4')\nr={{'started':ok}}")
                assert started['started']
                time.sleep(args.seconds+1)
                profile=editor(WORLD+f'p={PERF}\nr=json.loads(p.get_capture_json())')
                if args.trace:commands(['Trace.Stop','Trace.Disable'])
                save(directory/'profile.json',profile)
                after=context();save(directory/'after.json',after)
                result=summarize(profile,count,args.trace)
                if args.stop_miners:
                    assert result['background_removed']==0, 'Background mining was not stopped'
                    assert all(s['task_activity']['unit/3']['stopped']==16 for s in profile['network_samples'])
                result.update(round=round_index+1,directory=str(directory))
                result['same_view']=before['camera']==after['camera'] and before['viewport']==after['viewport'] and before['cvars']==after['cvars']
                result['scene_ok']=not after['fatal'] and 'instance_map=valid' in after['ore'] and 'pending=0' in after['ore']
                save(directory/'summary.json',result);reports.append(result)
                save(out/'summary.json',reports)
                print(json.dumps(result),flush=True)
                assert result['same_view'] and result['scene_ok']
    finally:
        try:
            if status().get('active'):
                commands(['Trace.Stop','Trace.Disable','gs.Resources.OreReview.Resync','gs.Resources.OreReview.Focus player'])
                time.sleep(1)
            if not args.existing_pie:save(out/'shutdown.json',stop_owned_pie())
        finally:
            editor("s=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrikeEditor.GuLiLocalPIESettings'))\n"
                f"s.set_editor_property('MainRole',unreal.GuLiCommanderRole.{settings['role']})\n"
                "w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
                f"unreal.SystemLibrary.execute_console_command(w,'t.IdleWhenNotForeground {settings['idle']}')\n"
                f"unreal.SystemLibrary.execute_console_command(w,'Slate.bAllowThrottling {settings['throttle']}')\nr={{'success':True}}")


if __name__=='__main__':
    main()
