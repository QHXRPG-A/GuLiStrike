"""Run existing authorized cases for the final loaded source build and local rollbacks."""
import json, time
from pathlib import Path
from run_four_stage_review import ROOT, run, result

OUT=ROOT/'outputs/performance/20261009-all-optimizations/final-tests'
OUT.mkdir(exist_ok=True)
filters=['GuLiStrike.Commander.Mass.Navigation.PredictiveAvoidance',
 'GuLiStrike.CombatEffects.FlightBatchBootstrapAndLifecycle','GuLiStrike.CombatEffects.NetworkOrderAndCleanup',
 'GuLiStrike.GroundMech.Fire.PlayerPoolAndWire','GuLiStrike.Ship.WorldHUD',
 'GuLiStrike.Commander.Network.PoseHistoryExpiryAndCleanup','GuLiStrike.CombatEffects.MathAndIdentity',
 'GuLiStrike.Combat.DamageLedger']
deadline=time.monotonic()+120
while time.monotonic()<deadline:
    try:
        original=run("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\nassert not unreal.EditorLevelLibrary.get_pie_worlds(True)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'pool':unreal.SystemLibrary.get_console_variable_int_value('gs.Flights.DataPool')}))",timeout=10)
        if original['pool'] in (0,1):break
    except Exception:pass
    time.sleep(2)
else:raise RuntimeError('Final editor build is not ready')
log=ROOT/'Saved/Logs/GuLiStrike.log'
records=[]
try:
    for name,pool,selection,expected in [
        ('optimized',1,filters,18),('actor_pool_rollback',0,[filters[1]],1)]:
        offset=log.stat().st_size
        result(f"unreal.SystemLibrary.execute_console_command(w,'gs.Flights.DataPool {pool}')\nunreal.SystemLibrary.execute_console_command(w,{'Automation RunTests '+ '+'.join(selection)!r})")
        deadline=time.monotonic()+120
        while time.monotonic()<deadline:
            with log.open('rb') as stream:stream.seek(offset);raw=stream.read().decode('utf-8',errors='replace')
            if 'Automation Test Queue Empty' in raw:break
            time.sleep(.25)
        else:raise RuntimeError('Existing automation queue timeout')
        (OUT/(name+'.log')).write_text(raw,encoding='utf-8')
        lines=[line for line in raw.splitlines() if 'Test Completed.' in line]
        passed=len(lines)==expected and all('Result={成功}' in line or 'Result={Success}' in line for line in lines)
        records.append({'name':name,'expected':expected,'completed':len(lines),'passed':passed,'cases':lines})
        print(json.dumps({'name':name,'expected':expected,'completed':len(lines),'passed':passed}),flush=True)
        if not passed:raise RuntimeError('Existing case failure; inspect '+str(OUT/(name+'.log')))
finally:
    result(f"unreal.SystemLibrary.execute_console_command(w,'gs.Flights.DataPool {original['pool']}')")
    (OUT/'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
