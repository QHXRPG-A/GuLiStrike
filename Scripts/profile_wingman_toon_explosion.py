"""Artist A/B sampling fixture; no automation tests or game configuration writes.

Run through ue_exec.py with WTE_BENCH_VARIANT ('old'/'toon'). The same 1280x960
offscreen game renderer, frame-stepped emission sequence and seed are used for
each case. CSV includes whole-frame GPU and Effects CPU scopes. ForceSolo is
explicit here to control old CPU/GPU particle age; runtime pooling is checked
separately at the real combat entry. Restore every temporary CVar on completion.
"""
import json
import math
import traceback
from pathlib import Path
import unreal

def start_profile(variant):
    assert not unreal.WidgetService.is_pie_running()
    out=Path('D:/UE5.7/test1/ArtSource/FX/WingmanGroundExplosion_Toon/performance')
    out.mkdir(parents=True,exist_ok=True)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    origin=unreal.Vector(0,0,80000)
    base='/Game/GuLiStrike/FX/WingmanWeapons/'
    specs=[(base+'NS_WingmanGroundExplosion_Big_17',5),(base+'NS_WingmanGroundShockwave_Big_17',3.1)] if variant=='old' else [(base+'StylizedExplosion/NS_WingmanGroundExplosion_Toon',5)]
    assets=[(unreal.load_asset(p),s) for p,s in specs]
    assert all(a for a,s in assets)
    state={'variant':variant,'frame':-300,'case_index':0,'components':[],'actors':[],'records':[],'complete':False,'released':0,'spawned':0}
    cases=[(r,n) for r in range(1,4) for n in (0,1,10,30)]
    settings={'t.MaxFPS':0,'r.VSync':0,'r.ScreenPercentage':100,'r.DynamicRes.OperationMode':0,'r.GPUCsvStatsEnabled':1,'Slate.bAllowThrottling':0}
    state['original_cvars']={k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in settings}
    def command(c):unreal.SystemLibrary.execute_console_command(world,c)
    for k,v in settings.items():command(k+' '+str(v))
    for category in ('Particles','Exclusive','RHI','NiagaraGpuCompute'):command('CsvCategory '+category+' enable')
    ground=actors.spawn_actor_from_class(unreal.StaticMeshActor,origin-unreal.Vector(0,0,60),transient=True);state['actors'].append(ground)
    ground.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'));ground.set_actor_scale3d(unreal.Vector(1600,1600,1))
    ground.static_mesh_component.set_cast_shadow(False)
    mat=ground.static_mesh_component.create_dynamic_material_instance(0,unreal.load_asset('/Engine/BasicShapes/BasicShapeMaterial'));mat.set_vector_parameter_value('Color',unreal.LinearColor(.025,.072,.105,1))
    camera=actors.spawn_actor_from_class(unreal.SceneCapture2D,origin,transient=True);state['actors'].append(camera)
    look=origin+unreal.Vector(0,0,700);loc=look+unreal.Vector(0,-19000,21000)
    camera.set_actor_location_and_rotation(loc,unreal.MathLibrary.find_look_at_rotation(loc,look),False,True)
    cap=camera.capture_component2d
    for k,v in [('capture_every_frame',False),('capture_on_movement',False),('fov_angle',45.0),('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR),('primitive_render_mode',unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)]:cap.set_editor_property(k,v)
    pp=cap.get_editor_property('post_process_settings')
    for k,v in [('override_auto_exposure_method',True),('auto_exposure_method',unreal.AutoExposureMethod.AEM_MANUAL),('override_auto_exposure_bias',True),('auto_exposure_bias',0.),('override_auto_exposure_apply_physical_camera_exposure',True),('auto_exposure_apply_physical_camera_exposure',False)]:pp.set_editor_property(k,v)
    cap.set_editor_property('post_process_settings',pp);cap.set_editor_property('post_process_blend_weight',1.0)
    rt=unreal.RenderingLibrary.create_render_target2d(world,1280,960,unreal.TextureRenderTargetFormat.RTF_RGBA8,unreal.LinearColor(0,0,0,1),False,False)
    cap.set_editor_property('texture_target',rt);cap.show_only_actor_components(ground)
    def spawn_hit(i,n,f):
        j=i%10;group=i//10
        pos=origin+unreal.Vector((j-4.5)*1333 if n>1 else 0,(group-1)*1700 if n>10 else 0,0)
        for asset,scale in assets:
            c=unreal.NiagaraFunctionLibrary.spawn_system_at_location(world,asset,pos,unreal.Rotator(yaw=(i*137.5)%360),unreal.Vector(scale,scale,scale),False,False,unreal.NCPoolMethod.MANUAL_RELEASE,False)
            c.set_cast_shadow(False);c.set_force_solo(True);c.set_random_seed_offset(1337+i)
            c.activate(True);c.set_component_tick_enabled(False)
            cap.show_only_component(c);state['components'].append((c,f+(180 if variant=='old' else 120)))
            state['spawned']+=1
    def release_all():
        for c,end in state['components']:cap.remove_show_only_component(c);c.release_to_pool();state['released']+=1
        state['components']=[]
    def finish(error=None):
        command('CsvProfile STOP')
        unreal.unregister_slate_post_tick_callback(state['handle'])
        release_all()
        for a in reversed(state['actors']):actors.destroy_actor(a)
        for k,v in state['original_cvars'].items():command(k+' '+str(v))
        state['complete']=True
        report={k:v for k,v in state.items() if k not in ('actors','components','handle')}
        report.update(success=not error,error=error,settings=settings,resolution=[1280,960],camera=str(loc),simulation_step=1/60,
                      frame_count=300,seed=1337,scope='Controlled editor SceneCapture, solo CPU/GPU simulations, manual pool release; no runtime gameplay timing inferred')
        (out/(variant+'-sampling.json')).write_text(json.dumps(report,indent=2),encoding='utf8')
    def tick(delta):
        try:
            f=state['frame']
            if f<0:
                if f==-280:
                    for i in range(30):spawn_hit(i,30,f)
            else:
                run,n=cases[state['case_index']]
                if f==0:
                    release_all()
                    name=f'{variant}-{n:02d}-r{run}.csv'
                    command('CsvProfile STARTFILE=../../../ArtSource/FX/WingmanGroundExplosion_Toon/performance/'+name)
                    command('CsvProfile START')
                    state['records'].append({'run':run,'hits':n,'csv':name})
                for i in range(n):
                    # Ten ground impacts span 0.9s; three lanes overlap by 0.1s.
                    if f==12+(i%10)*6+(i//10)*6:spawn_hit(i,n,f)
            keep=[]
            for c,end in state['components']:
                if f>=end:cap.remove_show_only_component(c);c.release_to_pool();state['released']+=1
                else:c.advance_simulation(1,1/60);keep.append((c,end))
            state['components']=keep
            cap.capture_scene()
            state['frame']+=1
            if f==299:
                command('CsvProfile STOP');state['frame']=-60;state['case_index']+=1
                if state['case_index']==len(cases):finish()
        except Exception:finish(traceback.format_exc())
    state['handle']=unreal.register_slate_post_tick_callback(tick)
    return state

WTE_BENCH=start_profile(globals().get('WTE_BENCH_VARIANT','old'))
unreal.MCPythonHelper.submit_result(json.dumps({'started':True,'variant':WTE_BENCH['variant']}))
