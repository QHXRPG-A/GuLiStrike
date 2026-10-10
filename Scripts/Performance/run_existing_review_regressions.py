"""Run the project's existing relevant cases on the loaded source editor build."""
from __future__ import annotations
import json, re, time
from run_four_stage_review import ROOT, OUT, run, result

filters=['GuLiStrike.Commander.Mass.Navigation.PredictiveAvoidance',
 'GuLiStrike.CombatEffects.FlightBatchBootstrapAndLifecycle',
 'GuLiStrike.CombatEffects.NetworkOrderAndCleanup',
 'GuLiStrike.GroundMech.Fire.PlayerPoolAndWire',
 'GuLiStrike.Ship.WorldHUD',
 'GuLiStrike.Commander.Network.PoseHistoryExpiryAndCleanup']
log=ROOT/'Saved/Logs/GuLiStrike.log'
state=run("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\nassert not unreal.EditorLevelLibrary.get_pie_worlds(True)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'pool':unreal.SystemLibrary.get_console_variable_float_value('gs.Flights.DataPool')}))")
records=[]
try:
    for mode,selection in [(1,filters),(0,[filters[1]])]:
        offset=log.stat().st_size
        result(f"unreal.SystemLibrary.execute_console_command(w,'gs.Flights.DataPool {mode}')\nunreal.SystemLibrary.execute_console_command(w,{'Automation RunTests '+ '+'.join(selection)!r})")
        deadline=time.monotonic()+120
        while time.monotonic()<deadline:
            with log.open('rb') as stream:stream.seek(offset);raw=stream.read().decode('utf-8',errors='replace')
            if 'Automation Test Queue Empty' in raw:break
            time.sleep(.25)
        else:raise RuntimeError('Existing automation queue timeout')
        (OUT/f'existing-regressions-final-pool{mode}.log').write_text(raw,encoding='utf-8')
        lines=[s for s in raw.splitlines() if 'Test Completed.' in s]
        expected=14 if mode else 1
        record={'data_pool':mode,'expected':expected,'completed':lines,'passed':len(lines)==expected and all('Result={成功}' in s or 'Result={Success}' in s for s in lines)}
        records.append(record);print(json.dumps(record,ensure_ascii=False),flush=True)
        if not record['passed']:raise RuntimeError('Existing regression failure: '+str(record))
finally:
    result(f"unreal.SystemLibrary.execute_console_command(w,'gs.Flights.DataPool {state['pool']:g}')")
    (OUT/'existing-regressions-final.json').write_text(json.dumps(records,indent=2,ensure_ascii=False),encoding='utf-8')
