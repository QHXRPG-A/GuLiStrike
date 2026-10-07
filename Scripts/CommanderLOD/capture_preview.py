"""Actual editor art renders of the saved review samples; never starts gameplay."""
import unreal,json,math,time,traceback,sys
from pathlib import Path
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART,selected_indices
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem);world=editor.get_editor_world()
assert not editor.get_game_world()
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
scene=json.loads((ART/'Reports/review_scene.json').read_text(encoding='utf8'))
only_unit=globals().get('commander_lod_arguments',{}).get('unit')
source_compare=globals().get('commander_lod_arguments',{}).get('source_compare',False)
if only_unit:scene['groups']=[g for g in scene['groups'] if g['name']==only_unit]
camera_data=json.loads((ROOT/'Data/Json/DT_GuLiStrikeCommander_Camera.json').read_text(encoding='utf8'))[0]
labels={actor.get_actor_label():actor for actor in actors.get_all_level_actors()}
if only_unit:labels.update({actor.get_actor_label():actor for actor in unreal._commander_lod_preview_actors})
state=dict(success=False,start=time.time(),frames=[],actors=[],saved_sample_poses_restored=False)
review_actors={}
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
groups={};jobs=[]

def spawn(cls,position):
    actor=actors.spawn_actor_from_class(cls,position,transient=True);state['actors'].append(actor);return actor

def finish(error=None):
    state['finished']=True;state['success']=error is None
    if state.get('handle'):unreal.unregister_slate_post_tick_callback(state['handle'])
    for group in groups.values():
        for component in group['components']:
            if isinstance(component,unreal.InstancedStaticMeshComponent):
                for index in range(63):component.set_custom_data_value(0,index,-1000 if index in [0,29] else (1 if index==30 else 0),index==62)
    for actor in reversed(state['actors']):actors.destroy_actor(actor)
    for actor,previous in review_actors.values():actor.set_editor_property('is_editor_only_actor',previous)
    settings_file=ART/'Reports/capture_editor_settings.json'
    if settings_file.exists():
        previous=json.loads(settings_file.read_text(encoding='utf8')).get('idle_when_not_foreground',0)
        unreal.SystemLibrary.execute_console_command(world,'t.IdleWhenNotForeground '+str(previous))
    if world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype':
        unreal.EditorLoadingAndSavingUtils.save_map(world,'/Game/Maps/LVL_CommanderMassPrototype')
    report={key:value for key,value in state.items() if key not in ['actors','handle']}
    report.update(success=error is None,error=error,saved_sample_poses_restored=True,
        scope='Actual editor art rendering and authored pose inspection; no PIE, movement simulation, networking or FPS test')
    destination=ART/only_unit/'Reports/ue_source_preview.json' if source_compare else ART/only_unit/'Reports/ue_preview.json' if only_unit else ART/'Reports/ue_preview.json'
    destination.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')

