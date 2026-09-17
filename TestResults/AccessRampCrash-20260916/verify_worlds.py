import os,unreal,json
assert os.getpid()==43932
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
state={'success':True,'pid':os.getpid(),'navigation_building':unreal.NavigationSystemV1.is_navigation_being_built_or_locked(world)}
for name in ['UEDPIE_0','UEDPIE_1']:
 w=unreal.find_object(None,f'/Game/Maps/{name}_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype')
 actors=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.load_class(None,'/Script/GuLiStrike.GuLiResourceFactoryActor'))
 bad=[]
 for a in actors:
  for c in a.get_components_by_class(unreal.ChildActorComponent):
   child=c.get_editor_property('child_actor')
   if child:
    for ramp in child.get_components_by_class(unreal.load_class(None,'/Script/GuLiStrike.GuLiGroundAccessRampComponent')):
     if not ramp.is_visible() or ramp.get_collision_enabled()!=unreal.CollisionEnabled.QUERY_AND_PHYSICS:bad.append(ramp.get_path_name())
 state[name]={'factories':len(actors),'invalid_ramps':bad}
unreal.MCPythonHelper.submit_result(json.dumps(state))
