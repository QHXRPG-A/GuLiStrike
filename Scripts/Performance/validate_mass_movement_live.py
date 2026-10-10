"""Native stop/order/90-degree/180-degree readback, outside timing windows.

Uses the existing population adapter; this does not publish assets or add RPCs.
"""
from __future__ import annotations
import argparse
import json
import math
import statistics
import sys
import time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Scripts'))
from Performance import run_four_stage_review as review
from Performance import run_muzzle_batch_review as fixture
from Performance import run_snapshot_parallel_review as capture
OUT=ROOT/'Artifacts/MassAvoidance20261011/DirectApply/Behavior'

def snapshot(direction=None):
    command='' if direction is None else f"assert unreal.GuLiComponentSkillQALibrary.order_performance_population(w,{direction}), 'Move order rejected'\n"
    return review.run("w=next(w for w in unreal.EditorLevelLibrary.get_pie_worlds(True) if '/UEDPIE_0_' in w.get_path_name())\n"+command+
        "unreal.MCPythonHelper.submit_result(unreal.GuLiComponentSkillQALibrary.performance_population_snapshot(w))")

def angle_delta(a,b):return (a-b+180)%360-180

def angular_rates(samples):
    rates={}
    for previous,current in zip(samples,samples[1:]):
        dt=current['world_seconds']-previous['world_seconds']
        if dt<=0:continue
        before={r['id']:r for r in previous['positions']}
        for row in current['positions']:
            old=before.get(row['id'])
            if not old or not old['moving'] or not row['moving']:continue
            assert old['entity_identity']==row['entity_identity'], 'Entity identity changed in an active track'
            rate=abs(angle_delta(row['yaw_degrees'],old['yaw_degrees']))/dt
            rates[row['id']]=max(rates.get(row['id'],0),rate)
    active=[v for v in rates.values() if v>5]
    return {'rotating_units':len(active),'median_peak_deg_per_second':statistics.median(active) if active else None,
            'maximum_deg_per_second':max(active) if active else None}

def exercise(scene):
    scale=3
    setup=fixture.launch(scene,2,fixed_viewports=True,install_review_assets=False,extra_previews=False,load_seconds=120)
    time.sleep(3)
    expected=600 if scene=='stress' else 200
    rows=[{'action':'initial','data':snapshot()}]
    snapshot(0);time.sleep(1)
    rows.append({'action':'stop','data':snapshot()})
    snapshot(1);time.sleep(1.5)
    rows.append({'action':'east','data':snapshot()})
    turns=[]
    for direction,label in [(2,'north_90'),(-2,'south_180')]:
        samples=[snapshot(direction)]
        for _ in range(14):
            time.sleep(.12)
            samples.append(snapshot())
        turns.append({'action':label,'samples':samples,'rates':angular_rates(samples)})
    snapshot(0);time.sleep(.5)
    rows.append({'action':'stop_after_reverse','data':snapshot()})
    snapshot(1);time.sleep(1)
    rows.append({'action':'resume','data':snapshot()})
    all_samples=[r['data'] for r in rows]+[s for t in turns for s in t['samples']]
    assert all(s['alive']==expected and s['requested']==expected for s in all_samples)
    assert all(s['rejected_completed_plans']==0 for s in all_samples), 'A completed native move plan was rejected'
    assert all(math.isfinite(value) for s in all_samples for p in s['positions'] for value in p['position']+[p['yaw_degrees']])
    result={'scene':scene,'turn_scale':scale,'setup':setup,'states':rows,'turns':turns,
            'population_and_finite_transforms_pass':True,
            'forced_solves_delta':rows[-1]['data']['forced_avoidance_solves']-rows[0]['data']['forced_avoidance_solves'],
            'scope':'Native order/transform readback outside capture; natural combat/navigation can delay or stop some turns. Rates are observed hull rates, not a frame-time benchmark.'}
    if scale==3:
        path=ROOT/'outputs/performance/20261011-mass-movement-fixed'/f'{scene}-client1.png'
        path.parent.mkdir(parents=True,exist_ok=True)
        result['screenshot']=review.run(f"w=next(w for w in unreal.EditorLevelLibrary.get_pie_worlds(True) if '/UEDPIE_1_' in w.get_path_name())\np=unreal.GameplayStatics.get_player_controller(w,0)\nunreal.MCPythonHelper.submit_result(json.dumps({{'success':unreal.GuLiComponentSkillQALibrary.capture_performance_viewport(p,{path.as_posix()!r}),'path':{path.as_posix()!r}}}))")
    capture.write(OUT/f'{scene}-scale{scale:g}.json',result)
    print(json.dumps({'scene':scene,'scale':scale,'rates':[t['rates'] for t in turns],
                      'stop_moving':rows[1]['data']['moving'],'forced_solves_delta':result['forced_solves_delta']}),flush=True)
    review.stop()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--scenes',nargs='+',choices=['dense200','stress'],default=['dense200','stress'])
    args=p.parse_args()
    keys=capture.KEYS
    original=review.run(f"assert not unreal.EditorLevelLibrary.get_pie_worlds(True)\nunreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'values':{{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in {keys!r}}}}}))")['values']
    capture.write(OUT/'original-controls.json',original)
    try:
        for scene in args.scenes:
            exercise(scene)
    finally:
        review.stop()
        fixture.commands([f'{k} {v:g}' for k,v in original.items()],True)
        restored=review.run(f"unreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'values':{{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in {keys!r}}}}}))")['values']
        capture.write(OUT/'restored-controls.json',restored)
        assert original==restored

if __name__=='__main__':main()
