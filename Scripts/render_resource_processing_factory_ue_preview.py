"""Capture RPF presentation frames in an already open, rendered Editor.

Run with runpy.run_path(...)["start"](). This captures real engine frames and
does not save pose or visibility overrides into the asset or demonstration map.
"""
from pathlib import Path
import json
import traceback
import unreal

OUT=Path('D:/UE5.7/test1/outputs/resource-processing-factory-20260909')


def start(video=True):
    subsystem=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors=subsystem.get_all_level_actors()
    subject=next(a for a in actors if a.get_actor_label()=='RPF_CLOSED')
    camera=next(a for a in actors if a.get_actor_label()=='RPF_HeroCamera')
    door=subject.get_component_by_class(unreal.SkeletalMeshComponent)
    door.set_update_animation_in_editor(True)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(world,'r.ScreenPercentage 100')
    for actor in actors:
        if actor.get_actor_label() in ['RPF_ANIMATED','RPF_OPEN'] or actor.get_class()==unreal.TextRenderActor.static_class():actor.set_is_temporarily_hidden_in_editor(True)
    specs=[
        ('UE_01_Closed.png',0,(6600,-7100,5800),(0,0,900),55),
        ('UE_02_Open.png',3,(6600,-7100,5800),(0,0,900),55),
        ('UE_03_Front.png',3,(10500,0,2300),(0,0,900),50),
        ('UE_04_Top.png',0,(2200,-2600,13000),(0,0,400),48),
        ('UE_05_Rear.png',0,(-6800,-7500,5300),(0,0,900),55),
        ('UE_06_Interior.png',3,(1420,0,700),(-1900,0,700),85),
        ('UE_07_HalfOpen.png',1.5,(6600,-7100,5800),(0,0,900),55),
    ]
    if video:
        (OUT/'UE_frames').mkdir(exist_ok=True)
        for i in range(90):
            t=i/10
            position=0 if t<=1 else t-1 if t<4 else 3 if t<=5 else 8-t if t<8 else 0
            specs.append(('UE_frames/frame_%04d.png'%i,position,(7400,-7000,4400),(0,0,900),55))
    original_transform=camera.get_actor_transform()
    original_fov=camera.get_component_by_class(unreal.CameraComponent).field_of_view
    state={'index':0,'task':None,'warm':0.0,'configured':False,'captured':[]}
    def restore():
        door.set_position(0,False);door.set_update_animation_in_editor(False)
        camera.set_actor_transform(original_transform,False,False)
        camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(original_fov)
        for actor in actors:actor.set_is_temporarily_hidden_in_editor(False)
    def tick(delta):
        try:
            if state['task'] is not None:
                if not state['task'].is_task_done():return
                if not (OUT/specs[state['index']][0]).exists():raise RuntimeError('Missing screenshot '+specs[state['index']][0])
                state['captured'].append(specs[state['index']][0]);state['index']+=1;state['task']=None;state['configured']=False
            if state['index']==len(specs):
                unreal.unregister_slate_post_tick_callback(handle);restore()
                (OUT/'ue_capture_report.json').write_text(json.dumps({'complete':True,'files':state['captured'],'video_fps':10,'video_seconds':9},indent=2),encoding='utf-8')
                return
            filename,position,location,target,fov=specs[state['index']]
            if not state['configured']:
                offset=subject.get_actor_location()
                camera.set_actor_location(unreal.Vector(*location)+offset,False,False)
                camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(),unreal.Vector(*target)+offset),False)
                camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(fov)
                door.set_position(position,False)
                state['warm']=.15 if filename.startswith('UE_frames') else 1.0
                state['configured']=True
                return
            state['warm']-=delta
            if state['warm']>0:return
            width,height=(1280,720) if filename.startswith('UE_frames') else (2400,1350)
            state['task']=unreal.AutomationLibrary.take_high_res_screenshot(width,height,str(OUT/filename),camera)
        except Exception:
            unreal.unregister_slate_post_tick_callback(handle);restore()
            (OUT/'ue_capture_error.txt').write_text(traceback.format_exc(),encoding='utf-8')
    handle=unreal.register_slate_post_tick_callback(tick)
    return {'frames_requested':len(specs),'callback':str(handle)}
