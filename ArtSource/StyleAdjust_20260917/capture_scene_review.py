"""Transient art review in the actual commander map; no map save."""
import unreal,json,traceback,time,re
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/StyleAdjust_20260917')
PHASE=globals().get('STYLE_REVIEW_PHASE','Before')
AS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
W=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert W.get_name()=='LVL_CommanderMassPrototype'
state={'success':False,'phase':PHASE,'actors':[],'frame':-45,'images':[],'started':time.monotonic()}
def spawn(cls,loc):
    a=AS.spawn_actor_from_class(cls,loc,unreal.Rotator(),transient=True);state['actors'].append(a);return a
start=unreal.Vector(-221397,130463,100000)
hit=unreal.SystemLibrary.line_trace_single(W,start,unreal.Vector(start.x,start.y,-100000),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,False,[],unreal.DrawDebugTrace.NONE)
assert hit
hit_text=hit.export_text()
assert 'bBlockingHit=True' in hit_text,hit_text
xyz=re.search(r'ImpactPoint=\(X=([^,]+),Y=([^,]+),Z=([^)]+)\)',hit_text)
origin=unreal.Vector(*(float(v) for v in xyz.groups()))+unreal.Vector(0,0,50)
state['origin']=list(origin.to_tuple())
for unit,offset,scale in [('Sweeper',(-700,-2500,0),1.0),('WarMachine',(-700,2200,0),0.34)]:
    a=spawn(unreal.StaticMeshActor,origin+unreal.Vector(*offset));a.set_actor_label('StyleReview_'+unit)
    a.static_mesh_component.set_static_mesh(unreal.load_asset('/Game/Commander/Units/Tactical/Cel/'+unit+'/Meshes/SM_'+unit+'_Cel'))
    a.set_actor_scale3d(unreal.Vector(scale,scale,scale));a.set_actor_rotation(unreal.Rotator(yaw=-40),False)
fx=spawn(unreal.NiagaraActor,origin+unreal.Vector(3800,0,0));c=fx.niagara_component
p='/Game/GuLiStrike/FX/CombatExplosions/'+('Rollback/NS_GroundDestruction_03_Before060' if PHASE=='Before' else 'NS_GroundDestruction_03')
c.set_auto_activate(False);c.set_asset(unreal.load_asset(p),True);c.set_variable_float('User.Area_Scale',3.)
c.set_force_solo(True);c.set_random_seed_offset(1337);c.set_cast_shadow(False);c.set_can_render_while_seeking(True);c.set_component_tick_enabled(False)
capture=spawn(unreal.SceneCapture2D,origin);cap=capture.capture_component2d
look=origin+unreal.Vector(1000,0,900);pos=look+unreal.Vector(8500,-12000,10500)
capture.set_actor_location_and_rotation(pos,unreal.MathLibrary.find_look_at_rotation(pos,look),False,True)
for k,v in [('capture_every_frame',False),('capture_on_movement',False),('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR),('fov_angle',42.),('post_process_blend_weight',0.),('always_persist_rendering_state',True)]:cap.set_editor_property(k,v)
rt=unreal.RenderingLibrary.create_render_target2d(W,1500,1000,unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB,unreal.LinearColor(0,0,0,1),False,False);cap.set_editor_property('texture_target',rt)
cam=spawn(unreal.CameraActor,pos);cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(pos,look),False)
cam.camera_component.set_field_of_view(42.);cam.camera_component.set_editor_property('post_process_blend_weight',0.)
samples={0:'models',12:'peak',33:'expansion',90:'smoke',240:'tail'}
def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    for a in reversed(state['actors']):AS.destroy_actor(a)
    state['success']=not error
    if error:state['error']=error
    r={k:v for k,v in state.items() if k not in ['actors','handle','shot_task']}
    (OUT/('scene_review_'+PHASE+'.json')).write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8')
def tick(dt):
    try:
        if time.monotonic()-state['started']>150:raise RuntimeError('Capture timeout')
        f=state['frame']
        if f==0:c.reinitialize_system();c.activate(True);c.set_paused(False);c.set_component_tick_enabled(False)
        if f>0:c.advance_simulation(1,1/60)
        if f-1 in samples:
            name=samples[f-1];filename=PHASE+'_'+name+'.png'
            unreal.RenderingLibrary.export_render_target(W,rt,str(OUT),filename)
            state['images'].append({'path':str(OUT/filename),'effect_time':(f-1)/60})
        if f in (0,90):
            state['shot_task']=unreal.AutomationLibrary.take_high_res_screenshot(1500,1000,str(OUT/(PHASE+'_Viewport_'+samples[f]+'.png')),camera=cam,delay=0.)
        cap.capture_scene();state['frame']+=1
        if f>max(samples)+1:finish()
    except Exception:finish(traceback.format_exc())
state['handle']=unreal.register_slate_post_tick_callback(tick)
STYLE_SCENE_REVIEW=state
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'started':PHASE,'origin':state['origin']}))
