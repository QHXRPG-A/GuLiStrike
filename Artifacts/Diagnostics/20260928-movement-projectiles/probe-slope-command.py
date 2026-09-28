"""Scoped command-channel reproduction of the formerly rejected high-speed slope."""
import json
import time
import traceback
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'Artifacts/Diagnostics/20260928-movement-projectiles/highspeed-slope-command.json'
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc=unreal.GameplayStatics.get_player_controller(w,0)
channel=pc.get_component_by_class(unreal.GuLiCommanderNetSyncComponent)
authority=next(o for o in unreal.ObjectIterator(unreal.GuLiBattleAuthoritySubsystem) if o.get_outer()==w)
previous=unreal.SystemLibrary.get_console_variable_int_value('guli.Commander.MoveLatencyDiagnostics')
unreal.SystemLibrary.execute_console_command(w,'guli.Commander.MoveLatencyDiagnostics 1',pc)
cap=unreal.SystemLibrary.get_console_variable_int_value('guli.stronghold.TeamUnitCap')
try:
    unreal.SystemLibrary.execute_console_command(w,'guli.stronghold.TeamUnitCap 10000',pc)
    fixture=json.loads(unreal.GuLiRogueCardQALibrary.build_fixture(pc,1,unreal.Vector(-15740,16340,-1550),400))
finally:
    unreal.SystemLibrary.execute_console_command(w,'guli.stronghold.TeamUnitCap '+str(cap),pc)
if not fixture.get('units'):
    unreal.SystemLibrary.execute_console_command(w,'guli.Commander.MoveLatencyDiagnostics '+str(previous),pc)
    raise RuntimeError('No navigable slope fixture could be created')
seed=fixture['units'][0]
origin=unreal.Vector(seed['x'],seed['y'],seed['z'])
target=unreal.Vector(-9000,14500,-428.731312)
assert channel.submit_move_response_diagnostic('radius',11001,seed['id'],origin)
unreal.GameplayStatics.get_player_pawn(w,0).jump_to_world_location(unreal.Vector(-13200,14400,-800))
start=time.monotonic(); last=-1.; phase='select'; handle=None
data={'world':w.get_path_name(),'fixture':fixture,'rows':[],'input_path':'Existing development adapter -> normal ordered selection/task RPC channel'}


def finish(error=None):
    global handle
    if handle is not None:unreal.unregister_slate_post_tick_callback(handle);handle=None
    unreal.SystemLibrary.execute_console_command(w,'guli.Commander.MoveLatencyDiagnostics '+str(previous),pc)
    data['diagnostic_cvar_restored']=unreal.SystemLibrary.get_console_variable_int_value('guli.Commander.MoveLatencyDiagnostics')
    data['error']=error;data['duration']=time.monotonic()-start
    OUT.write_text(json.dumps(data,indent=2),encoding='utf-8')


def tick(dt):
    global last,phase
    try:
        elapsed=time.monotonic()-start
        if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()!=w:
            finish('PIE world changed');return
        if elapsed-last>=.09:
            last=elapsed;d=json.loads(authority.get_move_response_diagnostics())
            s=next((s for s in d['soldiers'] if s['id']==seed['id']),None)
            if s:data['rows'].append({'time':unreal.GameplayStatics.get_time_seconds(w),'sim_tick':d['sim_tick'],**s})
        if phase=='select' and elapsed>.6:
            snap=ROOT/'outputs/movement-projectiles-slope-selected.json'
            unreal.SystemLibrary.execute_console_command(w,'gs.Commander.QA.InputSnapshot '+snap.as_posix(),pc)
            data['selected_ids']=json.loads(snap.read_text())['selected_ids']
            if seed['id'] not in data['selected_ids']:raise RuntimeError('Authority selection did not contain slope probe')
            assert channel.submit_move_response_diagnostic('move',11002,seed['id'],target)
            phase='move'
        elif phase=='move' and elapsed>5:
            unreal.SystemLibrary.execute_console_command(w,f'gs.GM.Commander.Nav.Soldier {seed["id"]}',pc)
            finish()
    except Exception:finish(traceback.format_exc())


handle=unreal.register_slate_post_tick_callback(tick)
print(json.dumps({'started':True,'seed':seed,'output':str(OUT)}))
