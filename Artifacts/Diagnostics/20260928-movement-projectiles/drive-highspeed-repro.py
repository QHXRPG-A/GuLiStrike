"""Bounded real input and sampling for the reported high-speed movement/shot issue."""
import json
import math
import time
import traceback
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'Artifacts/Diagnostics/20260928-movement-projectiles/highspeed-movement-runtime.json'
fixture=json.loads((ROOT/'Artifacts/Diagnostics/20260928-movement-projectiles/highspeed-fixture.json').read_text())
ids={s['id'] for s in fixture['units']}
seed=fixture['units'][0]
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc=unreal.GameplayStatics.get_player_controller(w,0)
authority=next(o for o in unreal.ObjectIterator(unreal.GuLiBattleAuthoritySubsystem) if o.get_outer()==w)
nav=next(o for o in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.RecastNavMesh) if 'CommanderSoldier' in o.get_name())
target=unreal.NavigationSystemV1.project_point_to_navigation(w,unreal.Vector(-9000,14500,0),nav,None,unreal.Vector(600,600,5000))
assert target
start=time.monotonic()
state={'phase':'select','since':start,'world':w.get_path_name(),'seed':seed,'unit_ids':sorted(ids),
       'target':[target.x,target.y,target.z],'rows':[],'actions':[]}
handle=None
last_sample=-1.0
trace_started=False


def command(cmd):
    unreal.SystemLibrary.execute_console_command(w,cmd,pc)
    state['actions'].append({'time':unreal.GameplayStatics.get_time_seconds(w),'command':cmd})


def step(phase):
    state['phase']=phase;state['since']=time.monotonic()


def finish(error=None):
    global handle
    if handle is not None:unreal.unregister_slate_post_tick_callback(handle);handle=None
    if trace_started:command('gs.Commander.PredictionTrace.Stop')
    state['error']=error;state['duration']=time.monotonic()-start
    OUT.write_text(json.dumps(state,indent=2),encoding='utf-8')


def tick(dt):
    global last_sample,trace_started
    try:
        if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()!=w:
            finish('PIE world changed');return
        now=time.monotonic(); elapsed=now-state['since']
        if now-start>25:finish('Timed out in '+state['phase']);return
        if now-last_sample>=.1:
            last_sample=now
            data=json.loads(authority.get_move_response_diagnostics())
            state['rows'].append({'time':unreal.GameplayStatics.get_time_seconds(w),'sim_tick':data['sim_tick'],
                'units':[{k:s[k] for k in ['id','position','velocity','alive','command','order','nav_state','no_progress']} for s in data['soldiers'] if s['id'] in ids]})
        phase=state['phase']
        if phase=='select' and elapsed>.8:
            snap=ROOT/'outputs/movement-projectiles-highspeed-selected.json'
            command('gs.Commander.QA.InputSnapshot '+snap.as_posix())
            selected=json.loads(snap.read_text())['selected_ids'];state['selected_ids']=selected
            if seed['id'] not in selected:raise RuntimeError('Real click did not select the seed')
            command(f'gs.Commander.PredictionTrace.Start baseline {seed["id"]} 18');trace_started=True
            command(f'gs.Commander.QA.MouseWorld right {target.x} {target.y} {target.z}')
            step('moving')
        elif phase=='moving' and elapsed>1:
            cap=unreal.SystemLibrary.get_console_variable_int_value('guli.stronghold.TeamUnitCap')
            try:
                command('guli.stronghold.TeamUnitCap 10000')
                for i in range(5):
                    command(f'gs.GM.Skill.Spawn Blue 2 {-6500+i*650} 15500 0')
            finally:
                command('guli.stronghold.TeamUnitCap '+str(cap))
            state['unit_cap_restored']=unreal.SystemLibrary.get_console_variable_int_value('guli.stronghold.TeamUnitCap')
            path=ROOT/'Artifacts/Diagnostics/20260928-movement-projectiles/capture-render-confirmation.py'
            exec(compile(path.read_text(encoding='utf-8'),str(path),'exec'),{'__name__':'highspeed_visible_render_probe'})
            step('combat')
        elif phase=='combat' and elapsed>4:
            command(f'gs.Commander.QA.MouseWorld right {seed["x"]} {seed["y"]} {seed["z"]}')
            step('return')
        elif phase=='return' and elapsed>4:
            command(f'gs.Commander.QA.MouseWorld right {target.x} {target.y} {target.z}')
            step('finish')
        elif phase=='finish' and elapsed>6:
            for sid in state['selected_ids']:command(f'gs.GM.Commander.Nav.Soldier {sid}')
            finish()
    except Exception:finish(traceback.format_exc())


unreal.GameplayStatics.get_player_pawn(w,0).jump_to_world_location(unreal.Vector(-13000,15100,-800))
command(f'gs.Commander.QA.MouseSoldier click {seed["id"]}')
handle=unreal.register_slate_post_tick_callback(tick)
print(json.dumps({'started':True,'seed':seed['id'],'target':state['target'],'output':str(OUT)}))
