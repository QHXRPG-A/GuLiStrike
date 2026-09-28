"""Requested commit-set/death/newborn/quality/initialization checks and uninspected temporal captures."""
import json
import time
import traceback
from pathlib import Path
import unreal

LIFE_ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
LIFE_OUT=LIFE_ROOT/'Artifacts/RogueCards/Upgrade/lifecycle.json'
LIFE_QA=unreal.GuLiRogueCardQALibrary
LIFE={'phase':'ready','start':time.monotonic(),'since':time.monotonic(),'events':[],'samples':[],
      'captures':[],'success':False,'visual_review':'user_pending'}
LIFE_HANDLE=None

def life_step(phase):
    LIFE['phase']=phase; LIFE['since']=time.monotonic()
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    LIFE['world_since']=unreal.GameplayStatics.get_time_seconds(world) if world else 0
def life_clean(s): return not any(s[k] for k in ('open','closing','overlay','capture','director','target','ticking')) and s['blur']==0
def life_event(name,**kwargs): LIFE['events'].append({'name':name,**kwargs})
def life_finish(error=None):
    global LIFE_HANDLE
    if LIFE_HANDLE is not None: unreal.unregister_slate_post_tick_callback(LIFE_HANDLE); LIFE_HANDLE=None
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if world: unreal.SystemLibrary.execute_console_command(world,'gs.RogueCards.UpgradeQuality 2')
    LIFE['success']=error is None
    if error: LIFE['error']=error
    LIFE_OUT.write_text(json.dumps(LIFE,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()

def life_capture(pc,name,age):
    path=LIFE_ROOT/f'Artifacts/RogueCards/Upgrade/upgrade-{name}.png'
    unreal.SystemLibrary.execute_console_command(pc,f'HighResShot 1 filename="{path.as_posix()}"',pc)
    LIFE['captures'].append({'phase':name,'requested_effect_age':age,'path':str(path),'review':'user_pending'})

def life_tick(dt):
    try:
        now=time.monotonic()
        if now-LIFE['start']>65: raise RuntimeError('lifecycle timeout: '+LIFE['phase'])
        world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not world: return
        pc=unreal.GameplayStatics.get_player_controller(world,0)
        if not pc: return
        snap=json.loads(LIFE_QA.snapshot(pc)); elapsed=unreal.GameplayStatics.get_time_seconds(world)-LIFE.get('world_since',0); phase=LIFE['phase']
        if phase=='ready':
            if not snap.get('ready'): return
            roster=json.loads(unreal.GuLiTeleportQALibrary.snapshot(pc))
            units=[u for u in roster['units'] if u['team']==snap['team'] and u['type']==2 and u['health']>0]
            LIFE['before_ids']=[u['id'] for u in units]; LIFE['victim']=units[0]
            center=unreal.Vector(*(sum(u[k] for u in units)/len(units) for k in ('x','y','z')))
            pc.get_controlled_pawn().jump_to_world_location(center)
            unreal.SystemLibrary.execute_console_command(world,'gs.RogueCards.UpgradeQuality 0',pc)
            LIFE_QA.action(pc,'Open'); life_step('choose')
        elif phase=='choose' and snap.get('phase')==1:
            LIFE['session']=snap['session']; LIFE_QA.action(pc,'Hit',0); life_step('flip')
        elif phase=='flip' and snap.get('phase')==3:
            LIFE_QA.action(pc,'Hit',0); life_step('committing')
        elif phase=='committing' and snap['committed']:
            assert snap['upgrade_active']==0
            victim=LIFE['victim']; assert unreal.GuLiTeleportQALibrary.damage_mass(pc,victim['id'],10000000)
            fixture=json.loads(LIFE_QA.build_fixture(pc,1,unreal.Vector(victim['x']+5000,victim['y'],victim['z']),400))
            assert fixture.get('created')==1, 'newborn test spawn failed'
            LIFE['newborn']=fixture['units'][0]
            assert abs(LIFE['newborn']['attack_rate']-2.4)<.001
            life_event('post_commit_death_and_newborn',newborn=LIFE['newborn'],victim=victim['id'])
            life_step('exit')
        elif phase=='exit' and not snap['open']:
            assert life_clean(snap); life_step('vfx')
        elif phase=='vfx':
            slots=json.loads(LIFE_QA.upgrade_slots(pc)).get('slots',[])
            ids=[s['id'] for s in slots]
            LIFE['samples'].append({'time':elapsed,**snap,'slot_ids':ids})
            if .1<elapsed<.7:
                assert LIFE['victim']['id'] not in ids and LIFE['newborn']['id'] not in ids
                assert set(ids).issubset(set(LIFE['before_ids']))
                assert all(s['motes']==0 for s in slots if s['visible'])
                if snap['upgrade_visible']:
                    assert snap['upgrade_particles']==2*snap['upgrade_visible']
                    LIFE['low_quality_seen']=True
            if elapsed>.1 and not LIFE.get('replayed'):
                g,ok=unreal.GuidLibrary.parse_string_to_guid(LIFE['session']); assert ok
                LIFE_QA.replay(pc,g,'',True); LIFE['replayed']=True
            if elapsed>1.4:
                assert not ids and snap['upgrade_components']==0 and LIFE.get('low_quality_seen')
                life_event('frozen_targets_low_quality_and_cleanup_passed')
                # Break only the transient settings value while Open executes synchronously in standalone.
                settings=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrike.GuLiRogueCardSettings'))
                previous=settings.get_editor_property('DirectorClass')
                try:
                    settings.set_editor_property('DirectorClass',None)
                    LIFE_QA.action(pc,'Open')
                finally: settings.set_editor_property('DirectorClass',previous)
                assert life_clean(json.loads(LIFE_QA.snapshot(pc)))
                life_event('missing_director_initialization_cleaned')
                unreal.SystemLibrary.execute_console_command(world,'gs.RogueCards.UpgradeQuality 2',pc)
                LIFE_QA.action(pc,'Open'); life_step('capture_choose')
        elif phase=='capture_choose' and snap.get('phase')==1:
            LIFE_QA.action(pc,'Hit',1); life_step('capture_flip')
        elif phase=='capture_flip' and snap.get('phase')==3:
            LIFE_QA.action(pc,'Hit',1); life_step('capture_exit')
        elif phase=='capture_exit' and not snap['open']:
            life_step('captures')
        elif phase=='captures':
            labels=[c['phase'] for c in LIFE['captures']]
            if elapsed>=.06 and 'start' not in labels: life_capture(pc,'start',elapsed)
            elif elapsed>=.35 and 'peak' not in labels: life_capture(pc,'peak',elapsed)
            elif elapsed>=.85 and 'decay' not in labels: life_capture(pc,'decay',elapsed)
            elif elapsed>=1.4:
                assert snap['upgrade_active']==0 and snap['upgrade_components']==0
                life_event('temporal_captures_requested_no_image_inspection'); life_finish()
    except Exception: life_finish(traceback.format_exc())

assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
assert unreal.GuLiComponentSkillQALibrary.start_pie(0,1)
LIFE_HANDLE=unreal.register_slate_post_tick_callback(life_tick)
print(json.dumps({'started':True,'report':str(LIFE_OUT)}))
