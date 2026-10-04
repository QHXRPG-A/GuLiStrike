"""Persist thirteen hover/turn samples in the existing Mass map.

The plain Actor/ISM is readable by the existing Editor binary. The new native
presentation module attaches its preview driver to this exact tag after compilation.
No existing army, spawn settings or other level actors are changed.
"""
import unreal,json,traceback
from pathlib import Path
ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/WarMachineTurn_20260930'
TAG='WarMachineHoverPreview20260929';PATH='/Game/Maps/LVL_CommanderMassPrototype'
api=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);lib=unreal.SubobjectDataBlueprintFunctionLibrary
r={'success':False,'map':PATH,'native_preview_run':False,'visual_acceptance':'pending','performance':'not_sampled'}
try:
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name()==PATH+'.LVL_CommanderMassPrototype'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    actor=next((a for a in api.get_all_level_actors() if TAG in [str(t) for t in a.tags]),None)
    if not actor:actor=api.spawn_actor_from_class(unreal.Actor,unreal.Vector(8000,-16000,0))
    actor.set_actor_label('WarMachineHover_PlayablePreview');actor.set_editor_property('tags',[TAG]);actor.set_editor_property('is_editor_only_actor',False)
    actor.set_folder_path('Review/WarMachineHover_20260929')
    c=actor.get_component_by_class(unreal.InstancedStaticMeshComponent)
    if not c:
        h,why=sub.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=sub.k2_gather_subobject_data_for_instance(actor)[0],new_class=unreal.InstancedStaticMeshComponent))
        c=lib.get_object(lib.get_data(h));assert c,str(why)
    c.set_static_mesh(unreal.load_asset('/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Rigid'))
    c.set_editor_property('num_custom_data_floats',51);c.clear_instances();c.set_cast_shadow(False)
    c.set_mobility(unreal.ComponentMobility.MOVABLE);c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    c.set_cull_distances(0,0);c.set_cull_distance(0);c.set_editor_property('never_distance_cull',True);c.set_editor_property('allow_cull_distance_volume',False)
    labels=['Idle','Idle + alternating recoil','Turn in place','720 cm/s','1440 cm/s','Start / stop','Phase / teleport / death / reuse',
            'Left 90','Right 90','180 and +/-179 wrap','Rapid reversal','Moving curve','Stable guns + unlocked pods']
    for i,label in enumerate(labels):
        idx=c.add_instance(unreal.Transform(location=unreal.Vector((i%3)*4400,(i//3)*2400,0),scale=unreal.Vector(.2,.2,.2)),False)
        pose=[0]*14;pose[11]=600
        data=[-1000]+pose+pose+[1 if i==12 else 0]*2+[0]*20
        for j,value in enumerate(data):assert c.set_custom_data_value(idx,j,value,j==50)
    note_label='WarMachineHover_PreviewInstructions'
    note=next((a for a in api.get_all_level_actors() if a.get_actor_label()==note_label),None)
    if not note:note=api.spawn_actor_from_class(unreal.Note,actor.get_actor_location()+unreal.Vector(0,-1000,0))
    note.set_actor_label(note_label);note.set_folder_path('Review/WarMachineHover_20260929');note.set_editor_property('is_editor_only_actor',True)
    note.set_editor_property('text','重防号转向预览：原点(8000,-16000,0)，3列共13例，保留前7项，新增左90/右90/180与跨角度边界/连续反向/移动弯道/转向稳定炮管与显隐导弹仓。需编译加载本次C++后自行PIE。旧模块不运行13样本驱动。示意不触发伤害；实战选原部队WM01，移动/停下攻击，并在解锁导弹仓后按Q，观察出膛与0.12s轨迹衔接。网络、死亡和传送应另走真实玩法验证。gs.Commander.HoverPreview 0关闭全部预览。')
    camera_label='WarMachineTurn_ReviewCamera'
    camera=next((a for a in api.get_all_level_actors() if a.get_actor_label()==camera_label),None)
    view=unreal.Vector(12400,-28500,16000);focus=unreal.Vector(12400,-11200,900)
    if not camera:camera=api.spawn_actor_from_class(unreal.CameraActor,view)
    camera.set_actor_location(view,False,False)
    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(view,focus),False)
    camera.set_actor_label(camera_label);camera.set_folder_path('Review/WarMachineHover_20260929')
    camera.set_editor_property('is_editor_only_actor',True)
    camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(60)
    # Upgrade the earlier editor-only review actors to the new data layout, preserving their poses.
    for a in api.get_all_level_actors():
        if 'MassRigidReview20260929' not in [str(t) for t in a.tags]:continue
        old=a.get_component_by_class(unreal.InstancedStaticMeshComponent)
        if not old or old.get_editor_property('num_custom_data_floats') not in [23,29,31]:continue
        old_count=old.get_editor_property('num_custom_data_floats')
        values=list(old.get_editor_property('per_instance_sm_custom_data'))
        old.set_editor_property('num_custom_data_floats',51)
        hover=600 if 'WarMachine' in a.get_actor_label() else 0
        for i in range(old.get_instance_count()):
            previous=values[i*old_count:(i+1)*old_count]
            data=previous[:12]+[hover,0,0]+previous[12:]+[hover,0,0] if old_count==23 else previous
            if len(data)==29:data += [1,1]  # Before pod slots existed, the shader default was visible.
            data += [0]*20
            for j,v in enumerate(data):assert old.set_custom_data_value(i,j,v,j==50)
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    r.update(success=True,actor=actor.get_path_name(),actor_location_cm=list(actor.get_actor_location().to_tuple()),samples=labels,
        instances=c.get_instance_count(),custom_floats=c.get_editor_property('num_custom_data_floats'),cull=list(c.get_cull_distances()),
        runtime_driver='UGuLiWarMachineHoverPreviewComponent (pending native compile)',tag=[str(t) for t in actor.tags],
        observation_camera=camera.get_path_name())
except:r['error']=traceback.format_exc()
(OUT/'scene-delivery.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(r,ensure_ascii=False))
