"""UE SceneCapture evidence of static ISM/WPO poses; never starts PIE."""
import unreal,json,math,time,traceback
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/MechanicalAnimation_20260929/Previews');OUT.mkdir(exist_ok=True)
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
WORLD=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
API=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
SUB=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);LIB=unreal.SubobjectDataBlueprintFunctionLibrary
ORIGIN=unreal.Vector(8000,8000,12000);SPAWNED=[]
def spawn(cls):
    a=API.spawn_actor_from_class(cls,ORIGIN,transient=True);SPAWNED.append(a);return a
BODY=spawn(unreal.Actor)
handles=SUB.k2_gather_subobject_data_for_instance(BODY)
h,why=SUB.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=handles[0],new_class=unreal.InstancedStaticMeshComponent))
ISM=LIB.get_object(LIB.get_data(h));assert ISM,str(why)
ISM.set_editor_property('num_custom_data_floats',29);ISM.set_cast_shadow(False);ISM.set_cull_distances(0,0)
BG=spawn(unreal.StaticMeshActor);BG.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Plane'))
BG.static_mesh_component.set_material(0,unreal.load_asset('/Game/Commander/Review/M_TacticalReview_Backdrop'))
BG.static_mesh_component.set_cast_shadow(False);BG.set_actor_scale3d(unreal.Vector(120,120,120))
CA=spawn(unreal.SceneCapture2D);CAP=CA.get_component_by_class(unreal.SceneCaptureComponent2D)
CAP.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
CAP.set_editor_property('capture_every_frame',False);CAP.set_editor_property('always_persist_rendering_state',True)
CAP.set_editor_property('primitive_render_mode',unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
CAP.show_only_actor_components(BODY,False);CAP.show_only_actor_components(BG,False)
pp=unreal.PostProcessSettings()
for k,v in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
 'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
 'override_auto_exposure_bias':True,'auto_exposure_bias':0.,'override_motion_blur_amount':True,'motion_blur_amount':0.,'override_bloom_intensity':True,'bloom_intensity':0.}.items():pp.set_editor_property(k,v)
CAP.set_editor_property('post_process_settings',pp)
RT=unreal.RenderingLibrary.create_render_target2d(WORLD,1280,1280,unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB,unreal.LinearColor(.12,.16,.20,1),False)
RT.set_editor_property('target_gamma',1);CAP.set_editor_property('texture_target',RT)
VIEWS=[('WarMachine_Neutral','WarMachine',[0]*11),('WarMachine_Aim_Recoil_Tilt','WarMachine',[.8,.35,.35,175,0,0,math.radians(15),0,0,0,0]),
       ('Sweeper_Neutral','Sweeper',[0]*11),('Sweeper_Pitch_Wheels','Sweeper',[0,math.radians(45),0,0,0,0,0,1.1,1.1,1.1,1.1])]
STATE={'i':0,'phase':0,'wait':1.,'start':time.monotonic()};REPORT={'success':False,'kind':'editor WPO pose captures; not native gameplay validation','images':[]}
def cleanup():
    unreal.unregister_slate_post_tick_callback(HANDLE)
    for a in reversed(SPAWNED):API.destroy_actor(a)
    (OUT/'capture-report.json').write_text(json.dumps(REPORT,indent=2),encoding='utf8')
def tick(dt):
    try:
        if time.monotonic()-STATE['start']>100:raise RuntimeError('Capture timeout')
        STATE['wait']-=dt
        if STATE['wait']>0:return
        if STATE['i']>=len(VIEWS):REPORT['success']=True;cleanup();return
        name,unit,pose=VIEWS[STATE['i']]
        pose=pose+[600 if unit=='WarMachine' else 0,0,0]
        if STATE['phase']==0:
            ISM.clear_instances();ISM.set_static_mesh(unreal.load_asset('/Game/Commander/Units/Tactical/Cel/'+unit+'/Meshes/SM_'+unit+'_Rigid'))
            ISM.set_editor_property('forced_lod_model',1)
            i=ISM.add_instance(unreal.Transform(scale=unreal.Vector(.2,.2,.2)),False)
            for j,v in enumerate([-1000]+pose+pose):ISM.set_custom_data_value(i,j,v,j==28)
            factor=1 if unit=='WarMachine' else .4
            pos=ORIGIN+unreal.Vector(1550,-1800,1250)*factor;aim=ORIGIN+unreal.Vector(0,0,360)*factor
            CA.set_actor_location(pos,False,False);CA.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(pos,aim),False);CAP.set_editor_property('fov_angle',43.)
            direction=(aim-pos).normal();BG.set_actor_location(aim+direction*1500,False,False);BG.set_actor_rotation(unreal.MathLibrary.make_rot_from_z(direction),False)
            CAP.capture_scene();STATE.update(phase=1,wait=.7);return
        CAP.capture_scene();unreal.RenderingLibrary.export_render_target(WORLD,RT,str(OUT),name+'.png')
        REPORT['images'].append({'file':name+'.png','pose':pose});STATE.update(i=STATE['i']+1,phase=0,wait=.2)
    except:REPORT['error']=traceback.format_exc();cleanup()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
HANDLE=unreal.register_slate_post_tick_callback(tick)
unreal.MCPythonHelper.submit_result(json.dumps({'started':True,'out':str(OUT)}))
