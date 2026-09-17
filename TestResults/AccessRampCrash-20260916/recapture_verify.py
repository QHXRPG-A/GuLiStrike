import os,unreal,json
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert os.getpid()==43932
for cmd in ['gs.Resources.SetTerritoryOwner 3 4 Red','gs.Resources.SetTerritoryOwner 3 4 Blue']:
 unreal.SystemLibrary.execute_console_command(world,cmd)
client=unreal.find_object(None,'/Game/Maps/UEDPIE_1_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype')
actors=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.load_class(None,'/Script/GuLiStrike.GuLiResourceFactoryActor'))
clientactors=unreal.GameplayStatics.get_all_actors_of_class(client,unreal.load_class(None,'/Script/GuLiStrike.GuLiResourceFactoryActor')) if client else []
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'pid':os.getpid(),'factory_count_authority':len(actors),'factory_count_client':len(clientactors),'navigation_building':unreal.NavigationSystemV1.is_navigation_being_built_or_locked(world),'recapture_commands':2,'r3c4_factory':[{'actor':a.get_path_name(),'team':str(a.get_team()),'location':str(a.get_actor_location())} for a in actors if a.get_component_by_class(unreal.load_class(None,'/Script/GuLiStrike.GuLiBuildingLifecycleComponent')).get_state().territory_index==13]}))
