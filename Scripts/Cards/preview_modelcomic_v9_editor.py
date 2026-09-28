"""RETIRED unsafe preview experiment, preserved only for incident analysis.

Editing target_gamma on a 2100 x 900 render target triggered UE's >2048-size
modal warning inside a Slate callback. Callback re-entry repeated native
window creation until Windows error 87 terminated the editor on 2026-09-28.
Only two partial previews were exported; they are not visual QA evidence.
See ModelComic_v9/Production/Inspection/preview-incident.json.
"""
raise RuntimeError('Retired unsafe preview experiment. Use readback_modelcomic_v9.py; user reviews the saved map.')
import unreal
import json
import math
import time
import traceback
from pathlib import Path

ROOT='/Game/GuLiStrike/Cards/WarMachineTarot'
MAP=ROOT+'/Maps/LVL_WarMachineTarotReview'
OUT=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/ModelComic_v9/Production/Previews')
OUT.mkdir(parents=True,exist_ok=True)
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
WORLD=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert WORLD.get_path_name().startswith(MAP+'.')
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for old in list(ACTORS.get_all_level_actors()):
    if old.get_actor_label().startswith('Temp_ModelComicV9_'):ACTORS.destroy_actor(old)
DIRECTOR=next(a for a in ACTORS.get_all_level_actors() if a.get_actor_label()=='WarMachine_CardDirector')
CAMERA=DIRECTOR.get_component_by_class(unreal.CameraComponent)
SPAWNED=[]
CARDS=[]
NAMES=['FireRate','MissileDamage','HighSpeed']
R={'success':False,'renderer':'UE editor SceneCapture, actual card blueprint meshes and materials',
   'pie_run':False,'assistant_visual_review':'not_performed_per_user_request',
   'text_preview':'UMG is runtime-created; static previews show artwork/frame only. FText table and row bindings read back separately.',
   'images':[],'poses':[]}

def spawn(cls,label,loc):
    a=ACTORS.spawn_actor_from_class(cls,loc)
    assert a,label
    a.set_actor_label('Temp_ModelComicV9_'+label)
    SPAWNED.append(a)
    return a

cardcls=unreal.load_asset('/Game/GuLiStrike/Cards/RevealDemo/Blueprints/BP_ParallaxRevealCard').generated_class()
for i,name in enumerate(NAMES):
    card=spawn(cardcls,name,unreal.Vector())
    card.call_method('SetArtwork',(DIRECTOR.get_editor_property('CardFrontMaterials')[i],DIRECTOR.get_editor_property('CardTextMaterials')[i],True))
    card.call_method('SetPresentationSize',(2.,2.,True))
    flash=card.get_editor_property('ConfirmationFlash')
    flash.set_visibility(False,True)
    CARDS.append(card)

