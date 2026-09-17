"""Frame-stepped art review of approved explosions, using transient actors only."""
import unreal,json,traceback
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/StylePass_20260917/FXFrames')
VARIANTS={'ground':'/Game/GuLiStrike/FX/CombatExplosions/NS_GroundDestruction_03',
          'air':'/Game/GuLiStrike/FX/CombatExplosions/NS_WingmanDestruction_05',
          'bomb':'/Game/GuLiStrike/FX/CombatExplosions/NS_WingmanBombardment_01',
          'bomb_source':'/Game/_VFXResources/Niagara_System/NS_Explosion_01'}
QUEUE=list(globals().get('REFERENCE_PREVIEW_QUEUE',[('ground',1.,'near'),('air',1.,'near'),('bomb',1.,'near'),('bomb_source',1.,'near')]))
EDITOR=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
OLD_CAMERA=EDITOR.get_level_viewport_camera_info()

def begin(variant,area,view):
    assert not unreal.WidgetService.is_pie_running()
    world=EDITOR.get_editor_world();actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    origin=unreal.Vector(0,0,80000);state={'frame':-30,'actors':[],'frames':[],'variant':variant,'area':area,'view':view}
    def spawn(cls,loc):
        a=actors.spawn_actor_from_class(cls,loc,unreal.Rotator(),transient=True);state['actors'].append(a);return a
    height=1000*area if variant=='air' else 0
    actor=spawn(unreal.NiagaraActor,origin+unreal.Vector(0,0,height));c=actor.niagara_component;c.set_auto_activate(False)
    c.set_asset(unreal.load_asset(VARIANTS[variant]),True);c.set_variable_float('User.Area_Scale',area);c.set_force_solo(True);c.set_random_seed_offset(1337);c.set_cast_shadow(False);c.set_can_render_while_seeking(True);c.set_component_tick_enabled(False)
    floor=spawn(unreal.StaticMeshActor,origin-unreal.Vector(0,0,30));floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'));floor.set_actor_scale3d(unreal.Vector(2000,2000,.5));floor.static_mesh_component.set_cast_shadow(False)
    floor.static_mesh_component.set_material(0,unreal.load_asset('/Game/Commander/Units/Tactical/Preview/Sweeper/M_ReviewFloor'))
    if view=='slope':floor.set_actor_rotation(unreal.Rotator(pitch=18),False);actor.set_actor_rotation(unreal.Rotator(pitch=18),False)
    capture=spawn(unreal.SceneCapture2D,origin);cap=capture.capture_component2d
    look=origin+unreal.Vector(0,0,350*area+height)
    offset=unreal.Vector(0,-3000,2300)*area*(2.6 if view=='far' else 1)
    pos=look+offset;rot=unreal.MathLibrary.find_look_at_rotation(pos,look);capture.set_actor_location_and_rotation(pos,rot,False,True);EDITOR.set_level_viewport_camera_info(pos,rot)
    for k,v in [('capture_every_frame',False),('capture_on_movement',False),('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR),('fov_angle',45.)]:cap.set_editor_property(k,v)
    pp=cap.get_editor_property('post_process_settings')
    for k,v in [('override_auto_exposure_method',True),('auto_exposure_method',unreal.AutoExposureMethod.AEM_MANUAL),('override_auto_exposure_bias',True),('auto_exposure_bias',0.),('override_auto_exposure_apply_physical_camera_exposure',True),('auto_exposure_apply_physical_camera_exposure',False),('override_motion_blur_amount',True),('motion_blur_amount',0.)]:pp.set_editor_property(k,v)
    cap.set_editor_property('post_process_settings',pp);cap.set_editor_property('post_process_blend_weight',1.)
    rt=unreal.RenderingLibrary.create_render_target2d(world,1280,960,unreal.TextureRenderTargetFormat.RTF_RGBA8,unreal.LinearColor(0,0,0,1),False,False);cap.set_editor_property('texture_target',rt)
    cap.set_editor_property('primitive_render_mode',unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
    for a in [actor,floor]:cap.show_only_actor_components(a)
    samples={0:'empty',3:'start',12:'peak',27:'expansion',72:'smoke',180:'tail',315:'end'}
    folder=OUT/(variant+'_'+view+'_x'+str(area));folder.mkdir(parents=True,exist_ok=True)
    def finish(error=None):
        unreal.unregister_slate_post_tick_callback(state['handle'])
        for a in reversed(state['actors']):actors.destroy_actor(a)
        report={k:v for k,v in state.items() if k not in ['actors','handle']};report['success']=not error
        if error:report['error']=error
        (folder/'capture.json').write_text(json.dumps(report,indent=2),encoding='utf8')
        if QUEUE and not error:globals()['REFERENCE_PREVIEW']=begin(*QUEUE.pop(0))
        else:EDITOR.set_level_viewport_camera_info(*OLD_CAMERA)
    def tick(dt):
        try:
            f=state['frame']
            if f==0:c.reinitialize_system();c.activate(True);c.set_paused(False);c.set_component_tick_enabled(False)
            if f>0:c.advance_simulation(1,1/60)
            if f-1 in samples:
                name=samples[f-1];unreal.RenderingLibrary.export_render_target(world,rt,str(folder),name+'.png');state['frames'].append({'name':name,'time':(f-1)/60,'path':str(folder/(name+'.png'))})
            cap.capture_scene();state['frame']+=1
            if f>max(samples)+1:finish()
        except Exception:finish(traceback.format_exc())
    state['handle']=unreal.register_slate_post_tick_callback(tick)
    return state

REFERENCE_PREVIEW=begin(*QUEUE.pop(0))
unreal.MCPythonHelper.submit_result(json.dumps({'started':True,'remaining':len(QUEUE)}))
