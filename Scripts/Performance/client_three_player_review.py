"""Optional camera/fixture helpers. Saved production resources need no override."""
import json,time,traceback
from pathlib import Path
import unreal

if globals().get('guli_client_three_review_state',{}).get('handle'):
    guli_client_three_review_close()
_review_out=Path(unreal.Paths.project_dir())/'outputs/performance/20261009-client-three-optimizations'
_review_definition=unreal.load_asset('/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundMissile')
assert _review_definition.get_editor_property('FlightPresentationProfile')
assert _review_definition.get_editor_property('GroundWarningStyle')
guli_client_three_review_state={'phase':'ready','handle':None,'last_check':0,
    'started':bool(unreal.EditorLevelLibrary.get_pie_worlds(True)),'installed':set(),
    'camera_originals':{},'pending_ground':None,'out':_review_out,
    'assets':{'definition':_review_definition,'warning':_review_definition.get_editor_property('GroundWarningStyle')}}

def _guli_review_write(state,**extras):
    report={'success':state['phase']!='failed','phase':state['phase'],
        'client_worlds':sorted(state['installed']),'production_references_applied':True,
        'resource_overrides':False,**extras}
    (state['out']/'manual-review-state.json').write_text(json.dumps(report,indent=2),encoding='utf-8')

def guli_client_three_review_close():
    state=guli_client_three_review_state
    if state.get('handle'):
        unreal.unregister_slate_post_tick_callback(state['handle']);state['handle']=None
    state['pending_ground']=None;state['camera_originals'].clear();state['phase']='closed'
    _guli_review_write(state)

def _guli_review_clients():
    rows=[]
    for w in unreal.EditorLevelLibrary.get_pie_worlds(True):
        pc=unreal.GameplayStatics.get_player_controller(w,0)
        if pc and pc.is_local_controller():rows.append((w,pc))
    assert rows,'Start PIE first'
    return rows

def guli_client_three_review_view(view='fx'):
    """fx/near/middle/far/offscreen/wingman/game; current clients only."""
    assert view in ('fx','near','middle','far','offscreen','wingman','game')
    state=guli_client_three_review_state
    for w,pc in _guli_review_clients():
        key=w.get_path_name()
        if key not in state['camera_originals']:
            state['camera_originals'][key]=(pc.get_view_target(),pc.get_editor_property('bAutoManageActiveCameraTarget'))
        if view=='game':
            target,automatic=state['camera_originals'][key]
            pc.set_editor_property('bAutoManageActiveCameraTarget',automatic)
            pc.set_view_target_with_blend(pc.get_pawn() or target,0);continue
        name={'fx':'Near','near':'Near','middle':'Middle','far':'Far','offscreen':'Far','wingman':'Wingman'}[view]
        cam=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.CameraActor)
                 if a.get_actor_label()=='ClientThreeReview_Camera_'+name)
        if name=='Far':
            loc=unreal.Vector(-14800,-33800,7600);cam.set_actor_location(loc,False,True)
            target=unreal.Vector(-14800,-19200,1500) if view!='offscreen' else unreal.Vector(50000,-33800,7600)
            cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(loc,target),True)
        pc.set_editor_property('bAutoManageActiveCameraTarget',False);pc.set_view_target_with_blend(cam,0)
        if view=='fx':unreal.SystemLibrary.execute_console_command(w,'gs.Perf.Review start PerfReview_Client3')

def guli_client_three_review_stop_fx():
    for w,pc in _guli_review_clients():unreal.SystemLibrary.execute_console_command(w,'gs.Perf.Review stop PerfReview_Client3')

def guli_client_three_review_stop_ground():
    guli_client_three_review_state['pending_ground']=None
    for w in unreal.EditorLevelLibrary.get_pie_worlds(True):
        if unreal.SystemLibrary.is_server(w):unreal.SystemLibrary.execute_console_command(w,'gs.Flights.Stop')

