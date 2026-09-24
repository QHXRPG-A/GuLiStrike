import unreal,json,math,struct,re,time
from pathlib import Path
out=Path('D:/UE5.7/test1/Artifacts/MassNavRepair20260924/current-stuck')
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
a=next(a for a in unreal.ObjectIterator(unreal.GuLiBattleAuthoritySubsystem) if a.get_outer()==w)
d=json.loads(str(a.get_move_response_diagnostics()))
nav=next(n for n in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.RecastNavMesh) if 'CommanderSoldier' in n.get_name())
f32=lambda x:struct.unpack('f',struct.pack('f',x))[0]
vec=lambda x:[x.x,x.y,x.z]
def hits(start,end,radius):
    hs=unreal.SystemLibrary.sphere_trace_multi_by_profile(w,start,end,radius,'Pawn',False,[],unreal.DrawDebugTrace.NONE,False) or []
    result=[]
    for h in hs:
        t=h.to_tuple()
        if not t[0]:continue
        depth=re.search(r'PenetrationDepth=([\d.eE+-]+)',h.export_text())
        result.append({'penetrating':t[1],'time':t[2],'normal':vec(t[6]),'depth':float(depth.group(1)) if depth else 0.,'component':t[10].get_path_name() if t[10] else None})
    return result
report={'sim':d['simulation_seconds'],'recovery':{k:v for k,v in d.items() if k.startswith('recovery')},'soldiers':[]}
for s in d['soldiers']:
    if not s['alive'] or math.hypot(s['position'][0]+18600,s['position'][1]-60000)>2000:continue
    p=unreal.Vector(*s['position']);r=s['radius'];lift=unreal.Vector(0,0,r+3)
    row={'soldier':s,'candidates':[]}
    for i in range(10):
        probe=p;extent=.1;angle=None
        if i==1:extent=300
        if i>=2:
            angle=f32(f32(6.28318530717)*(i-2))/8.
            probe=p+unreal.Vector(math.cos(angle)*300,math.sin(angle)*300,0);extent=150
        q=unreal.NavigationSystemV1.project_point_to_navigation(w,probe,nav,None,unreal.Vector(extent,extent,50))
        c={'candidate':i,'probe':vec(probe),'angle':angle,'projected':vec(q) if q else None}
        if q:
            delta=q-p;sq=delta.x**2+delta.y**2
            c.update({'distance_squared_xy':sq,'distance_gate_rejects':sq>90000.,'height_delta':delta.z,
                'forward':hits(p+lift,q+lift,r),'reverse':hits(q+lift,p+lift,r),'endpoint':hits(q+lift,q+lift+unreal.Vector(.001,0,0),r)})
        row['candidates'].append(c)
    report['soldiers'].append(row)
report['buildings']=[{'name':b.get_name(),'loc':vec(b.get_actor_location()),'bounds':[vec(v) for v in b.get_actor_bounds(True,False)]} for b in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiPlacedBuilding) if math.hypot(b.get_actor_location().x+18600,b.get_actor_location().y-60000)<2000]
report['ore_obstacles']=[{'name':b.get_name(),'loc':vec(b.get_actor_location()),'radius':b.get_obstacle_radius()} for b in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiOreClusterObstacleActor) if math.hypot(b.get_actor_location().x+18600,b.get_actor_location().y-60000)<b.get_obstacle_radius()+2500]
out.joinpath('exact-candidate-probe.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('EXACT_PROBE_SAVED',report['sim'],[s['soldier']['id'] for s in report['soldiers']])
