import unreal, json, math, time
from pathlib import Path
out=Path('D:/UE5.7/test1/Artifacts/MassNavRepair20260924/current-stuck')
state={'next':0.,'expires':time.monotonic()+150,'rows':[]}
def tick(dt):
    if time.monotonic()<state['next']: return
    state['next']=time.monotonic()+.15
    if time.monotonic()>state['expires']:
        unreal.unregister_slate_post_tick_callback(state['handle']); return
    for a in unreal.ObjectIterator(unreal.GuLiBattleAuthoritySubsystem):
        if 'UEDPIE_0' not in a.get_path_name(): continue
        d=json.loads(str(a.get_move_response_diagnostics()))
        if not d.get('soldiers'): continue
        w=a.get_outer()
        units=[s for s in d['soldiers'] if s['alive'] and math.hypot(s['position'][0]+18600,s['position'][1]-60000)<2500]
        buildings=[{'name':b.get_name(),'center':list(b.get_actor_location().to_tuple())} for b in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiPlacedBuilding) if math.hypot(b.get_actor_location().x+18600,b.get_actor_location().y-60000)<2500]
        row={'sim':d['simulation_seconds'],'nav_generation':d['nav_generation'],'units':units,'buildings':buildings,'recovery':{k:v for k,v in d.items() if k.startswith('recovery')}}
        state['rows'].append(row)
        if d['simulation_seconds']>=35:
            out.joinpath('exact-repro-timeline.json').write_text(json.dumps(state['rows']),encoding='utf-8')
            out.joinpath('exact-repro-paused.json').write_text(json.dumps(d,indent=2),encoding='utf-8')
            print('EXACT_REPRO_PAUSED',unreal.GameplayStatics.set_game_paused(w,True),d['simulation_seconds'])
            unreal.unregister_slate_post_tick_callback(state['handle']);return
state['handle']=unreal.register_slate_post_tick_callback(tick)
unreal._codex_nav_exact_repro=state
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
print('EXACT_REPRO_STARTED')