try:
    for row in scene['groups']:
        objects=[labels[label] for label in row['actors']]
        for actor in objects:
            review_actors[actor.get_path_name()]=(actor,True)
            actor.set_editor_property('is_editor_only_actor',False)
        components=[actor.get_component_by_class(unreal.MeshComponent) for actor in objects]
        origin=unreal.Vector(*row['origin'])
        centers=[];mins=[];maxs=[]
        for actor in objects:
            center,extent=actor.get_actor_bounds(False)
            centers.append(center);mins.append(center-extent);maxs.append(center+extent)
        low=unreal.Vector(*[min(v.to_tuple()[i] for v in mins) for i in range(3)])
        high=unreal.Vector(*[max(v.to_tuple()[i] for v in maxs) for i in range(3)])
        center=(low+high)*.5;diameter=max((high-low).to_tuple())
        if row['name']=='BiZhiMao':center=origin+unreal.Vector(305,0,689);diameter=3600
        elif row['name']=='DefaultSoldier':center=origin+unreal.Vector(0,0,160);diameter=1000
        elif row['name']=='WM01':center=origin+unreal.Vector(0,0,150);diameter=1150
        elif row['name']=='SweeperSummon':center=origin+unreal.Vector(0,0,120);diameter=900
        group=dict(row);group.update(components=components,center=center,diameter=max(700,diameter*1.3))
        groups[(row['name'],row['lod'])]=group
        for view in ['Hero','Front','Left','Back']:jobs.append(dict(name=row['name'],lod=row['lod'],view=view,frame=0))
        if row['lod']==0:
            for view in ['Near','Tactical','Overview']:jobs.append(dict(name=row['name'],lod=0,view=view,frame=0))
        if row['name'] in ['DefaultSoldier','BiZhiMao']:
            source=ART/'BiZhiMao/vertex_metadata.json' if row['name']=='BiZhiMao' else ROOT/'ArtSource/Mechs/RSGMechStyle_20261003/Delivery_UE_v1/vat_metadata.json'
            meta=json.loads(source.read_text(encoding='utf8'))
            for clip in meta['clips']:
                if clip['name']=='Death' and row['name']=='BiZhiMao':continue
                for index in range(8):jobs.append(dict(name=row['name'],lod=row['lod'],view='Motion',clip=clip['name'],
                    index=index,frame=clip['first_frame']+(clip['frame_count']-1)*index/8))
        elif row['name'] in ['WM01','SweeperSummon']:
            for index in range(8):jobs.append(dict(name=row['name'],lod=row['lod'],view='Motion',clip='Mechanism',index=index,frame=index/8))
    if source_compare:jobs=[j for j in jobs if j['view']=='Hero']
    backdrop=spawn(unreal.StaticMeshActor,unreal.Vector(0,0,0));floor=backdrop.static_mesh_component
    floor.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Plane'))
    mat=unreal.new_object(unreal.Material);mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    color=unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector)
    color.set_editor_property('constant',unreal.LinearColor(.87,.85,.80,1))
    unreal.MaterialEditingLibrary.connect_material_property(color,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat);floor.set_material(0,mat)
    floor.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    capture=spawn(unreal.SceneCapture2D,unreal.Vector(0,0,0));cap=capture.capture_component2d
    for key,value in dict(capture_every_frame=False,capture_on_movement=False,always_persist_rendering_state=True,
        capture_source=unreal.SceneCaptureSource.SCS_SCENE_COLOR_HDR_NO_ALPHA,
        primitive_render_mode=unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST).items():cap.set_editor_property(key,value)
    pp=cap.get_editor_property('post_process_settings')
    for key,value in dict(auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,auto_exposure_bias=0,
        auto_exposure_apply_physical_camera_exposure=False,motion_blur_amount=0,bloom_intensity=0,
        tone_curve_amount=0,blue_correction=0,expand_gamut=0).items():
        pp.set_editor_property('override_'+key,True);pp.set_editor_property(key,value)
    cap.set_editor_property('post_process_settings',pp);cap.set_editor_property('post_process_blend_weight',1)
    rt=unreal.RenderingLibrary.create_render_target2d(world,1024,1024,unreal.TextureRenderTargetFormat.RTF_RGBA16F,unreal.LinearColor(0,0,0,1),False,False)
    movie=unreal.RenderingLibrary.create_render_target2d(world,640,640,unreal.TextureRenderTargetFormat.RTF_RGBA16F,unreal.LinearColor(0,0,0,1),False,False)
    playback=dict(index=0,ticks=0)
    def apply(job):
        group=groups[(job['name'],job['lod'])]
        cap.clear_show_only_components()
        for component in group['components']:
            cap.show_only_component(component)
            if isinstance(component,unreal.InstancedStaticMeshComponent):
                tier=selected_indices(unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_lod_count(component.static_mesh))[job['lod']] if source_compare else job['lod']
                component.set_forced_lod_model(tier+1)
                values={51:job['frame'],55:job['frame'],59:job['frame'],61:job['frame']}
                if job['name']=='WM01':
                    phase=job['frame']*math.tau if job['view']=='Motion' else 0
                    values={1:math.sin(phase)*.5,2:math.sin(phase)*.15,3:math.sin(phase)*.15,12:math.sin(phase)*35,
                        31:math.sin(phase)*.08,32:math.sin(phase)*.08}
                    values.update({index+14:value for index,value in list(values.items()) if index<15})
                    values[41]=values[31];values[42]=values[32]
                elif job['name']=='SweeperSummon':
                    angle=job['frame']*math.tau if job['view']=='Motion' else 0
                    values={index:angle for index in [8,9,10,11,22,23,24,25]}
                for index,value in values.items():component.set_custom_data_value(0,index,value,True)
        cap.show_only_component(floor)
        backdrop.set_actor_location(unreal.Vector(*group['origin'])+unreal.Vector(0,0,-35),False,True)
        backdrop.set_actor_scale3d(unreal.Vector(1200,1200,1))
        target=group['center'];size=group['diameter'];view=job['view']
        cap.set_editor_property('texture_target',movie if view=='Motion' else rt)
        cap.set_editor_property('fov_angle',45)
        if view in ['Front','Left','Back']:
            cap.set_editor_property('projection_type',unreal.CameraProjectionMode.ORTHOGRAPHIC)
            cap.set_editor_property('ortho_width',size)
            offset={'Front':unreal.Vector(size*3,0,0),'Left':unreal.Vector(0,-size*3,0),'Back':unreal.Vector(-size*3,0,0)}[view]
            position=target+offset
        else:
            cap.set_editor_property('projection_type',unreal.CameraProjectionMode.PERSPECTIVE)
            if view in ['Near','Tactical','Overview']:
                height=camera_data[{'Near':'MinimumHeightMeters','Tactical':'TacticalStartHeightMeters','Overview':'TacticalMaximumHeightMeters'}[view]]*100
                pitch=camera_data[{'Near':'NearPitchDegrees','Tactical':'TacticalPitchDegrees','Overview':'OverviewPitchDegrees'}[view]]
                cap.set_editor_property('fov_angle',camera_data['FieldOfViewDegrees'])
                horizontal=height/max(.0001,math.tan(math.radians(pitch)))
                position=target+unreal.Vector(horizontal*.707,-horizontal*.707,height)
            else:position=target+unreal.Vector(size*.94,-size*.94,size*.75)
        capture.set_actor_location_and_rotation(position,unreal.MathLibrary.find_look_at_rotation(position,target),False,True)
        cap.set_editor_property('camera_cut_this_frame',True)
    apply(jobs[0])
    def tick(dt):
        try:
            if time.time()-state['start']>3600:raise RuntimeError('Editor art capture exceeded authoring timeout')
            if playback['ticks']<4:cap.capture_scene();playback['ticks']+=1;return
            job=jobs[playback['index']]
            folder=ART/job['name']/'Review/UE';folder.mkdir(parents=True,exist_ok=True)
            filename=f'LOD{job["lod"]}_{job["view"]}'
            if source_compare:filename='Source_'+filename
            if job['view']=='Motion':filename+=f'_{job["clip"]}_{job["index"]:03d}'
            unreal.RenderingLibrary.export_render_target(world,movie if job['view']=='Motion' else rt,str(folder),filename+'.exr')
            state['frames'].append(dict(**job,file=str(folder/(filename+'.exr'))))
            playback['index']+=1;playback['ticks']=0
            if playback['index']==len(jobs):finish();return
            apply(jobs[playback['index']])
        except Exception:finish(traceback.format_exc())
    state['handle']=unreal.register_slate_post_tick_callback(tick)
    unreal._commander_lod_capture_state=state
    unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,state='capturing',jobs=len(jobs))))
except Exception:finish(traceback.format_exc());raise
