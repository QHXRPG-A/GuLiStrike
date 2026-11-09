"""Authorized PIE check: real card clicks, semantic firing, structured observations.
Run after a fresh ready Ground session with unrelated automatic combat isolated.
No screenshots or synthetic damage/visual receipts.
"""
import json
import time
import traceback
from pathlib import Path
import unreal

OUT=Path(globals().get('GULI_PROBE_OUT',Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))/'Artifacts/GroundRevision_20261103'))
WORLD=unreal.EditorLevelLibrary.get_game_world()
PC=unreal.GameplayStatics.get_player_controller(WORLD,0)
PAWN=PC.get_controlled_pawn()
assert PAWN and unreal.SystemLibrary.get_console_variable_int_value('gs.Review.GroundIsolateCombat')==1
CENTER=unreal.Vector(-13950,51150,1075)
DIRECTOR=unreal.load_class(None,'/Game/GuLiStrike/CardSystem/RevealDemo/Blueprints/BP_CardRevealDirector.BP_CardRevealDirector_C')
REPORT={'success':False,'cards':[],'samples':[],'particles':[],'errors':[],'visual_test':False}
STATE={'phase':'open_burn','since':time.monotonic(),'start':time.monotonic(),'sample':0,'pulse':0,'handle':None,'aim':None}


def snapshot():
    return json.loads(unreal.GuLiGroundRogueDebugLibrary.read_state(PC,CENTER,1800))


def hud():
    return next(h for h in unreal.ObjectIterator(unreal.GuLiGroundHUDWidget) if h.is_in_viewport())


def finish(error=None):
    if STATE['handle'] is not None:
        unreal.unregister_slate_post_tick_callback(STATE['handle']);STATE['handle']=None
    if STATE['aim'] is not None:
        PAWN.submit_gameplay_input(unreal.Vector2D(),STATE['aim'],False,False,False,True,True)
    if error:REPORT['errors'].append(error)
    REPORT['final']=snapshot()
    REPORT['hud_final']=json.loads(unreal.GuLiGroundHUDAssetLibrary.read_motion_state(hud()))
    REPORT['burst_particles_remaining']=0
    for component in unreal.ObjectIterator(unreal.NiagaraComponent):
        asset=component.get_asset()
        if asset and asset.get_name()=='NS_GroundBurningBurst' and component.get_world()==WORLD:
            data=json.loads(unreal.GuLiCombatEffectAuthoringLibrary.inspect_burning_burst_component(component))
            REPORT['burst_particles_remaining']+=sum(e['particles'] for e in data.get('emitters',[]))
    if not error:
        presentation=REPORT['final']['presentation']
        if not (presentation['burst_receipts']==presentation['burst_accepted']==4 and presentation['burst_suppressed']==presentation['burst_rejected']==0):
            REPORT['errors'].append('Every one of four live burst receipts must be presented exactly once: '+str(presentation))
        bursts={b['last_burst']:b for s in REPORT['samples'] for b in s['state']['buffs'] if b['burst_at']}
        at=sorted(b['burst_at'] for b in bursts.values())
        if len(at)!=4 or any(b-a!=120 for a,b in zip(at,at[1:])):
            REPORT['errors'].append('Expected four independent 120-tick periods: '+str(at))
        particles=[e for p in REPORT['particles'] for e in p['data']['emitters'] if e['particles']]
        if not particles or any(e['outer_radius']>500.1 or abs(e['lifetime']-1)>.001 for e in particles):
            REPORT['errors'].append('Burst geometry/lifetime must stay inside 500 cm / 1 second')
        if not any(abs(e['outer_radius']-500)<.1 and e['max_age']>=.5 for e in particles):
            REPORT['errors'].append('No full-radius hold observed')
        if REPORT['burst_particles_remaining']:
            REPORT['errors'].append('Burst particles were not reclaimed after the last pulse')
        REPORT['period_ticks']=at
    REPORT['success']=not REPORT['errors']
    (OUT/'runtime_burst.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')


def phase(name):
    STATE.update(phase=name,since=time.monotonic())


