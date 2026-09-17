"""Production turntable/contact-sheet capture, run via ue_exec.py in a stopped editor.

Set WTE_VARIANT='old'/'toon', WTE_VIEW='near'/'far'/'bright'/'slope' in the bridge
before exec. Uses transient actors only and advances exactly 1/60 s per rendered
editor frame so GPU emitters also have a frame to execute. No level is saved.
"""
import json
import traceback
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/ArtSource/FX/WingmanGroundExplosion_Toon')
BASE = '/Game/GuLiStrike/FX/WingmanWeapons/'


def preview(variant='old', view='near'):
    assert not unreal.WidgetService.is_pie_running(), 'Stop PIE before capture'
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    state = {'actors': [], 'components': [], 'frames': [], 'frame': -12,
             'variant': variant, 'view': view, 'complete': False}
    origin = unreal.Vector(0, 0, 80000)
    specs = [(BASE+'NS_WingmanGroundExplosion_Big_17',5.0),
             (BASE+'NS_WingmanGroundShockwave_Big_17',3.1)] if variant == 'old' else [
             (BASE+'StylizedExplosion/NS_WingmanGroundExplosion_Toon',5.0)]
    for path, scale in specs:
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.NiagaraActor, origin, transient=True)
        state['actors'].append(actor)
        actor.set_actor_scale3d(unreal.Vector(scale,scale,scale))
        comp = actor.niagara_component
        comp.set_auto_activate(False)
        comp.set_asset(unreal.load_asset(path), reset_existing_override_parameters=True)
        comp.set_force_solo(True)
        comp.set_random_seed_offset(1337)
        comp.set_cast_shadow(False)
        comp.set_can_render_while_seeking(True)
        comp.set_paused(False)
        comp.set_component_tick_enabled(False)
        state['components'].append(comp)
    ground = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.StaticMeshActor,origin-unreal.Vector(0,0,60),transient=True)
    state['actors'].append(ground)
    ground.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    ground.set_actor_scale3d(unreal.Vector(1600,1600,1))
    if view == 'slope': ground.set_actor_rotation(unreal.Rotator(pitch=18),False)
    ground.static_mesh_component.set_cast_shadow(False)
    mat = ground.static_mesh_component.create_dynamic_material_instance(0,unreal.load_asset('/Engine/BasicShapes/BasicShapeMaterial'))
    mat.set_vector_parameter_value('Color',unreal.LinearColor(.62,.42,.23,1) if view=='bright' else unreal.LinearColor(.025,.072,.105,1))
    camera = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.SceneCapture2D, origin, transient=True)
    state['actors'].append(camera)
    look = origin+unreal.Vector(0,0,1800)
    offset = unreal.Vector(0,-23000,19000) if view != 'far' else unreal.Vector(0,-50000,66000)
    if view in ('detail','bright','slope'):
        look=origin+unreal.Vector(0,0,900)
        offset=unreal.Vector(0,-9000,8500)
    location = look+offset
    camera.set_actor_location_and_rotation(location,unreal.MathLibrary.find_look_at_rotation(location,look),False,True)
    cap = camera.capture_component2d
    cap.set_editor_property('capture_every_frame',False)
    cap.set_editor_property('capture_on_movement',False)
    cap.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    cap.set_editor_property('fov_angle',45.0)
    pp = cap.get_editor_property('post_process_settings')
    for k,v in [('override_auto_exposure_method',True),('auto_exposure_method',unreal.AutoExposureMethod.AEM_MANUAL),
                ('override_auto_exposure_bias',True),('auto_exposure_bias',0.0),
                ('override_auto_exposure_apply_physical_camera_exposure',True),('auto_exposure_apply_physical_camera_exposure',False)]:
        pp.set_editor_property(k,v)
    cap.set_editor_property('post_process_settings',pp)
    cap.set_editor_property('post_process_blend_weight',1.0)
    rt = unreal.RenderingLibrary.create_render_target2d(world,1280,960,unreal.TextureRenderTargetFormat.RTF_RGBA8,unreal.LinearColor(0,0,0,1),False,False)
    cap.set_editor_property('texture_target',rt)
    cap.set_editor_property('primitive_render_mode',unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
    for actor in state['actors'][:-1]: cap.show_only_actor_components(actor)
    samples = {0:'empty',3:'start',9:'peak',18:'spread',33:'smoke_start',66:'smoke',102:'tail',126:'end',186:'old_end'}
    folder = OUT/'frames'/f'{variant}_{view}'
    folder.mkdir(parents=True,exist_ok=True)
    state['camera']={'location':str(location),'rotation':str(camera.get_actor_rotation()),'resolution':[1280,960],'fov':45,'seed_offset':1337}

    def finish(error=None):
        unreal.unregister_slate_post_tick_callback(state['handle'])
        for actor in reversed(state['actors']): unreal.EditorLevelLibrary.destroy_actor(actor)
        state['complete']=True
        report={k:v for k,v in state.items() if k not in ['actors','components','handle']}
        report['success']=not error
        if error: report['error']=error
        (folder/'capture.json').write_text(json.dumps(report,indent=2),encoding='utf8')
        queue=globals().get('WTE_PREVIEW_QUEUE',[])
        if queue and not error:
            variant_next,view_next=queue.pop(0)
            globals()['WTE_PREVIEW']=preview(variant_next,view_next)

    def tick(delta):
        try:
            f=state['frame']
            if f==0:
                for comp in state['components']: comp.reinitialize_system(); comp.activate(True); comp.set_paused(False); comp.set_component_tick_enabled(False)
            if f>0:
                for comp in state['components']: comp.advance_simulation(1,1/60)
            # Queue capture one rendered frame before export, needed for GPU sims.
            if f-1 in samples:
                name=samples[f-1]
                unreal.RenderingLibrary.export_render_target(world,rt,str(folder),name+'.png')
                state['frames'].append({'name':name,'time':(f-1)/60,'path':str(folder/(name+'.png'))})
            cap.capture_scene()
            state['frame']+=1
            if f>max(samples)+1: finish()
        except Exception: finish(traceback.format_exc())
    state['handle']=unreal.register_slate_post_tick_callback(tick)
    return state


WTE_PREVIEW=preview(globals().get('WTE_VARIANT','old'),globals().get('WTE_VIEW','near'))
print(json.dumps({'started':True,'variant':WTE_PREVIEW['variant'],'view':WTE_PREVIEW['view']}))
