"""Render the right card from the real review-camera origin at pressure limits."""
import json
import math
import time
import traceback
from pathlib import Path
import unreal

LABEL=globals().get('EDGE_OUTPUT_LABEL','Before')
OUT=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/Comic_Closeups_v6/Production/EdgeCoverageFix')/LABEL
OUT.mkdir(parents=True,exist_ok=True)
assert unreal.WidgetService.is_pie_running()
P=next(p for p in unreal.ObjectIterator(unreal.PlayerController) if not p.get_name().startswith('Default__') and p.get_viewport_size()[0]>0)
D=P.get_editor_property('Director')
assert 'LVL_WarMachineTarotReview' in P.get_world().get_path_name()
D.set_actor_tick_enabled(False)
D.call_method('SetPhase',(6,))
D.call_method('StartPresentation')
D.call_method('TickPresentation',(1.5,))
R={'success':False,'phase':LABEL,'poses':[],'viewport':list(P.get_viewport_size()),'capture_origin':'same as review camera; perspective aim toward card center, no image crop'}
STATE={'wait':2.,'start':time.monotonic(),'index':0}
def pose(x,y):
    for i in range(3):
        c=D.get_editor_property('Card'+str(i))
        location=c.get_actor_location()
        c.call_method('ApplyFrame',(location.x,D.get_editor_property('CardScale'),1.,D.get_editor_property('FarDistance'),0.,x,y,16.,D.get_editor_property('HoverInterpSpeed'),1.,0.,D.get_editor_property('FlashIntensity'),True))
        assert (location-c.get_actor_location()).length()<.001
    R['poses'].append([x,y])
def closeup(name):
    c=D.get_editor_property('Card2')
    camera=D.get_component_by_class(unreal.CameraComponent)
    origin=camera.get_world_location()
    rotation=unreal.MathLibrary.find_look_at_rotation(origin,c.get_actor_location())
    transform=unreal.Transform(location=origin,rotation=rotation)
    gameplay=unreal.get_default_object(unreal.GameplayStatics)
    actor=gameplay.call_method('BeginDeferredActorSpawnFromClass',(P.get_world(),unreal.SceneCapture2D.static_class(),transform,unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
    try:
        gameplay.call_method('FinishSpawningActor',(actor,transform,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        capture=actor.get_component_by_class(unreal.SceneCaptureComponent2D)
        capture.set_editor_property('projection_type',unreal.CameraProjectionMode.PERSPECTIVE)
        capture.set_editor_property('fov_angle',math.degrees(2*math.atan(18.5*D.get_editor_property('CardScale')/(origin-c.get_actor_location()).length())))
        capture.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
        capture.set_editor_property('capture_every_frame',False)
        capture.set_editor_property('primitive_render_mode',unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
        capture.show_only_actor_components(c,False)
        capture.set_editor_property('post_process_settings',camera.post_process_settings)
        target=unreal.RenderingLibrary.create_render_target2d(P.get_world(),1024,1536,unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB,unreal.LinearColor(0,0,0,1),False)
        target.set_editor_property('target_gamma',2.2)
        capture.set_editor_property('texture_target',target)
        capture.capture_scene()
        unreal.RenderingLibrary.export_render_target(P.get_world(),target,str(OUT),name+'.png')
    finally:actor.destroy_actor()
def screenshot():
    unreal.SystemLibrary.execute_console_command(P.get_world(),'Shot showui filename="'+str(OUT/'three-cards.png')+'" -nosuffix',P)
JOBS=[]
poses=[(0,0,'center'),(1,0,'right'),(-1,0,'left'),(1,-1,'top-right'),(-1,-1,'top-left'),(1,1,'bottom-right'),(-1,1,'bottom-left')]
for x,y,name in poses:
    JOBS.extend([lambda x=x,y=y:pose(x,y),lambda name=name:closeup(name)])
JOBS.extend([lambda:pose(0,0),screenshot])
def finish():
    unreal.unregister_slate_post_tick_callback(HANDLE)
    (OUT/'preview.json').write_text(json.dumps(R,indent=2),encoding='utf-8')
def tick(delta):
    try:
        STATE['wait']-=delta
        if STATE['wait']>0:return
        if time.monotonic()-STATE['start']>90:raise RuntimeError('Edge preview timed out')
        if STATE['index']>=len(JOBS):
            R['success']=True
            finish();return
        JOBS[STATE['index']]()
        STATE['index']+=1
        STATE['wait']=.7
    except Exception:
        R['error']=traceback.format_exc();finish()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
HANDLE=unreal.register_slate_post_tick_callback(tick)
print(json.dumps({'started':LABEL}))
