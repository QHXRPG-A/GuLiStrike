import unreal,json,math,re,time
from pathlib import Path
out=Path('D:/UE5.7/test1/Artifacts/MassNavRepair20260924/current-stuck')
source=(out/'watch_repro.py').read_text(encoding='utf-8')
object_types=[unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1,unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY2,unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY3,unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY4,unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY5,unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY6]
exec(source[source.index('def vec'):source.index('def tick')],globals())
a=next(a for a in unreal.ObjectIterator(unreal.GuLiBattleAuthoritySubsystem) if 'UEDPIE_0' in a.get_path_name())
d=json.loads(str(a.get_move_response_diagnostics()));w=a.get_outer()
nav=next(n for n in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.RecastNavMesh) if 'CommanderSoldier' in n.get_name())
bs=[b for b in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor) if b.get_class().get_name()=='GuLiPlacedBuilding' and abs(b.get_actor_location().x+18600)<2500 and abs(b.get_actor_location().y-60000)<2500]
for s in d['soldiers']:
    if s['alive'] and s['id'] in [152,157,174]:
        r=probe(w,s,nav);r['simulation_seconds']=d['simulation_seconds'];r['buildings']=[]
        for b in bs:
            center,extent=b.get_actor_bounds(True,False);m=b.get_component_by_class(unreal.NavModifierComponent)
            info={'name':b.get_name(),'center':vec(center),'extent':vec(extent)}
            for key in ['failsafe_extent','include_agent_height','area_class']:
                try:info[key]=str(m.get_editor_property(key))
                except Exception as ex:info[key]=str(ex)
            r['buildings'].append(info)
        (out/('repro-probe-'+str(s['id'])+'.json')).write_text(json.dumps(r,indent=2),encoding='utf-8')
        print('probed',s['id'],s['position'],s['no_progress'])
