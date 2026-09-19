import unreal,json
w=next(w for w in unreal.ObjectIterator(unreal.World) if 'UEDPIE_1_' in w.get_path_name());pc=unreal.GameplayStatics.get_player_controller(w,0);p=pc.get_controlled_pawn()
sp=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor) if a.get_class().get_name()=='BP_SpiderMech_Styled_C')
def info(a):
    mesh=a.get_component_by_class(unreal.SkeletalMeshComponent)
    return {'location':list(a.get_actor_location().to_tuple()),'rotation':str(a.get_actor_rotation()),'bounds':str(a.get_actor_bounds(False)),'mesh_relative_rotation':str(mesh.relative_rotation),'mesh_scale':list(mesh.relative_scale3d.to_tuple()),'mesh_anim':mesh.get_anim_instance().get_class().get_path_name()}
unreal.MCPythonHelper.submit_result(json.dumps({'ground':info(p),'spider':info(sp),'spawn_methods':[m for m in dir(unreal.GameplayStatics) if 'spawn' in m],'world_methods':[m for m in dir(unreal.World) if 'spawn' in m],'services':[m for m in dir(unreal) if 'ActorService' in m],'capture':unreal.AutomationLibrary.take_high_res_screenshot.__doc__}))
