"""Capture imported art in the unchanged Demo environment using transient actors."""
import unreal,json,time,traceback
from pathlib import Path

OUT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/UE_StyleSync_v9')
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert not level.is_in_play_in_editor()
world=editor.get_editor_world()
assert world.get_path_name().startswith('/Game/Maps/LVL_GroundMech_Demo.')
previous_camera=editor.get_level_viewport_camera_info()
previous_selection=actors.get_selected_level_actors()

floor=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(18000,-8000,100000),unreal.Vector(18000,-8000,-100000),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True).to_tuple()
assert floor[0] and not floor[1]
light=actors.spawn_actor_from_class(unreal.EditorAssetLibrary.load_blueprint_class('/Game/GuLiStrike/GroundMech/BP_GroundMech_Light'),floor[5]+unreal.Vector(0,0,374.1898),transient=True)
light.set_actor_label('StyleSync_Transient_LightPreview')
light.get_editor_property('upper_body_pivot').set_world_rotation(unreal.Rotator(yaw=-90),False,False)
light.mesh.set_update_animation_in_editor(True)
light.mesh.set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
spider=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='GroundMech_SpiderMech_Styled_Reference')
camera=actors.spawn_actor_from_class(unreal.CameraActor,unreal.Vector(),transient=True)
camera.set_actor_label('StyleSync_Transient_Camera')
cc=camera.get_component_by_class(unreal.CameraComponent)
cc.set_field_of_view(40);cc.set_editor_property('aspect_ratio',1.6)
cc.set_editor_property('post_process_blend_weight',0)
actors.set_selected_level_actors([])
unreal.ViewportService.set_game_view(True)
unreal.ViewportService.set_view_mode('lit')
unreal.ViewportService.set_realtime(True)

shots=[('UE_Light_Hero.png',light,(1450,-1750,1000),(0,0,0)),
       ('UE_Light_VentRear.png',light,(-1050,1050,950),(0,0,200)),
       ('UE_Spider_Hero.png',spider,(700,-950,550),(0,0,15)),
       ('UE_Spider_Front.png',spider,(1200,0,460),(0,0,15))]
state={'index':0,'task':None,'configured':False,'wait':0,'files':[],'started':time.monotonic()}
def cleanup_style_capture():
    actors.destroy_actor(light);actors.destroy_actor(camera)
    actors.set_selected_level_actors(previous_selection)
    editor.set_level_viewport_camera_info(*previous_camera)
def capture_style_tick(delta):
    try:
        if time.monotonic()-state['started']>150:raise RuntimeError('Style capture timeout')
        if state['task']:
            if not state['task'].is_task_done():return
            name=shots[state['index']][0]
            assert (OUT/name).is_file(),name
            state['files'].append(name);state.update(index=state['index']+1,task=None,configured=False)
        if state['index']==len(shots):
            unreal.unregister_slate_post_tick_callback(style_capture_handle)
            cleanup_style_capture()
            (OUT/'ue_capture.json').write_text(json.dumps({'success':True,'files':state['files'],'environment':'LVL_GroundMech_Demo: original lighting and exposure','transient_preview_actors_removed':True},indent=2),encoding='utf-8')
            return
        name,subject,offset,target_offset=shots[state['index']]
        if not state['configured']:
            target=subject.get_actor_location()+unreal.Vector(*target_offset)
            position=target+unreal.MathLibrary.quat_rotate_vector(subject.get_actor_rotation().quaternion(),unreal.Vector(*offset))
            rotation=unreal.MathLibrary.find_look_at_rotation(position,target)
            camera.set_actor_location(position,False,False);camera.set_actor_rotation(rotation,False)
            editor.set_level_viewport_camera_info(position,rotation)
            state.update(configured=True,wait=4.0);return
        state['wait']-=delta
        if state['wait']>0:return
        state['task']=unreal.AutomationLibrary.take_high_res_screenshot(1600,1000,str(OUT/name),camera,delay=1)
    except Exception:
        unreal.unregister_slate_post_tick_callback(style_capture_handle)
        cleanup_style_capture()
        (OUT/'ue_capture.json').write_text(json.dumps({'success':False,'error':traceback.format_exc()},indent=2),encoding='utf-8')
style_capture_handle=unreal.register_slate_post_tick_callback(capture_style_tick)
unreal.MCPythonHelper.submit_result(json.dumps({'running':True,'captures':[s[0] for s in shots]}))
