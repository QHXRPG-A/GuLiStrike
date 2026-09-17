import os,unreal,json
assert os.getpid()==39244
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert 'UEDPIE_0' in world.get_path_name()
items=[]
for actor in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.load_class(None,'/Script/GuLiStrike.GuLiCommanderPresentationActor')):
 for c in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
  if not c.get_name().startswith('UnitInstances'):continue
  for i in range(c.get_instance_count()):
   t=c.get_instance_transform(i,True)
   if t.translation.y<0 and abs(t.scale3d.x)>0.001:
    items.append({'component':c.get_path_name(),'index':i,'position':[t.translation.x,t.translation.y,t.translation.z]})
corner=min(items,key=lambda x:x['position'][0]+x['position'][1])
for actor in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor):
 comp=actor.get_component_by_class(unreal.load_class(None,'/Script/GuLiStrike.GuLiBuildingProductionComponent'))
 if comp:comp.set_component_tick_enabled(False)
unreal.SystemLibrary.execute_console_command(world,'guli.stronghold.CaptureSeconds 1000000000')
managers=[]
if hasattr(unreal,'ObjectIterator'):
 for manager in unreal.ObjectIterator(unreal.load_class(None,'/Script/GuLiStrike.GuLiGroundCrowdManager')):
  managers.append({'path':manager.get_path_name(),'count':manager.get_ground_obstacle_count()})
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'pid':os.getpid(),'world':world.get_path_name(),'units':len(items),'corner':corner,'managers':managers,'navigation_building':unreal.NavigationSystemV1.is_navigation_being_built_or_locked(world)}))
