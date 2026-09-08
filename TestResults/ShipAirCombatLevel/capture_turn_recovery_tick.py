import json, unreal
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
actor=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.GuLiWingmanPresentationActor)[0]
comp=actor.get_owner_instances(); items=[]
for i in range(comp.get_instance_count()):
 t=comp.get_instance_transform(i,world_space=True); items.append({'index':i,'location':[t.translation.x,t.translation.y,t.translation.z],'rotation':[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w]})
row={'game_time':unreal.GameplayStatics.get_time_seconds(world),'items':items}
with open('D:/UE5.7/test1/TestResults/ShipAirCombatLevel/turn_recovery_timeseries.jsonl','a',encoding='utf-8') as f:f.write(json.dumps(row)+'\n')
unreal.MCPythonHelper.submit_result(json.dumps({'time':row['game_time']}))
