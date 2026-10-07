import unreal,json,gc,types
from pathlib import Path
O=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004/UE_Delivery_v1')
# Stop only this delivery's blocked viewport screenshot callback.
stopped=[]
for f in gc.get_objects():
    if isinstance(f,types.FunctionType) and f.__name__=='tick' and f.__code__.co_filename.endswith('preview_readback_ue.py'):
        env=dict(zip(f.__code__.co_freevars,[c.cell_contents for c in f.__closure__]))
        unreal.unregister_slate_post_tick_callback(env['handle']);stopped.append({k:v for k,v in env['state'].items() if k!='task'})
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
camera=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='CRM_ReviewCamera')
cc=camera.get_component_by_class(unreal.CameraComponent)
capture=actors.spawn_actor_from_class(unreal.SceneCapture2D,camera.get_actor_location(),camera.get_actor_rotation());capture.set_actor_label('CRM_CaptureProbe')
sc=capture.get_component_by_class(unreal.SceneCaptureComponent2D)
rt=unreal.RenderingLibrary.create_render_target2d(capture,2048,2048,unreal.TextureRenderTargetFormat.RTF_RGBA8)
for k,v in dict(texture_target=rt,capture_every_frame=False,capture_on_movement=False,capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR,projection_type=cc.get_editor_property('projection_mode'),ortho_width=cc.get_editor_property('ortho_width'),fov_angle=cc.get_editor_property('field_of_view')).items():sc.set_editor_property(k,v)
sc.capture_scene();unreal.RenderingLibrary.export_render_target(capture,rt,str(O/'Previews'),'UE_Probe.png')
unreal.MCPythonHelper.submit_result(json.dumps({'success':(O/'Previews/UE_Probe.png').is_file(),'stopped_callbacks':stopped,'probe_file':str(O/'Previews/UE_Probe.png')}))
