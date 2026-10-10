"""Historical Mass A/B review; runtime switches were removed on 2026-10-11.

Every window rebuilds PIE, warms 10 seconds, captures 30 seconds, and retains
production 250000 B/s connections. Retained for interpreting the old captures;
main refuses execution because the old baseline can no longer be selected.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts'))
from Performance import run_snapshot_parallel_review as capture
from Performance import run_muzzle_batch_review as fixture
from Performance import run_four_stage_review as review

OUT = ROOT / 'Artifacts/MassAvoidance20261011'
BASE = {'gs.Avoidance.LookaheadSeconds':2.5, 'gs.Avoidance.DistanceFirst':0,
        'gs.Avoidance.MaxCandidates':6, 'gs.Avoidance.SharedIndex':0,
        'gs.Avoidance.ReuseZero':0, 'gs.Avoidance.CacheMaxSeconds':.3,
        'gs.Units.MassTurnScale':1}
AVOID = dict(BASE, **{'gs.Avoidance.LookaheadSeconds':.5, 'gs.Avoidance.DistanceFirst':1,
                     'gs.Avoidance.SharedIndex':1, 'gs.Avoidance.ReuseZero':1})
TURN = dict(BASE, **{'gs.Units.MassTurnScale':3})
FINAL = dict(AVOID, **{'gs.Units.MassTurnScale':3})
VARIANTS = {'baseline':BASE, 'avoidance':AVOID, 'turn':TURN, 'combined':FINAL,
            'no-cache':dict(FINAL, **{'gs.Avoidance.ReuseZero':0}),
            'no-shared':dict(FINAL, **{'gs.Avoidance.SharedIndex':0})}

def write(path, value):
    capture.write(path, value)

def run_window(scene, candidate, number, variant, phase, seconds):
    return capture.capture(scene,candidate,number,variant,seconds=seconds,phase=phase,
                           controls=VARIANTS[variant],verify=False)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--phase',choices=['smoke','aa','avoidance','turn','combined','all','diagnostic'],default='smoke')
    p.add_argument('--scenes',nargs='+',choices=['dense200','stress'],default=['dense200','stress'])
    p.add_argument('--rounds',type=int,default=3)
    p.add_argument('--seconds',type=int,default=30)
    p.add_argument('--variant',choices=list(VARIANTS),default='combined')
    args=p.parse_args()
    raise SystemExit('Historical A/B controls were removed at the user request. Use run_snapshot_parallel_review.py --phase inspect (or --phase baseline) for the fixed optimized implementation; retained 40-window evidence belongs to the previous build.')
    capture.OUT=OUT
    keys=list(dict.fromkeys(capture.KEYS+list(BASE)))
    session=OUT/'Sessions'/(datetime.now().strftime('%Y%m%d-%H%M%S-')+args.phase)
    original=review.run(f"""
assert not unreal.EditorLevelLibrary.get_pie_worlds(True), 'An unrelated PIE session is active'
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert 'LVL_CommanderMassPrototype' in w.get_path_name()
unreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'values':{{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in {keys!r}}}}}))
""")['values']
    write(session/'original-controls.json',original)
    configs={k:hashlib.sha256((ROOT/'Config'/k).read_bytes()).hexdigest() for k in ['DefaultEngine.ini','DefaultGame.ini']}
    write(session/'config-hashes-before.json',configs)
    try:
        prepared=review.run("""
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
n=unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(w,True)
r=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
unreal.MCPythonHelper.submit_result(json.dumps({'success':r.success and n.success,'navigation':n.message,'resource':r.message}))
""",timeout=180)
        write(session/'preparation.json',prepared)
        phases=['aa','avoidance','turn','combined'] if args.phase=='all' else [args.phase]
        for phase in phases:
            for scene in args.scenes:
                if phase in ['smoke','diagnostic']:
                    run_window(scene,args.variant,1,args.variant,phase,5 if phase=='smoke' else args.seconds)
                elif phase=='aa':
                    for n in [1,2]:run_window(scene,'same-version',n,'baseline',phase,args.seconds)
                else:
                    for n in range(1,args.rounds+1):
                        order=['baseline',phase] if n%2 else [phase,'baseline']
                        for variant in order:run_window(scene,phase,n,variant,phase,args.seconds)
    finally:
        # Also close profilers if setup/transport failed before capture's guard.
        fixture.commands(['Trace.Stop','CsvProfile STOP'],True)
        review.stop()
        fixture.commands([f'{k} {v:g}' for k,v in original.items()],True)
        restored=review.run(f"unreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'values':{{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in {keys!r}}}}}))")['values']
        write(session/'restored-controls.json',restored)
        after={k:hashlib.sha256((ROOT/'Config'/k).read_bytes()).hexdigest() for k in configs}
        write(session/'config-hashes-after.json',after)
        assert configs==after,'Saved production configs changed'
        assert original==restored,'Temporary controls not restored exactly'

if __name__=='__main__':main()
