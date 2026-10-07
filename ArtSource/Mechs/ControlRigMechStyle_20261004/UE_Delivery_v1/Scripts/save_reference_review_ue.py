"""Persist the intended default reference-pose review without a retained animation."""
import unreal,json,time,math,traceback
from pathlib import Path
O=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004/UE_Delivery_v1')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actor=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='CRM_B_v4');comp=actor.get_component_by_class(unreal.SkeletalMeshComponent)
comp.set_animation_mode(unreal.AnimationMode.ANIMATION_BLUEPRINT);comp.override_animation_data(None,False,False,0.,0.);comp.set_update_animation_in_editor(True)
started=time.monotonic()
def tick(dt):
    if time.monotonic()-started<1.:return
    unreal.unregister_slate_post_tick_callback(handle)
    try:
        bones=unreal.SkeletonService.list_bones('/Game/GuLiStrike/Mechs/ControlRigMech/Meshes/SKM_ControlRigMech');global_pose={};errors=[]
        for bone in bones:
            name=str(bone.bone_name);parent=str(bone.parent_bone_name)
            expected=unreal.MathLibrary.compose_transforms(bone.local_transform,global_pose[parent]) if parent in global_pose else bone.local_transform
            global_pose[name]=expected;actual=comp.get_socket_transform(name,unreal.RelativeTransformSpace.RTS_COMPONENT)
            errors.append((actual.translation-expected.translation).length())
        assert len(errors)==152 and max(errors)<.001,max(errors)
        comp.set_update_animation_in_editor(False)
        assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
        report={'success':True,'bone_count':len(errors),'max_component_reference_position_delta_cm':max(errors),'map_saved':True,'animation_data_cleared':True,'actor_scale':list(actor.get_actor_scale3d().to_tuple())}
    except Exception:report={'success':False,'error':traceback.format_exc()}
    (O/'reference_review_saved.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
handle=unreal.register_slate_post_tick_callback(tick)
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'reference_review_save_started':True}))
