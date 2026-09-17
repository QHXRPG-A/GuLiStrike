import os,unreal,json
assert os.getpid()==39244
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
result={'success':True,'pid':os.getpid(),'navigation_building':unreal.NavigationSystemV1.is_navigation_being_built_or_locked(w),'factory':[]}
for label in ['UEDPIE_0','UEDPIE_1']:
 world=unreal.find_object(None,f'/Game/Maps/{label}_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype')
 for factory in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.load_class(None,'/Script/GuLiStrike.GuLiResourceFactoryActor')):
  lc=factory.get_component_by_class(unreal.load_class(None,'/Script/GuLiStrike.GuLiBuildingLifecycleComponent'))
  if lc.get_state().territory_index!=13:continue
  row={'world':label,'actor':factory.get_path_name(),'location':str(factory.get_actor_location()),'ramps':[]}
  for child in factory.get_components_by_class(unreal.ChildActorComponent):
   a=child.get_editor_property('child_actor')
   if a:
    for c in a.get_components_by_class(unreal.load_class(None,'/Script/GuLiStrike.GuLiGroundAccessRampComponent')):row['ramps'].append({'visible':c.is_visible(),'collision':str(c.get_collision_enabled())})
  result['factory'].append(row)
unreal.MCPythonHelper.submit_result(json.dumps(result))
