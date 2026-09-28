"""Apply five real movement cards and prepare a transient, scoped PIE reproduction."""
import json
import time
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT/'Artifacts/Diagnostics/20260928-movement-projectiles/highspeed-setup.json'
QA = unreal.GuLiRogueCardQALibrary
state = {'phase':'ready','cards':0,'events':[], 'start':time.monotonic(), 'since':time.monotonic()}
handle = None


def step(phase):
    state['phase'] = phase
    state['since'] = time.monotonic()


def finish(error=None):
    global handle
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        handle = None
    state['error'] = error
    state['duration'] = time.monotonic()-state['start']
    OUT.write_text(json.dumps(state,indent=2),encoding='utf-8')


def tick(dt):
    try:
        if time.monotonic()-state['start'] > 100:
            finish('Timed out in '+state['phase'])
            return
        w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not w:return
        pc=unreal.GameplayStatics.get_player_controller(w,0)
        if not pc:return
        snap=json.loads(QA.snapshot(pc))
        phase=state['phase'];elapsed=time.monotonic()-state['since']
        if phase=='ready':
            if not snap.get('ready'):return
            state['world']=w.get_path_name()
            QA.action(pc,'F4Down');step('release')
        elif phase=='release' and elapsed>.15:
            QA.action(pc,'F4Up');step('choose')
        elif phase=='choose' and snap.get('phase')==1:
            QA.action(pc,'Hit',2);step('flip')
        elif phase=='flip' and snap.get('phase')==3:
            QA.action(pc,'Hit',2);step('close')
        elif phase=='close' and not snap['open']:
            state['cards']+=1
            state['events'].append({'card':state['cards'],'world_time':unreal.GameplayStatics.get_time_seconds(w)})
            step('settle')
        elif phase=='settle' and elapsed>1.3:
            if state['cards']<5:
                QA.action(pc,'Open');step('choose')
                return
            game_state=unreal.GameplayStatics.get_game_state(w)
            state['published_base_speed']=game_state.get_effective_soldier_move_speed_cm_per_second()
            state['published_multipliers']=[x.export_text() for x in game_state.get_editor_property('UnitMovementMultipliers')]
            a=next(o for o in unreal.ObjectIterator(unreal.GuLiBattleAuthoritySubsystem) if o.get_outer()==w)
            before={s['id'] for s in json.loads(a.get_move_response_diagnostics())['soldiers']}
            for row in range(2):
                for col in range(5):
                    unreal.SystemLibrary.execute_console_command(w,f'gs.GM.Skill.Spawn Red 2 {-17500+col*650} {14000+row*700} -1400',pc)
            after=json.loads(a.get_move_response_diagnostics())['soldiers']
            state['units']=[s for s in after if s['id'] not in before]
            if not state['units']:raise RuntimeError('No reproduction units were created')
            pawn=unreal.GameplayStatics.get_player_pawn(w,0)
            pawn.jump_to_world_location(unreal.Vector(-15500,15500,-1200))
            step('focus')
        elif phase=='focus' and elapsed>.8:
            seed=state['units'][0]['id']
            unreal.SystemLibrary.execute_console_command(w,f'gs.Commander.QA.MouseSoldier click {seed}',pc)
            state['seed']=seed
            step('selected')
        elif phase=='selected' and elapsed>.8:
            snapshot=ROOT/'outputs/movement-projectiles-highspeed-selected.json'
            unreal.SystemLibrary.execute_console_command(w,'gs.Commander.QA.InputSnapshot '+snapshot.as_posix(),pc)
            picked=json.loads(snapshot.read_text())
            state['selected_ids']=picked['selected_ids']
            if state['seed'] not in state['selected_ids']:raise RuntimeError('Real viewport selection did not select the seed')
            step('complete');finish()
    except Exception:
        finish(traceback.format_exc())


handle=unreal.register_slate_post_tick_callback(tick)
print(json.dumps({'started':True,'output':str(OUT)}))
