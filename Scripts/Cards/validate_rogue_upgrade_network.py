"""Actual two-player RPC/color/role-cleanup regression; no image inspection."""
import json
import time
import traceback
from pathlib import Path
import unreal

NET_MODE=globals().get('ROGUE_NET_MODE',1)
NET_ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
NET_OUT=NET_ROOT/f'Artifacts/RogueCards/Upgrade/network-mode-{NET_MODE}.json'
NET_QA=unreal.GuLiRogueCardQALibrary
NET_ARRAY=unreal.NiagaraDataInterfaceArrayFunctionLibrary
NET_SYSTEM=unreal.load_asset('/Game/GuLiStrike/FX/RogueCards/NS_RogueUpgrade_Lite')
NET={'phase':'ready','start':time.monotonic(),'since':time.monotonic(),'round':0,'events':[],'samples':[],
     'mode':NET_MODE,'success':False,'visual_review':'user_pending','locals':[],'home':{},'seen':{}}
NET_HANDLE=None

def net_step(value): NET['phase']=value; NET['since']=time.monotonic()
def net_clean(s): return not any(s[k] for k in ('open','closing','submitting','overlay','capture','director','target','ticking')) and s['blur']==0
def net_event(name,**data): NET['events'].append({'name':name,'time':round(time.monotonic()-NET['start'],3),**data})
def net_finish(error=None):
    global NET_HANDLE
    if NET_HANDLE is not None:
        unreal.unregister_slate_post_tick_callback(NET_HANDLE); NET_HANDLE=None
    NET['success']=error is None
    if error: NET['error']=error
    report={k:v for k,v in NET.items() if k not in ('locals','home','server','actor')}
    NET_OUT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()

def net_colors(pc):
    prefix=pc.get_path_name().split(':')[0]
    colors=[]
    for c in unreal.ObjectIterator(unreal.NiagaraComponent):
        if c.get_path_name().startswith(prefix+':') and c.get_asset()==NET_SYSTEM:
            colors.extend([v for v in NET_ARRAY.get_niagara_array_color(c,'User.UpgradeColors') if v.a>0])
    return sorted(set((round(v.r,4),round(v.g,4),round(v.b,4),round(v.a,4)) for v in colors))

def net_begin_round():
    actor=NET['locals'][NET['round']]; NET['actor']=actor
    team=json.loads(NET_QA.snapshot(actor))['team']
    units=[u for u in json.loads(unreal.GuLiTeleportQALibrary.snapshot(NET['server']))['units'] if u['team']==team and u['type']==2 and u['health']>0]
    center=unreal.Vector(*(sum(u[k] for u in units)/len(units) for k in ('x','y','z')))
    # Put both independent viewports over the upgraded army so enemy colors can be observed numerically.
    for pc in NET['locals']:
        camera=pc.player_camera_manager; origin=camera.get_camera_location(); forward=camera.get_camera_rotation().get_forward_vector()
        ground=origin+forward*((center.z-origin.z)/forward.z)
        delta=center-ground; delta.z=0
        pawn=pc.get_controlled_pawn(); pawn.set_actor_location(pawn.get_actor_location()+delta,False,True)
    NET['seen']={}; NET_QA.action(actor,'Open'); net_step('choose')

