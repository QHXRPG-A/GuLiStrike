"""Capture actual PIE viewport start/peak/stop sequences for the saved VFX candidates.

This is an opt-in review recorder, not an asset renderer or gameplay override.
Production references remain unchanged. Each take uses one effect, a fixed camera,
the same moving end point and the native review actor's 2.4 s / 4 s lifecycle.
"""
from __future__ import annotations
import argparse, json, math, time
from pathlib import Path
from run_four_stage_review import ROOT, OUT, run, result, launch, stop

VARIANTS = ['Source', 'Wide', 'Opaque', 'Nodes16', 'Nodes8', 'NoCollision', 'NoSolver', 'GPU']


def record(role, variant, directory):
    label = 'PerfReview_' + role + '_' + variant
    take = directory / (role + '_' + variant)
    take.mkdir(parents=True, exist_ok=False)
    location = (-16000, -20500, 1500)
    distance = 1500 if role != 'Flash' else 320
    setup = run(f"label={label!r}\nrole={role!r}\n" + """
w=unreal.EditorLevelLibrary.get_pie_worlds(True)[1]
pc=unreal.GameplayStatics.get_player_controller(w,0)
for other in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiPerformanceReviewActor):other.stop_comparison()
a=next(x for x in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiPerformanceReviewActor) if x.get_actor_label()==label)
a.set_actor_location(unreal.Vector(-16000,-20500,1500),False,True)
a.set_editor_property('EffectCount',1)
a.set_editor_property('ActiveSeconds',2.4)
a.set_editor_property('CycleSeconds',4.0)
a.set_editor_property('RelativeBeamEnd',unreal.Vector(1600,0,0))
a.set_editor_property('BaseScale',unreal.Vector(2,2,2) if label.endswith('Impact') else unreal.Vector(1,1,1))
cam=next(x for x in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.CameraActor) if x.get_actor_label()=='PerfReview_Camera')
center=a.get_actor_location()+unreal.Vector(800,0,0) if role!='Flash' else a.get_actor_location()
""" + f"cam.set_actor_location(center+unreal.Vector(0,-{distance},{distance * .3}),False,True)\n" + """
cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(cam.get_actor_location(),center),True)
pc.set_editor_property('bAutoManageActiveCameraTarget',False)
pc.set_view_target_with_blend(cam,0)
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'label':label,'system':str(a.get_editor_property('System')),'scale':str(a.get_editor_property('BaseScale')),'camera':str(cam.get_actor_transform())}))
""")
    time.sleep(.4)
    result('a.start_comparison()')
    wall_start = time.monotonic()
    frames=[]
    # ReadPixels records only pixels actually rendered by this client viewport.
    for index in range(21):
        delay = index * .2 - (time.monotonic() - wall_start)
        if delay > 0:time.sleep(delay)
        elapsed=time.monotonic()-wall_start
        # Identical endpoint motion for every laser; retains start/end structure.
        end = (1600, 160 * math.sin(elapsed * 1.5), 60 * math.sin(elapsed * 2))
        path = take / f'{index:03}.png'
        frame=run(f"a.set_editor_property('RelativeBeamEnd',unreal.Vector{end!r})\n" +
            f"ok=unreal.GuLiComponentSkillQALibrary.capture_performance_viewport(pc,{path.as_posix()!r})\n" +
            "unreal.MCPythonHelper.submit_result(json.dumps({'success':ok,'game_seconds':unreal.GameplayStatics.get_time_seconds(w),'tick_enabled':a.is_actor_tick_enabled()}))")
        frames.append({'file':path.relative_to(directory).as_posix(),'wall_elapsed':elapsed,**frame})
    result('a.stop_comparison()')
    record={'role':role,'variant':variant,'setup':setup,'frames':frames,
            'active_seconds':2.4,'cycle_seconds':4,'capture':'Actual PIE client viewport PNG; no synthesized pixels.'}
    (take/'take.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
    print(json.dumps({'captured':label,'frames':len(frames),'directory':str(take)}),flush=True)
    return record


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--roles',nargs='+',default=['Mining','Construction','Flash']);ap.add_argument('--variants',nargs='+');ap.add_argument('--directory',default='visual-review');args=ap.parse_args()
    output=OUT/args.directory;output.mkdir(parents=True,exist_ok=True)
    takes=[]
    keys=['guli.stronghold.TeamUnitCap','guli.stronghold.CaptureSeconds','gs.Flights.DataPool','gs.Avoidance.QueryOptimizations','gs.SceneUI.ProjectionCache','gs.Effects.BoundsCull','gs.Avoidance.VerifyPrefix','t.IdleWhenNotForeground','Slate.bAllowThrottling']
    original=run(f"unreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'values':{{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in {keys!r}}}}}))")['values']
    try:
        launch('width','Source')
        result("for w0 in unreal.EditorLevelLibrary.get_pie_worlds(True)[1:]:\n for a0 in unreal.GameplayStatics.get_all_actors_of_class(w0,unreal.GuLiPerformanceReviewActor):a0.stop_comparison()")
        for role in args.roles:
            variants=args.variants or (['Source','Muzzle','Impact','MuzzleNoCollision'] if role=='Flash' else VARIANTS)
            for variant in variants:takes.append(record(role,variant,output))
    finally:
        (output/'takes.json').write_text(json.dumps(takes,indent=2),encoding='utf-8')
        stop()
        result("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"+'\n'.join(f'unreal.SystemLibrary.execute_console_command(w,{f"{k} {v:g}"!r})' for k,v in original.items()))


if __name__=='__main__':main()
