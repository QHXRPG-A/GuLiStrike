"""Isolated editor art preview; no PIE, gameplay, fixture or production references.

GPU particles advance once per rendered editor frame. The illustrative paths only
review smoke/flame appearance; gameplay trajectory verification remains separate.
"""
import json, traceback, sys
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'ArtSource/FX/WM01MissileCluster/Candidate_v3'
sys.path.insert(0,str(ROOT/'Scripts'))
from wm01_missile_visual_config import read_profile

def preview(quality='Full', distance_scale=1.):
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    old_camera=editor.get_level_viewport_camera_info()
    state={'actors':[],'frame':-120,'frames':[],'complete':False,'quality':quality,'ready_wait_frames':0}
    state['visual_revision']=3
    profile, visual_parameters=read_profile(unreal)
    speed_ratio=profile['SpeedCentimetersPerSecond']/1200.
    state['table_profile']=profile
    state['user_parameters']=visual_parameters
    state['capture_conditions']={'resolution':[1280,720],'fov':42,'manual_exposure_bias':0,
        'physical_camera_exposure':False,'camera_location':[100,-3900,2400],
        'camera_target':[1000,0,400],'distance_scale':distance_scale,'flight_seconds':2.4/speed_ratio,'tail_seconds':2.4,
        'comparison':'Same camera, exposure and four illustrative trajectories as Candidate_v2; table-driven 2x speed; no PIE.'}
    origin=unreal.Vector(0,0,0)
    a=unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.NiagaraActor,origin,transient=True)
    state['actors'].append(a)
    c=a.niagara_component
    c.set_auto_activate(False)
    system=unreal.load_asset('/Game/GuLiStrike/FX/WM01Missiles/NS_WM01MissileCluster_Preview'+quality)
    c.set_asset(system,True)
    c.set_force_solo(True);c.set_cast_shadow(False);c.set_can_render_while_seeking(True);c.set_component_tick_enabled(False)
    c.set_system_fixed_bounds(unreal.Box(unreal.Vector(-600,-1400,-1000),unreal.Vector(3200,1400,2000)))
    for name in ['Body','Flame']+(['History'] if quality!='Minimal' else []):
        c.set_emitter_fixed_bounds(name,unreal.Box(unreal.Vector(-600,-1400,-1000),unreal.Vector(3200,1400,2000)))
    for k,v in [('MissileHeads',1.),('MissileEmitHistory',1.),('MissileTrailWeight',1.),('MissileTime',0.)]: c.set_variable_float('User.'+k,v)
    for name,value in visual_parameters.items(): c.set_variable_float(name,value)
    c.set_variable_float('User.MissilePreviewSpeedRatio',speed_ratio)
    c.set_variable_int('User.MissileSlotCount',4)
    c.set_variable_int('User.MissileHistoryCount',4*({'Full':24,'Lite':8,'Minimal':0}[quality]))
    ground=unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.StaticMeshActor,origin-unreal.Vector(0,0,150),transient=True)
    state['actors'].append(ground)
    ground.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    ground.set_actor_scale3d(unreal.Vector(90,90,1))
    m=ground.static_mesh_component.create_dynamic_material_instance(0,unreal.load_asset('/Engine/BasicShapes/BasicShapeMaterial'))
    m.set_vector_parameter_value('Color',unreal.LinearColor(.055,.10,.15,1))
    ca=unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.SceneCapture2D,origin,transient=True)
    state['actors'].append(ca)
    target=origin+unreal.Vector(1000,0,400);location=target+unreal.Vector(-900,-3900,2000)*distance_scale
    state['capture_conditions']['camera_location']=list(location.to_tuple())
    ca.set_actor_location_and_rotation(location,unreal.MathLibrary.find_look_at_rotation(location,target),False,True)
    editor.set_level_viewport_camera_info(location,ca.get_actor_rotation())
    cap=ca.capture_component2d
    cap.set_editor_property('capture_every_frame',False);cap.set_editor_property('capture_on_movement',False)
    cap.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    cap.set_editor_property('fov_angle',42.)
    cap.set_editor_property('show_flag_settings',[unreal.EngineShowFlagsSetting(show_flag_name=n,enabled=True) for n in ['Particles','Niagara','Translucency']])
    pp=cap.get_editor_property('post_process_settings')
    for k,v in [('override_auto_exposure_method',True),('auto_exposure_method',unreal.AutoExposureMethod.AEM_MANUAL),
                ('override_auto_exposure_bias',True),('auto_exposure_bias',0.),('override_auto_exposure_apply_physical_camera_exposure',True),('auto_exposure_apply_physical_camera_exposure',False)]: pp.set_editor_property(k,v)
    cap.set_editor_property('post_process_settings',pp);cap.set_editor_property('post_process_blend_weight',1.)
    rt=unreal.RenderingLibrary.create_render_target2d(world,1280,720,unreal.TextureRenderTargetFormat.RTF_RGBA8,unreal.LinearColor(0,0,0,1),False,False)
    cap.set_editor_property('texture_target',rt)
    cap.set_editor_property('primitive_render_mode',unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
    cap.show_only_actor_components(a);cap.show_only_actor_components(ground)
    folder=OUT/(quality if distance_scale==1. else quality+'_Far');folder.mkdir(parents=True,exist_ok=True)
    # Same spatial phases at double speed; include late decay and a 5.1 s clear frame.
    samples={9:'start',30:'flight',56:'long_tail',69:'peak_tail',84:'impact',114:'dissipating',180:'late_tail',306:'cleared'}
    def finish(error=None):
        unreal.unregister_slate_post_tick_callback(state['handle'])
        for actor in reversed(state['actors']): unreal.EditorLevelLibrary.destroy_actor(actor)
        state['complete']=True
        editor.set_level_viewport_camera_info(*old_camera)
        report={k:v for k,v in state.items() if k not in ['actors','handle']}
        report.update(success=not error,error=error,scope='editor art preview; illustrative paths; no PIE or gameplay/performance validation')
        (folder/'capture.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    def tick(dt):
        try:
            # The preview copies generate their illustrative path from System.Age.
            # Array setters reinitialize active components in an editor world.
            f=state['frame']
            if f==0:
                # Editor load/override changes can queue a compile after the asset
                # readback. Keep simulation time at zero until its GPU data is ready.
                diag=unreal.GuLiCombatEffectAuthoringLibrary.get_war_machine_hover_compile_diagnostics(system)
                ready='System valid=1 ready=1 gpu=1' in diag and diag.count('GPU finished=1 complete=1')==(2 if quality=='Minimal' else 3)
                if not ready:
                    state['ready_wait_frames']+=1
                    if state['ready_wait_frames']>600: raise RuntimeError('Preview shader readiness timeout: '+diag)
                    return
                c.reinitialize_system();c.activate(True);c.set_rendering_enabled(True);c.set_component_tick_enabled(False)
            if f>0:
                c.advance_simulation(1,1/60)
                assert c.is_active(), 'Preview stopped before its persistent particle slots could be read.'
            if f in (65,306):
                cache=unreal.NiagaraSimCacheFunctionLibrary.create_niagara_sim_cache(world)
                params=unreal.NiagaraSimCacheCreateParameters()
                params.set_editor_property('attribute_capture_mode',unreal.NiagaraSimCacheAttributeCaptureMode.ALL)
                captured=unreal.NiagaraSimCacheFunctionLibrary.capture_niagara_sim_cache_immediate(cache,params,c,False)
                snapshot_key='gpu_snapshot' if f==65 else 'cleared_snapshot'
                state[snapshot_key]={'captured':bool(captured),'seconds':f/60,'active':c.is_active(),'visible':c.is_visible(),
                    'bounds':str(unreal.SystemLibrary.get_component_bounds(c)),'emitters':[]}
                if captured:
                    for emitter in cache.get_emitter_names():
                        positions=cache.read_position_attribute('Position',emitter)
                        colors=cache.read_color_attribute('Color',emitter)
                        slots=cache.read_int_attribute('LaserSlot',emitter)
                        sizes_gpu=cache.read_vector2_attribute('SpriteSize',emitter)
                        visible=sum(col.a>0 and size.x>0 and size.y>0 for col,size in zip(colors,sizes_gpu))
                        if str(emitter)=='Body':
                            scales_gpu=cache.read_vector_attribute('Scale',emitter)
                            visible=sum(col.a>0 and scale.x>0 for col,scale in zip(colors,scales_gpu))
                        if str(emitter)=='History' and f==65:
                            born=cache.read_float_attribute('HistoryBorn',emitter)
                            generation=cache.read_float_attribute('HistoryGeneration',emitter)
                            seen_time=cache.read_float_attribute('HistoryTime',emitter)
                            lanes_gpu=cache.read_int_attribute('MissileLane',emitter)
                            states_gpu=cache.read_float_attribute('HistoryState',emitter)
                            buckets_gpu=cache.read_int_attribute('HistoryBucket',emitter)
                            samples_gpu=cache.read_position_attribute('HistorySample',emitter)
                            state['history_samples']=[{'slot':int(slots[j]),'lane':int(lanes_gpu[j]),'born':float(born[j]),
                                'generation':float(generation[j]),'state':float(states_gpu[j]),'bucket':int(buckets_gpu[j]),'time':float(seen_time[j]),'sample':list(samples_gpu[j].to_tuple()),
                                'color':list(colors[j].to_tuple()),'alpha':float(colors[j].a),'size':list(sizes_gpu[j].to_tuple())} for j in range(len(slots)) if slots[j]==0][:24]
                        state[snapshot_key]['emitters'].append({'name':str(emitter),'count':len(positions),'slots':list(slots[:8]),
                            'visible_count':visible,
                            'sizes':[list(v.to_tuple()) for v in sizes_gpu[:4]],
                            'positions':[list(v.to_tuple()) for v in positions[:4]],'colors':[list(v.to_tuple()) for v in colors[:4]]})
            if f-1 in samples:
                name=samples[f-1]
                unreal.RenderingLibrary.export_render_target(world,rt,str(folder),name+'.png')
                state['frames'].append({'frame':f-1,'seconds':(f-1)/60,'path':str(folder/(name+'.png'))})
            cap.capture_scene()
            state['frame']+=1
            if f>308:
                live=state['gpu_snapshot'];clear=state['cleared_snapshot']
                assert live['captured'] and clear['captured'], 'GPU snapshot unavailable; images cannot establish lifecycle.'
                counts={e['name']:e for e in live['emitters']}
                assert counts['Body']['count']==4 and counts['Body']['visible_count']==4
                assert counts['Flame']['count']==4 and counts['Flame']['visible_count']==4
                if quality!='Minimal':
                    assert counts['History']['count']==4*({'Full':24,'Lite':8}[quality]) and counts['History']['visible_count']>0
                assert all(e['visible_count']==0 for e in clear['emitters']), 'Visible particles remain at 5.1 s.'
                finish()
        except Exception: finish(traceback.format_exc())
    state['handle']=unreal.register_slate_post_tick_callback(tick)
    return state

WM01_SMOKE_PREVIEW=preview(globals().get('WM01_PREVIEW_QUALITY','Full'),globals().get('WM01_PREVIEW_DISTANCE_SCALE',1.))
unreal.MCPythonHelper.submit_result(json.dumps({'started':True,'scope':'isolated editor art preview','output':str(OUT)}))
