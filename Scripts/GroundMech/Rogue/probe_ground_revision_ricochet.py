"""Real two-level card selection and live ricochet; read-only runtime evidence."""
import json
import math
import time
import traceback
from pathlib import Path
import unreal

OUT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))/'Artifacts/GroundRevision_20261103'
WORLD=unreal.EditorLevelLibrary.get_game_world()
PC=unreal.GameplayStatics.get_player_controller(WORLD,0)
PAWN=PC.get_controlled_pawn()
ROGUE=next(r for r in unreal.ObjectIterator(unreal.GuLiRogueCardSubsystem) if r.get_world()==WORLD)
PLAYER=PC.player_state.get_player_guid()
DIRECTOR=unreal.load_class(None,'/Game/GuLiStrike/CardSystem/RevealDemo/Blueprints/BP_CardRevealDirector.BP_CardRevealDirector_C')
CENTER=unreal.Vector(-8500,54000,1150)
R={'success':False,'cards':[],'samples':[],'errors':[],'visual_test':False}
S={'phase':'open','start':time.monotonic(),'since':time.monotonic(),'sample':0,'handle':None,'aim':None}
assert PAWN and unreal.SystemLibrary.get_console_variable_int_value('gs.Review.GroundIsolateCombat')==1
assert ROGUE.get_acquisition_count(PLAYER,'06.01')==0


def snapshot():
    return json.loads(unreal.GuLiGroundRogueDebugLibrary.read_state(PC,CENTER,8500))


def phase(value):
    S.update(phase=value,since=time.monotonic())


def finish(error=None):
    unreal.unregister_slate_post_tick_callback(S['handle'])
    S['handle']=None
    if S['aim'] is not None:
        PAWN.submit_gameplay_input(unreal.Vector2D(),S['aim'],False,False,False,True,True)
    if error:R['errors'].append(error)
    R['final']=snapshot()
    R['confirmed_levels']={c:ROGUE.get_acquisition_count(PLAYER,c) for c in ('06.01','07.01','08.01')}
    R['success']=not R['errors']
    (OUT/'runtime_ricochet.json').write_text(json.dumps(R,ensure_ascii=False,indent=2),encoding='utf-8')


def tick(delta):
    try:
        now=time.monotonic()
        if now-S['start']>60:finish('Timed out: '+S['phase']);return
        qa=json.loads(unreal.GuLiRogueCardQALibrary.snapshot(PC))
        if S['phase']=='open':
            if qa.get('closing'):return
            assert not qa.get('open')
            unreal.GuLiRogueCardQALibrary.action(PC,'Open',0);phase('pick');return
        if S['phase']=='pick':
            if qa.get('phase')!=1 or not qa.get('ground_materials_ready'):return
            d=unreal.GameplayStatics.get_actor_of_class(WORLD,DIRECTOR)
            ids=[str(i) for i in d.get_editor_property('LiveCardIds')]
            texts=[str(t.get_text()) for t in unreal.ObjectIterator(unreal.RichTextBlock) if '普攻折射-' in str(t.get_text())]
            old=ROGUE.get_acquisition_count(PLAYER,'06.01')
            assert any('普攻折射-'+str(old+1) in t for t in texts),texts
            R['cards'].append({'before_level':old,'descriptions':texts,'candidates':ids,'session':qa['session']})
            assert unreal.GuLiRogueCardQALibrary.action(PC,'Hit',ids.index('06.01'))
            unreal.GuLiRogueCardQALibrary.action(PC,'Hit',ids.index('06.01'))
            phase('wait');return
        if S['phase']=='wait':
            if qa.get('open') or qa.get('closing'):return
            count=ROGUE.get_acquisition_count(PLAYER,'06.01')
            assert count==len(R['cards']),(count,len(R['cards']))
            if count<2:phase('open');return
            R['before']=snapshot()
            target=min((t for t in R['before']['targets'] if t['team']==2),key=lambda t:math.dist(t['center'],(-4350,54150,1150)))
            assert target['health']==target['max_health'] and not target['dead']
            S['aim']=unreal.Vector(*target['center'])
            assert unreal.GuLiGroundRogueDebugLibrary.prepare_shooter(PC,unreal.Vector(-2800,54000,1450),S['aim'])
            phase('settle');return
        if S['phase']=='settle':
            if now-S['since']<1.2:return
            assert PAWN.submit_gameplay_input(unreal.Vector2D(),S['aim'],False,True,False,True,True)
            phase('fire');return
        if S['phase']=='fire':
            if now-S['since']<.1:return
            PAWN.submit_gameplay_input(unreal.Vector2D(),S['aim'],False,False,False,True,True)
            phase('observe');return
        if S['phase']=='observe':
            if now-S['sample']>=.08:
                S['sample']=now;R['samples'].append({'time':now-S['since'],'state':snapshot()})
            if now-S['since']<2.5:return
            routes=[r for s in R['samples'] for r in s['state']['ricochets']]
            assert routes,'No frozen route from the real shot'
            for route in routes:
                assert 6<=route['budget']<=9 and route['effects']==3,route
                assert len({h['target'] for h in route['hops']})==len(route['hops'])
                assert all(math.dist(route['center'],h['center'])<=5000.001 for h in route['hops']),route
            latest={r['key']:r for r in routes}
            assert len(latest)==1,'Expected one accepted bullet'
            route=next(iter(latest.values()))
            assert len(route['hops'])>=3,route
            for i,hop in enumerate(route['hops']):
                evidence=[r['hops'][i] for r in routes if r['key']==route['key'] and len(r['hops'])>i and r['hops'][i]['found']]
                assert evidence,'No authority receipt for hop '+str(i+1)
                assert abs(evidence[-1]['damage']+evidence[-1]['absorbed']-route['base_damage']*.8**(i+1))<.001,evidence[-1]
            R['route']=route
            finish();return
    except Exception:
        finish(traceback.format_exc())


S['handle']=unreal.register_slate_post_tick_callback(tick)
unreal.MCPythonHelper.submit_result(json.dumps({'started':True,'output':str(OUT/'runtime_ricochet.json')}))
