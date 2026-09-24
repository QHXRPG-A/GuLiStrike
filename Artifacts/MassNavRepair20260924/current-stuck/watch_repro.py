import unreal
import json, math, time, re
from pathlib import Path

out = Path('D:/UE5.7/test1/Artifacts/MassNavRepair20260924/current-stuck')
out.joinpath('repro-timeline.jsonl').write_text('', encoding='utf-8')
state = {'next': 0, 'expires': time.monotonic() + 150, 'probed': set()}
object_types = [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1, unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY2,
                unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY3, unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY4,
                unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY5, unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY6]

def vec(v):
    return [v.x, v.y, v.z]

def hits(world, start, end, radius):
    result = []
    for h in unreal.SystemLibrary.sphere_trace_multi_for_objects(world, start, end, radius, object_types, False, [], unreal.DrawDebugTrace.NONE, False) or []:
        t = h.to_tuple()
        component = t[10]
        if not component or component.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN) != unreal.CollisionResponseType.ECR_BLOCK:
            continue
        depth = re.search(r'PenetrationDepth=([\d.eE+-]+)', h.export_text())
        result.append({'blocking': t[0], 'penetrating': t[1], 'time': t[2], 'normal': vec(t[6]),
                       'depth': float(depth.group(1)) if depth else 0, 'component': component.get_path_name()})
    return result

def probe(world, soldier, nav):
    p = unreal.Vector(*soldier['position'])
    radius = soldier['radius']
    lift = unreal.Vector(0, 0, radius + 3)
    candidates = [(p, .1), (p, 300)]
    candidates += [(p + unreal.Vector(math.cos(i*math.pi/4)*300, math.sin(i*math.pi/4)*300, 0), 150) for i in range(8)]
    report = {'soldier': soldier, 'candidates': []}
    for index, (point, extent) in enumerate(candidates):
        q = unreal.NavigationSystemV1.project_point_to_navigation(world, point, nav, None, unreal.Vector(extent, extent, 50))
        c = {'index': index, 'point': vec(q) if q else None}
        if q:
            delta = q-p
            c['distance_xy'] = math.hypot(delta.x, delta.y)
            c['height_delta'] = delta.z
            c['overlaps'] = [x.get_path_name() for x in unreal.SystemLibrary.sphere_overlap_components(world,q+lift,radius,object_types,unreal.PrimitiveComponent,[]) or []
                             if x.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN) == unreal.CollisionResponseType.ECR_BLOCK]
            c['forward'] = hits(world,p+lift,q+lift,radius)
            c['reverse'] = hits(world,q+lift,p+lift,radius)
        report['candidates'].append(c)
    return report

def tick(dt):
    now = time.monotonic()
    if now > state['expires']:
        unreal.unregister_slate_post_tick_callback(state['handle'])
        return
    if now < state['next']:
        return
    state['next'] = now + .2
    try:
        for authority in unreal.ObjectIterator(unreal.GuLiBattleAuthoritySubsystem):
            if 'UEDPIE_' not in authority.get_path_name():
                continue
            d = json.loads(str(authority.get_move_response_diagnostics()))
            if not d.get('soldiers'):
                continue
            world = authority.get_outer()
            units = [s for s in d['soldiers'] if s['alive'] and abs(s['position'][0]+18600)<2000 and abs(s['position'][1]-60000)<2000]
            buildings = []
            for b in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor):
                if b.get_class().get_name() != 'GuLiPlacedBuilding':
                    continue
                loc=b.get_actor_location()
                if abs(loc.x+18600)>1 or abs(loc.y-60000)>1:
                    continue
                center,extent=b.get_actor_bounds(True,False)
                buildings.append({'name':b.get_name(),'center':vec(center),'extent':vec(extent)})
            record={'time':time.time(),'sim':d['simulation_seconds'],'tick':d['sim_tick'],'nav_generation':d['nav_generation'],
                    'recovery':{k:v for k,v in d.items() if k.startswith('recovery')},'units':units,'buildings':buildings}
            with out.joinpath('repro-timeline.jsonl').open('a',encoding='utf-8') as f:
                f.write(json.dumps(record)+'\n')
            if buildings:
                b=buildings[0]
                nav=next((n for n in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.RecastNavMesh) if 'CommanderSoldier' in n.get_name()),None)
                for s in units:
                    if nav and s['nav_repair_pending'] and s['no_progress']>2 and s['id'] not in state['probed']:
                        state['probed'].add(s['id'])
                        result=probe(world,s,nav)
                        result['building']=b
                        result['simulation_seconds']=d['simulation_seconds']
                        out.joinpath('repro-probe-'+str(s['id'])+'.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    except Exception as exc:
        out.joinpath('repro-errors.txt').write_text(str(exc),encoding='utf-8')

state['handle']=unreal.register_slate_post_tick_callback(tick)
unreal._codex_nav_repro_watch = state
print('REPRO_CAPTURE_READY')
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
print('PIE_RESTART_REQUESTED')
