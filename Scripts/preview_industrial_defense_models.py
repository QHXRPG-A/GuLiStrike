"""Build a separate UE showcase and capture the imported models and real poses.
Run in a rendered -IndustrialDefensePreviewWorker process, never the live task.
"""
import unreal,json,traceback,math,time
from pathlib import Path

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'outputs/hardsurface-models-20260914'
BASE='/Game/GuLiStrike/Buildings/IndustrialDefenseSet/Demo'
MAP=BASE+'/LVL_IndustrialDefense_Showcase'
OWNER='GuLiStrike.IndustrialDefenseSet.V3'

def main():
    if '-IndustrialDefensePreviewWorker' not in unreal.SystemLibrary.get_command_line():raise RuntimeError('Separate preview worker required')
    (OUT/'ue_preview_report.json').unlink(missing_ok=True)
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    lib=unreal.EditorAssetLibrary;level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    # Do not load a World asset into a Python variable before switching levels:
    # that reference prevents the editor from garbage collecting the old World.
    exists=lib.does_asset_exist(MAP)
    current_path=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name().split('.')[0]
    if current_path!=MAP:
        if exists:assert level.load_level(MAP)
        else:assert level.new_level(MAP)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if exists and lib.get_metadata_tag(world,'GuLi.ModelProduction.Owner')!=OWNER:raise RuntimeError('Unowned showcase map')
    lib.set_metadata_tag(world,'GuLi.ModelProduction.Owner',OWNER)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    # Only actors in this owned showcase are touched.
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label().startswith('IDS_'):actors.destroy_actor(actor)
    def spawn(cls,label,location=(0,0,0),rotation=unreal.Rotator()):
        actor=actors.spawn_actor_from_class(cls,unreal.Vector(*location),rotation)
        if not actor:raise RuntimeError('Spawn failed '+label)
        actor.set_actor_label('IDS_'+label);return actor
    report=json.loads((OUT/'ue_import_report.json').read_text(encoding='utf-8'))
    cannon_record=report['assets']['HeavyDefenseCannon']
    assert lib.get_metadata_tag(unreal.load_asset(cannon_record['animation']),'GuLi.ModelProduction.AnimationUnits')=='reference-transforms-v1','Run the importer animation normalization first'
    reference_bones={str(b.bone_name):b.global_transform for b in unreal.SkeletonService.list_bones(cannon_record['mesh'])}
    subjects={}
    offsets={'RedOreRefinery':(1160,980,0),'ShieldGenerator':(0,0,0),'HeavyDefenseCannon':(-1160,-980,0)}
    for name,record in report['assets'].items():
        mesh=unreal.load_asset(record['mesh']);skeletal=isinstance(mesh,unreal.SkeletalMesh)
        actor=spawn(unreal.SkeletalMeshActor if skeletal else unreal.StaticMeshActor,name,offsets[name])
        if skeletal:
            comp=actor.get_component_by_class(unreal.SkeletalMeshComponent);comp.set_skeletal_mesh_asset(mesh)
            comp.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
            comp.play_animation(unreal.load_asset(record['animation']),False);comp.set_update_animation_in_editor(True)
            comp.set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
            comp.set_play_rate(0);comp.set_position(0,False)
        else:actor.static_mesh_component.set_static_mesh(mesh)
        subjects[name]=actor
    floorpath=BASE+'/M_ShowcaseFloor';floor_mat=unreal.load_asset(floorpath)
    if floor_mat and lib.get_metadata_tag(floor_mat,'GuLi.ModelProduction.Owner')!=OWNER:raise RuntimeError('Unowned floor material')
    if not floor_mat:
        floor_mat=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_ShowcaseFloor',BASE,unreal.Material,unreal.MaterialFactoryNew())
        node=unreal.MaterialEditingLibrary.create_material_expression(floor_mat,unreal.MaterialExpressionConstant3Vector,-300,0)
        node.set_editor_property('constant',unreal.LinearColor(.017,.028,.045,1))
        unreal.MaterialEditingLibrary.connect_material_property(node,'',unreal.MaterialProperty.MP_BASE_COLOR)
        node=unreal.MaterialEditingLibrary.create_material_expression(floor_mat,unreal.MaterialExpressionConstant,-300,200)
        node.set_editor_property('r',.82);unreal.MaterialEditingLibrary.connect_material_property(node,'',unreal.MaterialProperty.MP_ROUGHNESS)
        unreal.MaterialEditingLibrary.recompile_material(floor_mat)
        lib.set_metadata_tag(floor_mat,'GuLi.ModelProduction.Owner',OWNER);assert lib.save_loaded_asset(floor_mat,False)
    floor=spawn(unreal.StaticMeshActor,'Floor',(0,0,-12));floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    floor.set_actor_scale3d(unreal.Vector(500,500,.20));floor.static_mesh_component.set_material(0,floor_mat)
    for i,(rotation,intensity,color) in enumerate([((-45,125,0),12.0,(1,.94,.85)),((-25,-40,0),5.0,(.78,.88,1))]):
        light=spawn(unreal.DirectionalLight,'Light_'+str(i),rotation=unreal.Rotator(*rotation))
        lc=light.get_component_by_class(unreal.DirectionalLightComponent)
        lc.set_mobility(unreal.ComponentMobility.MOVABLE);lc.set_intensity(intensity);lc.set_light_color(unreal.LinearColor(*color,1))
    skylight=spawn(unreal.SkyLight,'AmbientSky')
    sky=skylight.get_component_by_class(unreal.SkyLightComponent);sky.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
    sky.set_intensity(2.0)
    camera=spawn(unreal.CameraActor,'HeroCamera',(3500,-4700,3200))
    cc=camera.get_component_by_class(unreal.CameraComponent);cc.set_field_of_view(40)
    pp=unreal.PostProcessSettings()
    for k,v in dict(override_auto_exposure_method=True,auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
                    override_auto_exposure_apply_physical_camera_exposure=True,auto_exposure_apply_physical_camera_exposure=False,
                    override_auto_exposure_bias=True,auto_exposure_bias=0,
                    override_motion_blur_amount=True,motion_blur_amount=0,
                    override_bloom_intensity=True,bloom_intensity=.1).items():pp.set_editor_property(k,v)
    cc.set_editor_property('post_process_settings',pp);cc.set_editor_property('post_process_blend_weight',1)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode',unreal.GameModeBase)
    actors.set_selected_level_actors([])
    viewport_before=str(unreal.ViewportService.get_viewport_info())
    previous_mode=unreal.ViewportService.get_view_mode()
    assert unreal.ViewportService.set_view_mode('lit')
    assert unreal.ViewportService.set_exposure(True,-1.0)
    assert unreal.ViewportService.set_realtime(True)
    (OUT/'ue_preview_environment.json').write_text(json.dumps({'mode_before':previous_mode,'viewport_before':viewport_before,'viewport_after':str(unreal.ViewportService.get_viewport_info())},indent=2),encoding='utf-8')
    for cvar in ['r.ScreenPercentage 100','r.Streaming.FullyLoadUsedTextures 1','r.Streaming.PoolSize 2500','r.AntiAliasingMethod 2','r.PostProcessAAQuality 6']:
        unreal.SystemLibrary.execute_console_command(world,cvar)
    cannon=subjects['HeavyDefenseCannon'].get_component_by_class(unreal.SkeletalMeshComponent)
    captures=[('UE_All.png',None,0),('UE_RedOreRefinery.png','RedOreRefinery',0),
              ('UE_ShieldGenerator.png','ShieldGenerator',0),('UE_HeavyDefenseCannon.png','HeavyDefenseCannon',0),
              ('UE_HeavyDefenseCannon_Aimed.png','HeavyDefenseCannon',2),
              ('UE_HeavyDefenseCannon_Depressed.png','HeavyDefenseCannon',5)]
    state={'index':0,'configured':False,'warm':8.0,'task':None,'files':[],'poses':[],'started':time.monotonic()}
    def pose_record(t):
        instance=cannon.get_anim_instance()
        row={'seconds':t,'instance_seconds':cannon.get_position(),'animation_instance':str(instance),'animation_data':str(cannon.get_editor_property('animation_data')),'bones':{}}
        for name in ['root','base_yaw','barrel_pitch']:
            tr=cannon.get_socket_transform(name,unreal.RelativeTransformSpace.RTS_COMPONENT)
            assert (tr.scale3d-reference_bones[name].scale3d).length()<.01,'Animated scale differs from reference: '+name
            assert (tr.translation-reference_bones[name].translation).length()<.01,'Mechanical pivot moved: '+name
            row['bones'][name]={'location':list(tr.translation.to_tuple()),'scale':list(tr.scale3d.to_tuple()),'rotation_xyzw':[getattr(tr.rotation,k) for k in ['x','y','z','w']]}
        # Remove parent yaw before measuring elevation; global change alone could be yaw only.
        a=row['bones']['base_yaw']['rotation_xyzw'];b=row['bones']['barrel_pitch']['rotation_xyzw']
        x,y,z,w=-a[0],-a[1],-a[2],a[3];X,Y,Z,W=b
        row['pitch_local_xyzw']=[w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z]
        return row
    def configure(subject,t):
        for name,actor in subjects.items():actor.set_is_temporarily_hidden_in_editor(subject is not None and subject!=name)
        # Recreate the single-node instance from persistent AnimationData. Merely
        # changing its mode to the same value does not initialize that instance.
        cannon.set_animation_mode(unreal.AnimationMode.ANIMATION_BLUEPRINT)
        cannon.override_animation_data(unreal.load_asset(report['assets']['HeavyDefenseCannon']['animation']),False,False,t,0.0)
        if subject:
            offset=subjects[subject].get_actor_location()
            span=max(report['assets'][subject]['dimensions_cm'])
            target=offset+unreal.Vector(0,0,report['assets'][subject]['dimensions_cm'][2]*.45)
            # Slightly wider view covers the full elevated rail assembly.
            if subject=='HeavyDefenseCannon':span*=1.13;target+=unreal.Vector(50,0,40)
            position=target+unreal.Vector(1.45,-2.0,1.32)*span
            cc.set_field_of_view(36);cc.set_editor_property('aspect_ratio',1.5)
        else:
            target=unreal.Vector(0,0,240);position=unreal.Vector(3500,-4700,3200)
            cc.set_field_of_view(40);cc.set_editor_property('aspect_ratio',2)
        camera.set_actor_location(position,False,False)
        rotation=unreal.MathLibrary.find_look_at_rotation(position,target);camera.set_actor_rotation(rotation,False)
        unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(position,rotation)
    def finish():
        configure(None,0)
        for actor in subjects.values():actor.set_is_temporarily_hidden_in_editor(False)
        cannon.set_update_animation_in_editor(False)
        lib.set_metadata_tag(world,'GuLi.ModelProduction.Owner',OWNER)
        assert level.save_current_level()
        # Verify pose changes from actual evaluated UE components, including a fixed root.
        poses=state['poses'];rest=poses[0]['bones']['root']
        (OUT/'ue_pose_samples.json').write_text(json.dumps(poses,indent=2),encoding='utf-8')
        assert all(p['bones']['root']==rest for p in poses),'Cannon root moved'
        angle=lambda a,b:math.degrees(2*math.acos(min(1,abs(sum(x*y for x,y in zip(a,b))))))
        pitch_angles=[angle(poses[0]['pitch_local_xyzw'],p['pitch_local_xyzw']) for p in poses]
        yaw_angles=[angle(poses[0]['bones']['base_yaw']['rotation_xyzw'],p['bones']['base_yaw']['rotation_xyzw']) for p in poses]
        assert max(pitch_angles)>34,'Pitch did not change'
        assert max(yaw_angles)>179,'Yaw did not change'
        (OUT/'ue_preview_report.json').write_text(json.dumps({'success':True,'map':MAP,'captures':state['files'],'poses':poses,'pitch_angles_deg':pitch_angles,'yaw_angles_deg':yaw_angles},indent=2),encoding='utf-8')
        (OUT/'ue_preview_error.txt').unlink(missing_ok=True)
        unreal.SystemLibrary.quit_editor()
    def tick(delta):
        try:
            if time.monotonic()-state['started']>240:raise RuntimeError('Preview capture exceeded four minutes')
            if state['task']:
                if not state['task'].is_task_done():return
                filename=captures[state['index']][0]
                if not (OUT/filename).is_file():raise RuntimeError('Missing UE screenshot '+filename)
                state['files'].append(filename);state.update(index=state['index']+1,configured=False,task=None)
            if state['index']==len(captures):
                unreal.unregister_slate_post_tick_callback(handle);finish();return
            filename,subject,t=captures[state['index']]
            if not state['configured']:
                configure(subject,t);state.update(configured=True,warm=10.0 if state['index']==0 else 1.5);return
            state['warm']-=delta
            if state['warm']>0:return
            if subject=='HeavyDefenseCannon':state['poses'].append(pose_record(t))
            state['task']=unreal.AutomationLibrary.take_high_res_screenshot(2400 if subject is None else 1800,1200,str(OUT/filename),camera,delay=1.5)
        except Exception:
            unreal.unregister_slate_post_tick_callback(handle)
            (OUT/'ue_preview_error.txt').write_text(traceback.format_exc(),encoding='utf-8')
            unreal.log_error(traceback.format_exc());unreal.SystemLibrary.quit_editor()
    handle=unreal.register_slate_post_tick_callback(tick)

if __name__=='__main__':
    try:main()
    except Exception:
        OUT.mkdir(parents=True,exist_ok=True);(OUT/'ue_preview_error.txt').write_text(traceback.format_exc(),encoding='utf-8')
        unreal.log_error(traceback.format_exc());unreal.SystemLibrary.quit_editor()