def net_tick(dt):
    try:
        now=time.monotonic(); elapsed=now-NET['since']
        if now-NET['start']>130: raise RuntimeError('network test timeout in '+NET['phase'])
        if NET['phase']=='ready':
            controllers=[pc for pc in unreal.ObjectIterator(unreal.GuLiCommanderPlayerController) if '/UEDPIE_' in pc.get_path_name()]
            locals_=[pc for pc in controllers if json.loads(NET_QA.snapshot(pc)).get('local')]
            if len(locals_)!=2: return
            states=[json.loads(NET_QA.snapshot(pc)) for pc in locals_]
            if not all(s.get('ready') and s.get('commander') for s in states): return
            assert len(set(s['team'] for s in states))==2
            locals_.sort(key=lambda pc:json.loads(NET_QA.snapshot(pc))['team'])
            NET['locals']=locals_
            NET['home']={str(i):pc.get_controlled_pawn().get_actor_location() for i,pc in enumerate(locals_)}
            server=next(pc for pc in controllers if json.loads(unreal.GuLiTeleportQALibrary.snapshot(pc))['net_mode']!=3)
            NET['server']=server
            net_event('both_commanders_ready',states=states)
            previous=json.loads((NET_ROOT/'Artifacts/RogueCards/Upgrade/runtime-regression.json').read_text())['session']
            guid,valid=unreal.GuidLibrary.parse_string_to_guid(previous); assert valid
            for pc in locals_: NET_QA.replay(pc,guid,'01.01',False); NET_QA.replay(pc,guid,'',True)
            net_step('old_match')
            return
        if not NET['locals']: return
        actor=NET.get('actor',NET['locals'][0]); snap=json.loads(NET_QA.snapshot(actor))
        if NET['phase']=='old_match' and elapsed>.3:
            assert all(json.loads(NET_QA.snapshot(pc))['upgrade_active']==0 for pc in NET['locals'])
            net_event('previous_match_session_ignored'); net_begin_round()
        elif NET['phase']=='choose' and snap.get('phase')==1:
            guid,valid=unreal.GuidLibrary.parse_string_to_guid(snap['session']); assert valid
            NET_QA.replay(actor,guid,'',True)
            NET['session']=snap['session']; NET_QA.action(actor,'Hit',0); net_step('flip')
        elif NET['phase']=='flip' and snap.get('phase')==3:
            assert all(json.loads(NET_QA.snapshot(pc))['upgrade_active']==0 for pc in NET['locals'])
            net_event('uncommitted_ready_ignored',round=NET['round'])
            NET_QA.action(actor,'Hit',0); net_step('exit')
        elif NET['phase'] in ('exit','vfx'):
            for i,pc in enumerate(NET['locals']):
                state=json.loads(NET_QA.snapshot(pc)); colors=net_colors(pc) if state['upgrade_components'] else []
                if colors:
                    own=i==NET['round']
                    assert all(c[0]>0 and (c[1]>0 and c[2]>0 if own else c[1]==0 and c[2]==0) for c in colors)
                    NET['seen'][str(i)]={'team':state['team'],'colors':colors,'own':own}
                NET['samples'].append({'round':NET['round'],'viewer':i,'time':round(now-NET['start'],3),**state})
            if NET['phase']=='exit' and not snap['open']:
                assert net_clean(snap); net_event('UI_closed',round=NET['round']); net_step('vfx')
            elif NET['phase']=='vfx' and elapsed>1.5:
                assert len(NET['seen'])==2,'both viewers must observe the effect'
                assert all(json.loads(NET_QA.snapshot(pc))['upgrade_components']==0 for pc in NET['locals'])
                net_event('friendly_gold_enemy_red',round=NET['round'],seen=NET['seen'])
                guid,valid=unreal.GuidLibrary.parse_string_to_guid(NET['session']); assert valid
                NET_QA.replay(actor,guid,'',True); NET_QA.replay(actor,guid,'01.01',False); net_step('duplicate')
        elif NET['phase']=='duplicate' and elapsed>.3:
            assert all(json.loads(NET_QA.snapshot(pc))['upgrade_active']==0 for pc in NET['locals'])
            NET['round']+=1
            if NET['round']<2: net_begin_round()
            else:
                # Server-side role loss must close whichever local viewport owns that player.
                NET['actor']=NET['locals'][0]; NET_QA.action(NET['actor'],'Open'); net_step('revoke')
        elif NET['phase']=='revoke' and elapsed>.4:
            server_prefix=NET['server'].get_path_name().split(':')[0]
            target_team=json.loads(NET_QA.snapshot(NET['actor']))['team']
            server_pc=next(pc for pc in unreal.ObjectIterator(unreal.PlayerController)
                if pc.get_path_name().startswith(server_prefix+':') and json.loads(NET_QA.snapshot(pc)).get('team')==target_team)
            assert unreal.GuLiTeleportQALibrary.revoke_commander_role(server_pc)
            net_step('revoked')
        elif NET['phase']=='revoked' and elapsed>.6:
            state=json.loads(NET_QA.snapshot(NET['actor'])); assert net_clean(state) and not state['commander']
            net_event('role_change_cleans_UI',snapshot=state); net_finish()
    except Exception: net_finish(traceback.format_exc())

assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
assert unreal.GuLiComponentSkillQALibrary.start_pie(NET_MODE,2)
NET_HANDLE=unreal.register_slate_post_tick_callback(net_tick)
print(json.dumps({'started':True,'mode':NET_MODE,'report':str(NET_OUT)}))
