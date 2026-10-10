"""Dispatch only the approved existing Mass/aim automation entries, before PIE."""
import json
from pathlib import Path
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Scripts'))
from Performance import run_four_stage_review as review
from Performance import run_muzzle_batch_review as fixture

OUT=ROOT/'Artifacts/MassAvoidance20261011/QA'
OUT.mkdir(parents=True,exist_ok=True)
keys=['t.IdleWhenNotForeground','Slate.bAllowThrottling']
original=review.run(f"""
assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
unreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'values':{{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in {keys!r}}}}}))
""")['values']
(OUT/'original-controls.json').write_text(json.dumps(original,indent=2))
try:
    fixture.commands(['t.IdleWhenNotForeground 0','Slate.bAllowThrottling 0','Trace.Stop','CsvProfile STOP'],True)
    cases=[('predictive','GuLiStrike.Commander.Mass.Navigation.PredictiveAvoidance'),
           ('soft','GuLiStrike.Commander.Mass.Navigation.ManualAvoidanceGrid'),
           ('aim','GuLiStrike.Presentation.MechanicalAnimation')]
    receipts=[]
    log=ROOT/'Saved/Logs/GuLiStrike.log'
    for name,scope in cases:
        offset=log.stat().st_size
        fixture.commands(['Automation RunTests '+scope],True)
        deadline=time.monotonic()+150
        while time.monotonic()<deadline:
            with log.open('rb') as f:f.seek(offset);text=f.read().decode('utf-8',errors='replace')
            if 'Automation Test Queue Empty' in text:break
            time.sleep(1)
        else:raise RuntimeError('Automation did not finish: '+scope)
        (OUT/(name+'.log')).write_text(text,encoding='utf-8')
        completed=[s for s in text.splitlines() if 'Test Completed.' in s]
        failures=[s for s in text.splitlines() if 'LogAutomationController: Error:' in s or 'LogAutomationTest: Error:' in s]
        result={'scope':scope,'completed':completed,'failures':failures,'passed':bool(completed) and not failures}
        receipts.append(result)
        (OUT/'results.json').write_text(json.dumps(receipts,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps({'scope':scope,'tests':len(completed),'passed':result['passed']}),flush=True)
        assert result['passed'],failures
finally:
    fixture.commands([f'{k} {v:g}' for k,v in original.items()],True)
    restored=review.run(f"unreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'values':{{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in {keys!r}}}}}))")['values']
    (OUT/'restored-controls.json').write_text(json.dumps(restored,indent=2))
    assert original==restored
