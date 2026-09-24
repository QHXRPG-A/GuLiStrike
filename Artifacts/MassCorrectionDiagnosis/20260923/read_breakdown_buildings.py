import json
from pathlib import Path
from collections import Counter
import unreal

w = unreal.find_object(None, '/Game/Maps/UEDPIE_0_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype')
assert w
bc = unreal.load_class(None, '/Script/GuLiStrike.GuLiPlacedBuilding')
sc = unreal.load_class(None, '/Script/GuLiStrike.GuLiBuildingShieldComponent')
rows=[]
for actor in unreal.GameplayStatics.get_all_actors_of_class(w,bc):
    shield=actor.get_component_by_class(sc)
    row=dict(name=actor.get_name(),shield_tick=shield.is_component_tick_enabled(),
             shield_enabled=shield.is_shield_enabled(),dormancy=str(actor.get_editor_property('net_dormancy')))
    for name in ('definition_id','building_type','team'):
        try: row[name]=str(actor.get_editor_property(name))
        except Exception as exc: row[name+'_error']=str(exc)
    rows.append(row)
result=dict(frame=unreal.SystemLibrary.get_frame_count(),world=w.get_path_name(),rows=rows,
            counts=dict(total=len(rows),shield_tick=sum(r['shield_tick'] for r in rows),
                        shield_enabled=sum(r['shield_enabled'] for r in rows)))
Path('D:/UE5.7/test1/Artifacts/MassCorrectionDiagnosis/20260923/breakdown-buildings.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'counts':result['counts'],'frame':result['frame']}))