def tick(delta):
    try:
        now=time.monotonic()
        if now-STATE['start']>70:finish('Timed out: '+STATE['phase']);return
        qa=json.loads(unreal.GuLiRogueCardQALibrary.snapshot(PC))
        if STATE['phase'].startswith('open_'):
            if qa.get('open'):unreal.GuLiRogueCardQALibrary.action(PC,'Cancel',0);return
            if qa.get('closing'):return
            unreal.GuLiRogueCardQALibrary.action(PC,'Open',0)
            phase('pick_burn' if STATE['phase']=='open_burn' else 'pick_burst');return
        if STATE['phase'].startswith('pick_'):
            if qa.get('phase')!=1 or not qa.get('ground_materials_ready'):return
            d=unreal.GameplayStatics.get_actor_of_class(WORLD,DIRECTOR)
            ids=[str(x) for x in d.get_editor_property('LiveCardIds')]
            wanted='07.01' if STATE['phase']=='pick_burn' else '08.01'
            assert wanted in ids,(wanted,ids)
            texts=[str(t.get_text()) for t in unreal.ObjectIterator(unreal.RichTextBlock) if '游骑兵' in str(t.get_text())]
            REPORT['cards'].append({'wanted':wanted,'candidates':ids,'descriptions':texts,'session':qa['session']})
            assert unreal.GuLiRogueCardQALibrary.action(PC,'Hit',ids.index(wanted))
            # Real duplicate user action must be locked, rather than predicting a second acquisition.
            unreal.GuLiRogueCardQALibrary.action(PC,'Hit',ids.index(wanted))
            phase('wait_burn' if wanted=='07.01' else 'wait_burst');return
        if STATE['phase'].startswith('wait_'):
            if qa.get('open') or qa.get('closing'):return
            if STATE['phase']=='wait_burn':phase('open_burst');return
            state=snapshot();REPORT['before']=state
            host=min((t for t in state['targets'] if t['max_health']==3000),key=lambda t:sum((a-b)**2 for a,b in zip(t['center'],(-13950,51150,1075))))
            assert host['health']==3000 and not host['dead'],'Fixture damaged before firing'
            assert all(t['health']==t['max_health'] for t in state['targets']),'Unrelated target damage'
            aim=unreal.Vector(*host['center']);STATE['aim']=aim
            pos=unreal.Vector(aim.x+2400,aim.y+2400,aim.z+280)
            assert unreal.GuLiGroundRogueDebugLibrary.prepare_shooter(PC,pos,aim),'Prepare owned shooter'
            REPORT['hud_before']=json.loads(unreal.GuLiGroundHUDAssetLibrary.read_motion_state(hud()))
            phase('settle');return
        if STATE['phase']=='settle':
            if now-STATE['since']<1.2:return
            REPORT['shooter']={'position':list(PAWN.get_actor_location().to_tuple()),'aim':list(STATE['aim'].to_tuple())}
            assert PAWN.submit_gameplay_input(unreal.Vector2D(),STATE['aim'],False,True,False,True,True)
            phase('firing');return
        if STATE['phase']=='firing':
            if now-STATE['since']<.10:return
            PAWN.submit_gameplay_input(unreal.Vector2D(),STATE['aim'],False,False,False,True,True)
            REPORT['materials_after_first_shot']={name:unreal.GuLiCombatEffectAuthoringLibrary.get_buff_compile_diagnostics(unreal.load_asset('/Game/GuLiStrike/FX/GroundMech/Burning/'+name)) for name in ('NS_GroundBurningBullet','NS_GroundBurningBurst')}
            for name,diagnostic in REPORT['materials_after_first_shot'].items():
                assert 'Niagara renderer proxy fallback=0' in diagnostic and 'Niagara renderer proxy fallback=1' not in diagnostic and 'ERROR:' not in diagnostic,(name,diagnostic)
            phase('observe');return
        if STATE['phase']=='observe':
            if now-STATE['sample']>=.12:
                STATE['sample']=now;s=snapshot();REPORT['samples'].append({'time':now-STATE['since'],'state':s})
                (OUT/'runtime_burst_progress.json').write_text(json.dumps({'phase':'observe','elapsed':now-STATE['since'],'samples':len(REPORT['samples']),'presentation':s['presentation']}),encoding='utf-8')
                for component in unreal.ObjectIterator(unreal.NiagaraComponent):
                    asset=component.get_asset()
                    if asset and asset.get_name()=='NS_GroundBurningBurst' and component.get_world()==WORLD:
                        data=json.loads(unreal.GuLiCombatEffectAuthoringLibrary.inspect_burning_burst_component(component))
                        if any(e['particles'] for e in data.get('emitters',[])):
                            REPORT['particles'].append({'time':now-STATE['since'],'component':component.get_name(),'data':data})
            if now-STATE['since']>=10.8:
                assert REPORT['samples'] and any(s['state']['buffs'] for s in REPORT['samples']),'No accepted burning hit'
                assert REPORT['particles'],'No rendered burst particle component observed'
                finish();return
    except Exception:
        finish(traceback.format_exc())


STATE['handle']=unreal.register_slate_post_tick_callback(tick)
unreal.MCPythonHelper.submit_result(json.dumps({'started':True,'output':str(OUT/'runtime_burst.json')}))
