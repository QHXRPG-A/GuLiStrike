"""Capture the user-requested repeat at the gift-building site after loading R2."""
import unreal,json,time,os,math
from pathlib import Path
out=Path('D:/UE5.7/test1/Artifacts/MassNavRepair20260924/current-stuck')
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
world=editor.get_editor_world()
assert world and '/Game/Maps/LVL_CommanderMassPrototype.' in world.get_path_name()
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
out.joinpath('loaded-r2-scene.json').write_text(json.dumps({'pid':os.getpid(),'map':world.get_path_name(),'actors':len(actors),'pie_before':level.is_in_play_in_editor(),'review':[{'name':a.get_actor_label(),'position':list(a.get_actor_location().to_tuple())} for a in actors if a.get_actor_label().startswith('MassNavRepair_')]},indent=2),encoding='utf-8')
state={'next':0.,'expires':time.monotonic()+210,'rows':[],'cohort':set()}
def tick(dt):
    if time.monotonic()<state['next']:return
    state['next']=time.monotonic()+.3
    if time.monotonic()>state['expires']:
        out.joinpath('verify-r2-timeout.json').write_text(json.dumps({'rows':len(state['rows'])}))
        unreal.unregister_slate_post_tick_callback(state['handle']);return
    for a in unreal.ObjectIterator(unreal.GuLiBattleAuthoritySubsystem):
        if 'UEDPIE_0' not in a.get_path_name():continue
        d=json.loads(str(a.get_move_response_diagnostics()))
        if not d.get('soldiers'):continue
        w=a.get_outer()
        near=[s for s in d['soldiers'] if s['alive'] and math.hypot(s['position'][0]+18600,s['position'][1]-60000)<2500]
        if 10<d['simulation_seconds']<35:state['cohort'].update(s['id'] for s in near)
        buildings=[]
        nav=next(n for n in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.RecastNavMesh) if 'CommanderSoldier' in n.get_name())
        for b in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiPlacedBuilding):
            if math.hypot(b.get_actor_location().x+18600,b.get_actor_location().y-60000)>2000:continue
            center,extent=b.get_actor_bounds(True,False)
            foot=unreal.Vector(center.x,center.y,center.z-extent.z+1)
            q=unreal.NavigationSystemV1.project_point_to_navigation(w,foot,nav,None,unreal.Vector(.1,.1,50))
            buildings.append({'name':b.get_name(),'center':list(center.to_tuple()),'extent':list(extent.to_tuple()),'ground_inside_walkable':list(q.to_tuple()) if q else None})
        row={'sim':d['simulation_seconds'],'tick':d['sim_tick'],'nav_generation':d['nav_generation'],'recovery':{k:v for k,v in d.items() if k.startswith('recovery')},'near':near,'buildings':buildings}
        state['rows'].append(row)
        with out.joinpath('verify-r2-timeline.jsonl').open('a',encoding='utf-8') as f:f.write(json.dumps(row)+'\n')
        if d['simulation_seconds']>=90:
            report={'pid':os.getpid(),'sim':d['simulation_seconds'],'cohort_ids':sorted(state['cohort']),'cohort_final':[s for s in d['soldiers'] if s['id'] in state['cohort']],'last_region':row,'sample_count':len(state['rows'])}
            out.joinpath('verify-r2-final.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
            unreal.unregister_slate_post_tick_callback(state['handle']);return
out.joinpath('verify-r2-timeline.jsonl').write_text('',encoding='utf-8')
state['handle']=unreal.register_slate_post_tick_callback(tick)
unreal._codex_nav_r2_verification=state
level.editor_request_begin_play()
print('R2_REPRO_REQUESTED')
