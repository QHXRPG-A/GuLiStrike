import unreal,json,time,math,traceback
from pathlib import Path
O=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004/UE_Delivery_v1')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
target_actor=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='CRM_B_v4')
target=target_actor.get_component_by_class(unreal.SkeletalMeshComponent)
probe=actors.spawn_actor_from_class(unreal.SkeletalMeshActor,unreal.Vector(0,0,-100000));probe.set_actor_label('CRM_SourcePoseProbe')
source=probe.get_component_by_class(unreal.SkeletalMeshComponent);source.set_skeletal_mesh_asset(unreal.load_asset('/Game/Assets/ControlRig/Characters/Mech/Meshes/SKM_Mech'))
source.set_update_animation_in_editor(True);source.set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES);source.set_editor_property('forced_lod_model',1)
anim=unreal.load_asset('/Game/Assets/ControlRig/Characters/Mech/Animations/Mech_Walk')
source.set_animation_mode(unreal.AnimationMode.ANIMATION_BLUEPRINT);source.override_animation_data(anim,False,False,2.5,0.);source.set_play_rate(0.);source.set_position(2.5,False)
started=time.monotonic()
def tick(dt):
    if time.monotonic()-started<2.:return
    unreal.unregister_slate_post_tick_callback(handle)
    try:
        raw=unreal.AnimSequenceService.get_pose_at_time(anim.get_path_name(),2.5,True);rows=[]
        def dist(a,b):
            qa=a.rotation.to_tuple();qb=b.rotation.to_tuple();dot=abs(sum(x*y for x,y in zip(qa,qb)))/math.sqrt(sum(x*x for x in qa)*sum(y*y for y in qb))
            return {'translation_cm':(a.translation-b.translation).length(),'scale':(a.scale3d-b.scale3d).length(),'rotation_degrees':math.degrees(2*math.acos(min(1.,dot)))}
        for p in raw:
            a=source.get_socket_transform(str(p.bone_name),unreal.RelativeTransformSpace.RTS_COMPONENT);b=target.get_socket_transform(str(p.bone_name),unreal.RelativeTransformSpace.RTS_COMPONENT)
            rows.append({'bone':str(p.bone_name),'source_component_vs_raw':dist(a,p.transform),'target_component_vs_source_component':dist(b,a)})
        report={'success':True,'animation':'Mech_Walk','time_s':2.5,'source_anim_instance':str(source.get_anim_instance()),'max_source_component_vs_raw':{k:max(r['source_component_vs_raw'][k] for r in rows) for k in ['translation_cm','scale','rotation_degrees']},'max_target_vs_source_component':{k:max(r['target_component_vs_source_component'][k] for r in rows) for k in ['translation_cm','scale','rotation_degrees']},'worst':sorted(rows,key=lambda r:r['target_component_vs_source_component']['translation_cm'],reverse=True)[:8]}
    except Exception:report={'success':False,'error':traceback.format_exc()}
    (O/'animation_compression_probe.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');actors.destroy_actor(probe)
handle=unreal.register_slate_post_tick_callback(tick)
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'probe_started':True}))
