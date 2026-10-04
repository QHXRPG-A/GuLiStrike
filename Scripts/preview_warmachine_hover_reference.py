"""Real GPU art preview. A GPU input generator avoids editor array-setter resets."""
import json, math, traceback
from pathlib import Path
import unreal
OUT=Path('D:/UE5.7/test1/ArtSource/WarMachineHover_20260930/PreviewClear5to15')
OUT.mkdir(parents=True,exist_ok=True)
PREFIX='/Game/GuLiStrike/FX/WarMachineHover/Reference_v2/NS_WarMachineHoverReference'
OFFSETS=[unreal.Vector(293.39724,326.25042,0),unreal.Vector(293.39724,-326.25042,0),unreal.Vector(-350.82282,380.42546,0),unreal.Vector(-350.82282,-380.42546,0)]

def start_reference_preview():
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    ed=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world=editor.get_editor_world();old_camera=editor.get_level_viewport_camera_info()
    for c in unreal.ObjectIterator(unreal.NiagaraComponent):
        if c.get_world()==world and c.get_asset() and c.get_asset().get_path_name().startswith(PREFIX):
            c.set_rendering_enabled(False);c.deactivate()
    actors=[]
    def spawn(cls,name,loc):
        a=ed.spawn_actor_from_class(cls,unreal.Vector(*loc),transient=True)
        a.set_actor_label(name);a.set_actor_hidden_in_game(False);actors.append(a)
        return a
    base=unreal.Vector(8000,-26000,0)
    template=next(a for a in ed.get_all_level_actors() if a.get_actor_label()=='WarMachineHover_PlayablePreview')
    model=ed.duplicate_actor(template,offset=base-template.get_actor_location())
    model.set_actor_label('WM01_Reference_TransientModel');model.set_editor_property('tags',[])
    model.set_actor_hidden_in_game(False);actors.append(model)
    body=model.get_component_by_class(unreal.InstancedStaticMeshComponent);assert body
    body.clear_instances();body.set_visibility(True);body.set_editor_property('num_custom_data_floats',51)
    body.set_forced_lod_model(1);body.set_cull_distances(0,0)
    body.add_instance(unreal.Transform(scale=unreal.Vector(.2,.2,.2)),False)
    ground=spawn(unreal.StaticMeshActor,'WM01_Reference_TransientFloor',(base.x+3000,base.y,-60))
    ground.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    ground.set_actor_scale3d(unreal.Vector(160,100,1))
    gm=ground.static_mesh_component.create_dynamic_material_instance(0,unreal.load_asset('/Engine/BasicShapes/BasicShapeMaterial'))
    gm.set_vector_parameter_value('Color',unreal.LinearColor(.22,.25,.29,1))
    fxactor=spawn(unreal.NiagaraActor,'WM01_Reference_TransientFX',(0,0,0))
    fx=fxactor.niagara_component;fx.set_auto_activate(False)
    fx.set_asset(unreal.load_asset(PREFIX+'_Preview'),True)
    fx.set_force_solo(True);fx.set_cast_shadow(False);fx.set_can_render_while_seeking(True);fx.set_component_tick_enabled(False)
    bounds=unreal.Box(min=base+unreal.Vector(-5000,-5000,-2000),max=base+unreal.Vector(18000,5000,3000))
    fx.set_system_fixed_bounds(bounds)
    for name in ['LaserBolts','LaserMuzzles']:fx.set_emitter_fixed_bounds(name,bounds)
    capture=spawn(unreal.SceneCapture2D,'WM01_Reference_TransientCapture',base.to_tuple())
    cap=capture.capture_component2d
    cap.set_editor_property('capture_every_frame',False);cap.set_editor_property('capture_on_movement',False)
    cap.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    cap.set_editor_property('fov_angle',45.)
    pp=cap.get_editor_property('post_process_settings')
    for k,v in [('override_auto_exposure_method',True),('auto_exposure_method',unreal.AutoExposureMethod.AEM_MANUAL),('override_auto_exposure_bias',True),('auto_exposure_bias',0.),('override_auto_exposure_apply_physical_camera_exposure',True),('auto_exposure_apply_physical_camera_exposure',False)]:pp.set_editor_property(k,v)
    cap.set_editor_property('post_process_settings',pp);cap.set_editor_property('post_process_blend_weight',1.)
    rt=unreal.RenderingLibrary.create_render_target2d(world,1280,720,unreal.TextureRenderTargetFormat.RTF_RGBA8,unreal.LinearColor(0,0,0,1),False,False)
    cap.set_editor_property('texture_target',rt)
    cap.set_editor_property('primitive_render_mode',unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
    for a in [model,ground,fxactor]:cap.show_only_actor_components(a)
    state={'frame':-120,'done':False,'captures':[],'gpu':[],'body':body,'fx':fx}
    def finish(error=None):
        if state['done']:return
        state['done']=True;unreal.unregister_slate_post_tick_callback(state['handle'])
        for a in reversed(actors):ed.destroy_actor(a)
        editor.set_level_viewport_camera_info(*old_camera)
        r={k:v for k,v in state.items() if k not in ['body','fx','handle','finish']}
        r.update(success=not error,error=error,scope='real GPU shader and world-history preview; synthetic nozzle path; no PIE/network/performance validation',actors_removed=True)
        (OUT/'preview.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
    state['finish']=finish
    def tick(dt):
        try:
            if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():return finish('PIE started externally')
            f=state['frame'];t=max(0,f/60)
            speed=0 if t<1.5 or 7.5<=t<10.5 or t>=12.5 else 720 if t<3.5 else 1080 if t<5.5 else 1440
            travel=0 if t<1.5 else (t-1.5)*720 if t<3.5 else 1440+(t-3.5)*1080 if t<5.5 else 3600+(t-5.5)*1440 if t<7.5 else 6480
            if t>=10.5:travel+=min(t-10.5,2.0)*1440
            label='idle' if t<1.5 else '5m' if t<3.5 else '10m' if t<5.5 else '15m' if t<7.5 else 'stopping' if t<8 else 'pods_hidden' if t<9.5 else 'pods_visible' if t<10.5 else 'restart_15m' if t<12.5 else 'stopped_again'
            root=base+unreal.Vector(travel,0,0)
            body.update_instance_transform(0,unreal.Transform(location=root,scale=unreal.Vector(.2,.2,.2)),True,True,False)
            pose=[0]*14;pose[11]=600+25*math.sin(t*2)
            data=[-1000]+pose+pose+[0 if t<9.5 else 1]*2+[0]*20
            for i,v in enumerate(data):body.set_custom_data_value(0,i,v,i==50)
            nozzle_root=root+unreal.Vector(0,0,pose[11]*.2)
            fx.set_variable_vec3('User.HoverPreviewRoot',nozzle_root)
            fx.set_variable_float('User.HoverPreviewSpeed',speed)
            fx.set_variable_float('User.HoverTime',t)
            if f==0:
                fx.reinitialize_system();fx.activate(True);fx.set_rendering_enabled(True);fx.set_component_tick_enabled(False)
            if f>0:fx.advance_simulation(1,1/60)
            camloc=root+unreal.Vector(2050,-2750,1180);aim=root+unreal.Vector(-120,0,360)
            capture.set_actor_location_and_rotation(camloc,unreal.MathLibrary.find_look_at_rotation(camloc,aim),False,True)
            editor.set_level_viewport_camera_info(camloc,capture.get_actor_rotation())
            if f in [75,96,105,120,135,150,180,300,420,500,636,645,660,675,690,735,780]:
                cache=unreal.NiagaraSimCacheFunctionLibrary.create_niagara_sim_cache(world)
                params=unreal.NiagaraSimCacheCreateParameters();params.set_editor_property('attribute_capture_mode',unreal.NiagaraSimCacheAttributeCaptureMode.ALL)
                assert unreal.NiagaraSimCacheFunctionLibrary.capture_niagara_sim_cache_immediate(cache,params,fx,False),'GPU snapshot unavailable'
                sample={'frame':f,'time':t,'emitters':{}}
                for en in cache.get_emitter_names():
                    colors=cache.read_color_attribute('Color',en);sizes=cache.read_vector2_attribute('SpriteSize',en)
                    row={'count':len(colors),'visible':sum(c.a>0 and s.x>0 and s.y>0 for c,s in zip(colors,sizes))}
                    if str(en)=='LaserBolts':
                        positions=cache.read_position_attribute('Position',en)
                        slots=cache.read_int_attribute('LaserSlot',en)
                        row['attachment']=[]
                        for p,c,s,slot in zip(positions,colors,sizes,slots):
                            if c.a>0 and s.x>0 and s.y>0:
                                top=p+unreal.Vector(0,0,s.y*.5)
                                expected=nozzle_root+OFFSETS[slot]
                                row['attachment'].append({'slot':slot,'top':list(top.to_tuple()),'nozzle':list(expected.to_tuple()),'error_cm':(top-expected).length()})
                        row['max_attachment_error_cm']=max(v['error_cm'] for v in row['attachment']) if row['attachment'] else None
                    if str(en)=='LaserMuzzles':
                        slots=cache.read_int_attribute('LaserSlot',en)
                        lanes=cache.read_int_attribute('HoverLane',en)
                        lengths=cache.read_float_attribute('RefLength',en)
                        ages=cache.read_float_attribute('RefMoveAge',en)
                        # GPU buffers are not ordered by nozzle/lane. Identify
                        # the live nozzle explicitly, never take element zero.
                        row['samples']=sorted([{'lane':lane,'travel':v,'sample':p,'alpha':c.a,'length':l} for slot,lane,v,p,c,l in zip(slots,lanes,cache.read_float_attribute('RefTravel',en),cache.read_float_attribute('RefSampleDistance',en),colors,lengths) if slot==0],key=lambda s:s['lane'])
                        row['length_cm']=max(l for slot,l in zip(slots,lengths) if slot<4)
                        row['moving_age_seconds']=max(a for slot,a in zip(slots,ages) if slot<4)
                        positions=cache.read_position_attribute('Position',en)
                        extents=[]
                        for slot,p,c,s in zip(slots,positions,colors,sizes):
                            if slot<4 and c.a>0 and s.x>0 and s.y>0:
                                nozzle_x=(nozzle_root+OFFSETS[slot]).x
                                extents.extend([nozzle_x-p.x-s.y*.5,nozzle_x-p.x+s.y*.5])
                        row['visible_path_extent_cm']=[min(extents),max(extents)] if extents else []
                        assert not extents or (min(extents)>-.25 and max(extents)<=row['length_cm']+.25),row
                    sample['emitters'][str(en)]=row
                state['gpu'].append(sample)
                (OUT/'gpu-snapshots.json').write_text(json.dumps(state['gpu'],indent=2),encoding='utf-8')
                assert sample['emitters']['LaserBolts']['visible']==4,sample
                assert sample['emitters']['LaserBolts']['max_attachment_error_cm']<.25,sample
                assert (sample['emitters']['LaserMuzzles']['visible']>0 if speed>0 else sample['emitters']['LaserMuzzles']['visible']==0),sample
                expected_lengths={96:14,105:78.125,120:250,135:421.875,150:500,180:500,300:1000,420:1500,636:42,645:234.375,660:720,675:1080,690:1440,735:1500}
                if f in expected_lengths:assert abs(sample['emitters']['LaserMuzzles']['length_cm']-expected_lengths[f])<.2,sample
            if f>61 and (f-1) % 15==0:
                filename=f'{f-1:04d}_{label}.png'
                unreal.RenderingLibrary.export_render_target(world,rt,str(OUT),filename)
                state['captures'].append({'file':filename,'time':(f-1)/60,'speed_cm_s':speed,'state':label})
            cap.capture_scene();state['frame']+=1
            if f>=795:finish()
        except Exception:finish(traceback.format_exc())
    state['handle']=unreal.register_slate_post_tick_callback(tick)
    return state

WM01_HOVER_REFERENCE=start_reference_preview()
unreal.MCPythonHelper.submit_result(json.dumps({'started':True,'output':str(OUT)}))