CAP_ACTOR=spawn(unreal.SceneCapture2D,'Capture',CAMERA.get_world_location())
CAP=CAP_ACTOR.get_component_by_class(unreal.SceneCaptureComponent2D)
CAP.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
CAP.set_editor_property('capture_every_frame',False)
CAP.set_editor_property('always_persist_rendering_state',True)
CAP.set_editor_property('primitive_render_mode',unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
CAP.set_editor_property('post_process_settings',CAMERA.post_process_settings)

def pose(aspect,hx,hy):
    viewwidth=2.*140.*math.tan(math.radians(25.))
    viewheight=viewwidth/aspect
    scale=min(viewheight*.5/48.617,viewwidth*.24/32.175)*math.sqrt(2.)
    for i,c in enumerate(CARDS):
        x=(i-1)*viewwidth*.3
        c.set_actor_location(unreal.Vector(x,0,0),False,False)
        c.set_actor_scale3d(unreal.Vector(scale,scale,scale))
        c.get_editor_property('HoverPivot').set_editor_property('relative_rotation',unreal.Rotator(roll=hy*16.,yaw=-hx*16.))
        c.get_editor_property('FlipPivot').set_editor_property('relative_rotation',unreal.Rotator())
        loc=c.get_actor_location()
        hover=c.get_editor_property('HoverPivot').get_relative_transform().rotation.rotator()
        assert abs(loc.x-x)<.001 and abs(loc.y)<.001 and abs(loc.z)<.001
        assert abs(hover.yaw+hx*16.)<.001 and abs(hover.roll-hy*16.)<.001
    R['poses'].append({'aspect':aspect,'hover':[hx,hy],'center_drift_cm':0.,'scale':scale,
        'x_positions':[c.get_actor_location().x for c in CARDS],
        'hover_rotator':str(CARDS[0].get_editor_property('HoverPivot').get_relative_transform().rotation.rotator())})
    return scale

JOBS=[]
for tag,w,h in [('16x9',1920,1080),('16x10',1920,1200),('21x9',2100,900)]:
    JOBS.append({'name':'three-cards-'+tag,'size':[w,h],'aspect':w/h,'hover':[0.,0.],'card':None})
for i,name in enumerate(NAMES):
    for tag,hx,hy in [('center',0.,0.),('top-right',1.,-1.),('bottom-left',-1.,1.)]:
        JOBS.append({'name':name+'-'+tag,'size':[1024,1536],'aspect':16./9.,'hover':[hx,hy],'card':i})
STATE={'i':0,'phase':0,'wait':2.,'start':time.monotonic()}
TARGETS=[]

def cleanup():
    unreal.unregister_slate_post_tick_callback(HANDLE)
    for a in reversed(SPAWNED):ACTORS.destroy_actor(a)
    R['temporary_actor_count_after']=len([a for a in ACTORS.get_all_level_actors() if a.get_actor_label().startswith('Temp_ModelComicV9_')])
    assert R['temporary_actor_count_after']==0
    R['map_saved_after_cleanup']=unreal.EditorLoadingAndSavingUtils.save_map(WORLD,MAP)
    (OUT/'preview-manifest.json').write_text(json.dumps(R,ensure_ascii=False,indent=2),encoding='utf-8')

def tick(delta):
    try:
        STATE['wait']-=delta
        if STATE['wait']>0:return
        if time.monotonic()-STATE['start']>150:raise RuntimeError('Preview timed out')
        if STATE['i']>=len(JOBS):R['success']=True;cleanup();return
        job=JOBS[STATE['i']]
        if STATE['phase']==0:
            scale=pose(job['aspect'],*job['hover'])
            CAP.clear_show_only_components()
            loc=CAMERA.get_world_location()
            if job['card'] is None:
                for c in CARDS:CAP.show_only_actor_components(c,False)
                CAP.show_only_component(DIRECTOR.get_editor_property('Backdrop'))
                rot=CAMERA.get_world_rotation();fov=50.
            else:
                c=CARDS[job['card']]
                CAP.show_only_actor_components(c,False)
                rot=unreal.MathLibrary.find_look_at_rotation(loc,c.get_actor_location())
                fov=math.degrees(2.*math.atan(18.5*scale/(loc-c.get_actor_location()).length()))
            CAP_ACTOR.set_actor_location(loc,False,False)
            CAP_ACTOR.set_actor_rotation(rot,False)
            CAP.set_editor_property('fov_angle',fov)
            target=unreal.RenderingLibrary.create_render_target2d(WORLD,*job['size'],unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB,unreal.LinearColor(.012,.014,.019,1),False)
            target.set_editor_property('target_gamma',2.2)
            TARGETS.append(target)
            CAP.set_editor_property('texture_target',target)
            CAP.capture_scene()
            STATE.update(phase=1,wait=.55)
            return
        CAP.capture_scene()
        unreal.RenderingLibrary.export_render_target(WORLD,TARGETS[-1],str(OUT),job['name']+'.png')
        R['images'].append(dict(job,file=job['name']+'.png'))
        STATE.update(i=STATE['i']+1,phase=0,wait=.15)
    except Exception:
        R['error']=traceback.format_exc()
        cleanup()

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
HANDLE=unreal.register_slate_post_tick_callback(tick)
print(json.dumps({'started':True,'jobs':len(JOBS),'pie':False,'out':str(OUT)}))
