"""Explicit structured PIE acceptance; real modal/card paths, no image capture."""
import json
import time
import traceback
from pathlib import Path
import unreal

OUT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))/'Artifacts/GroundHUDRevision_20261103'
REPORT_FILE=globals().get('GULI_REPORT_FILE','runtime_hud.json')
WORLD=unreal.EditorLevelLibrary.get_game_world()
PC=unreal.GameplayStatics.get_player_controller(WORLD,0)
HUD=next(h for h in unreal.ObjectIterator(unreal.GuLiGroundHUDWidget) if h.is_in_viewport())
DIRECTOR=unreal.load_class(None,'/Game/GuLiStrike/CardSystem/RevealDemo/Blueprints/BP_CardRevealDirector.BP_CardRevealDirector_C')
REPORT={'success':False,'samples':[],'errors':[],'visual_test':False}
STATE={'phase':'baseline','start':time.monotonic(),'since':time.monotonic(),'handle':None}

def sample(label):
    row={'label':label,'time':time.monotonic(),'world_time':unreal.GameplayStatics.get_time_seconds(WORLD),
         'hud':json.loads(unreal.GuLiGroundHUDAssetLibrary.read_motion_state(HUD)),
         'cards':json.loads(unreal.GuLiRogueCardQALibrary.snapshot(PC))}
    REPORT['samples'].append(row)
    return row

def phase(name):STATE.update(phase=name,since=time.monotonic())

def finish(error=None):
    if STATE['handle'] is not None:
        unreal.unregister_slate_post_tick_callback(STATE['handle']);STATE['handle']=None
    if error:REPORT['errors'].append(error)
    if error:
        unreal.GuLiRogueCardQALibrary.action(PC,'Cancel',0)
        HUD.call_method('CloseClicked')
    REPORT['success']=not REPORT['errors']
    (OUT/REPORT_FILE).write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),'utf-8')

def tick(delta):
    try:
        now=time.monotonic();elapsed=now-STATE['since'];p=STATE['phase']
        if now-STATE['start']>50:raise RuntimeError('Timeout '+p)
        if p=='baseline' and elapsed>=4:
            row=sample('baseline_end');before=REPORT['samples'][0]
            dt=row['world_time']-before['world_time'];count=row['hud']['ordinary_refreshes']-before['hud']['ordinary_refreshes']
            assert 0<count<=dt*5+1,(count,dt)
            assert row['hud']['ambient_running'] and row['hud']['loaded_icons']==8
            assert row['hud']['ambient_transitions']==before['hud']['ambient_transitions']
            assert row['hud']['motion_frames']==before['hud']['motion_frames'],'Ambient loop invalidated CPU motion every frame'
            assert row['hud']['resident_mib']<=64
            REPORT['refresh_hz']=count/dt
            HUD.call_method('MapClicked');phase('map');return
        if p=='map' and elapsed>=1:
            row=sample('map_open');assert row['hud']['modal']==1 and not row['hud']['ambient_running']
            HUD.call_method('CloseClicked');phase('map_closed');return
        if p=='map_closed' and elapsed>=1:
            row=sample('map_closed');assert row['hud']['modal']==0 and row['hud']['ambient_running']
            assert row['hud']['entry_plays']==REPORT['samples'][0]['hud']['entry_plays']
            unreal.GuLiRogueCardQALibrary.action(PC,'F4Down',0)
            phase('f4_held');return
        if p=='f4_held' and elapsed>=.15:
            unreal.GuLiRogueCardQALibrary.action(PC,'F4Up',0)
            phase('cards_open');return
        if p=='cards_open':
            qa=json.loads(unreal.GuLiRogueCardQALibrary.snapshot(PC))
            if not qa.get('ground_materials_ready') or qa.get('phase')!=1:return
            row=sample('cards_hidden_start');assert row['hud']['hidden'] and not row['hud']['ambient_running']
            phase('hidden');return
        if p=='hidden' and elapsed>=4:
            row=sample('cards_hidden_end');before=REPORT['samples'][-2]
            for key in ('ordinary_refreshes','motion_frames','ambient_paused_phase','ambient_transitions'):
                assert row['hud'][key]==before['hud'][key],key
            if 'ambient_sample_phase' in row['hud']:assert row['hud']['ambient_sample_phase']==before['hud']['ambient_sample_phase']
            assert row['hud']['hidden'] and not row['hud']['ambient_running']
            unreal.GuLiRogueCardQALibrary.action(PC,'Cancel',0);phase('cancel');return
        if p=='cancel' and elapsed>=1:
            row=sample('cards_cancelled');assert not row['hud']['hidden'] and row['hud']['ambient_running']
            unreal.GuLiRogueCardQALibrary.action(PC,'Open',0);phase('reopen');return
        if p=='reopen':
            qa=json.loads(unreal.GuLiRogueCardQALibrary.snapshot(PC))
            if not qa.get('ground_materials_ready') or qa.get('phase')!=1:return
            row=sample('cards_reopened');assert row['cards']['session']==REPORT['samples'][-4]['cards']['session']
            if globals().get('GULI_ONLY_AMBIENT',False):
                unreal.GuLiRogueCardQALibrary.action(PC,'Cancel',0);phase('ambient_done');return
            director=unreal.GameplayStatics.get_actor_of_class(WORLD,DIRECTOR)
            ids=[str(v) for v in director.get_editor_property('LiveCardIds')]
            assert '06.01' in ids,ids
            unreal.GuLiRogueCardQALibrary.action(PC,'Hit',ids.index('06.01'))
            unreal.GuLiRogueCardQALibrary.action(PC,'Hit',ids.index('06.01'))
            phase('confirmed');return
        if p=='ambient_done' and elapsed>=1:
            row=sample('ambient_done');assert row['hud']['ambient_running'] and row['hud']['ambient_clock']=='SlateAppTime'
            finish();return
        if p=='confirmed':
            qa=json.loads(unreal.GuLiRogueCardQALibrary.snapshot(PC))
            if qa.get('open') or qa.get('closing'):return
            if elapsed<1:return
            row=sample('confirmed');assert row['hud']['ambient_running'] and not row['hud']['hidden']
            REPORT['icons']=[]
            for image in unreal.ObjectIterator(unreal.Image):
                if not image.get_path_name().startswith(HUD.get_path_name()+'.'):continue
                obj=image.get_editor_property('brush').get_editor_property('resource_object')
                if obj and '/UI/Icons/' in obj.get_path_name():REPORT['icons'].append(obj.get_path_name())
            assert len(set(REPORT['icons']))==8,REPORT['icons']
            REPORT['state']=json.loads(unreal.GuLiGroundRogueDebugLibrary.read_state(PC,unreal.Vector(-13950,51150,1075),1800))
            finish()
    except Exception:finish(traceback.format_exc())

sample('baseline_start')
STATE['handle']=unreal.register_slate_post_tick_callback(tick)
unreal.MCPythonHelper.submit_result(json.dumps({'started':True,'output':str(OUT/REPORT_FILE)}))
