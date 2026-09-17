import os,unreal,json
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert os.getpid()==43932
rows=[]
life=unreal.load_class(None,'/Script/GuLiStrike.GuLiBuildingLifecycleComponent')
for actor in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor):
 c=actor.get_component_by_class(life)
 if not c: continue
 state=c.get_state()
 if state.get_editor_property('DefinitionId') not in (4,5,6):continue
 item={'actor':actor.get_path_name(),'id':state.definition_id,'territory':state.territory_index,'phase':str(state.phase),'location':str(actor.get_actor_location()),'rotation':str(actor.get_actor_rotation()),'ramps':[]}
 for childc in actor.get_components_by_class(unreal.ChildActorComponent):
  child=childc.get_editor_property('child_actor')
  if child:
   for ramp in child.get_components_by_class(unreal.load_class(None,'/Script/GuLiStrike.GuLiGroundAccessRampComponent')):
    item['ramps'].append({'path':ramp.get_path_name(),'visible':ramp.is_visible(),'collision':str(ramp.get_collision_enabled()),'location':str(ramp.get_world_location())})
 rows.append(item)
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'pid':os.getpid(),'world':world.get_path_name(),'buildings':rows}))