def guli_client_three_review_start():
    """Optional dedicated-server/Ground/Air start, only when explicitly called."""
    assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
    w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    nav=unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(w,True)
    bake=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    assert nav.success and bake.success,(nav.message,bake.message)
    assert unreal.GuLiComponentSkillQALibrary.start_performance_pie(True,1280,720)

def guli_client_three_review_ground(count=125):
    """Actual ground Definition/source, waiting for authoritative hangar deployment."""
    assert count in (125,250,500);_guli_review_clients()
    worlds=unreal.EditorLevelLibrary.get_pie_worlds(True)
    server=next((w for w in worlds if unreal.SystemLibrary.is_server(w)),None)
    assert server,'An in-process authority World is required'
    ships=unreal.GameplayStatics.get_all_actors_of_class(server,unreal.GuLiStrikeShip)
    assert ships,'Enter the Air seat or use the optional Ground/Air start first'
    ship=ships[0]
    marker=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(server,unreal.Actor) if unreal.Name('FlightEventsQAOrigin') in a.tags)
    o=marker.get_actor_location();ship.set_actor_location(unreal.Vector(o.x,o.y,o.z+9100),False,True)
    if not ship.get_hangar_capability():
        assert unreal.GuLiComponentSkillQALibrary.commit_ship_choice(ship.player_state.get_ship_build(),'08') is not None
    fixture=next(s for s in unreal.ObjectIterator(unreal.GuLiFlightAcceptanceSubsystem) if s.get_outer()==server)
    assert not json.loads(fixture.get_load_stats_json())['running'],'Stop the existing load first'
    now=time.monotonic()
    guli_client_three_review_state['pending_ground']={'count':count,'server':server.get_path_name(),
        'next_attempt':now,'deadline':now+20}

def _guli_review_tick(delta):
    state=guli_client_three_review_state;now=time.monotonic()
    if now-state['last_check']<.1:return
    state['last_check']=now
    try:
        worlds=unreal.EditorLevelLibrary.get_pie_worlds(True)
        if not worlds:
            if state['started']:guli_client_three_review_close()
            return
        state['started']=True
        state['installed']={w.get_path_name() for w in worlds
            if (pc:=unreal.GameplayStatics.get_player_controller(w,0)) and pc.is_local_controller()}
        pending=state['pending_ground']
        if pending and now>=pending['next_attempt']:
            pending['next_attempt']=now+.5
            if now>pending['deadline']:
                state['pending_ground']=None
                _guli_review_write(state,ground_load_error='Production Wingman not ready within 20 seconds')
                unreal.log_warning('Ground review: inspect the Air seat and deployed hangar Wingman.');return
            server=next((w for w in worlds if w.get_path_name()==pending['server']),None)
            snapshots=json.loads(unreal.GuLiComponentSkillQALibrary.wingman_snapshot(server)) if server else []
            if any(r['lifecycle']==2 and r['config_usable'] for r in snapshots):
                fixture=next(s for s in unreal.ObjectIterator(unreal.GuLiFlightAcceptanceSubsystem) if s.get_outer()==server)
                if fixture.start_wingman_ground_load(pending['count'],45,state['assets']['warning']):
                    state['pending_ground']=None;guli_client_three_review_view('wingman')
                    _guli_review_write(state,ground_load_started=pending['count'])
    except Exception:
        error=traceback.format_exc();guli_client_three_review_close();state['phase']='failed'
        _guli_review_write(state,error=error);unreal.log_error(error)

state=guli_client_three_review_state
state['handle']=unreal.register_slate_post_tick_callback(_guli_review_tick)
_guli_review_tick(0);_guli_review_write(state)
unreal.log('Saved production optimizations active. Optional view/ground helpers ready; normal PIE needs no script.')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'helpers_ready':True,'automatic_play':False,
    'production_references_applied':True,'resource_overrides':False}))
