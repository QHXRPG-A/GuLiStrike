"""Real rendered Editor preview; retain the review session for the user, no PIE."""
import unreal, json, traceback, math, time
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004');O=R/'UE_Delivery_v1';P=O/'Previews';P.mkdir(parents=True,exist_ok=True)
BASE='/Game/GuLiStrike/Mechs/ControlRigMech';MAP=BASE+'/Preview/LVL_ControlRigMech_B_v4'
OWNER='GuLiStrike.ControlRigMech.B-v4.UE-v1';LIB=unreal.EditorAssetLibrary
REPORT={'success':False,'map':MAP,'captures':[],'component_pose_checks':[],'PIE':'not_run'}
def dump(name,data):(O/name).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
def path(o):return o.get_path_name() if o else None
def vec(v):return list(v.to_tuple())
def tr(t):return {'translation_cm':vec(t.translation),'rotation_xyzw':vec(t.rotation),'scale':vec(t.scale3d)}
def main():
    assert '-ControlRigMechReviewSession' in unreal.SystemLibrary.get_command_line()
    imported=json.loads((O/'ue_import_report.json').read_text(encoding='utf-8'));assert imported['success']
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    mesh=unreal.load_asset(BASE+'/Meshes/SKM_ControlRigMech');skeleton=unreal.load_asset(BASE+'/Skeleton/SKEL_ControlRigMech');cr=unreal.load_asset(BASE+'/Rigs/CR_ControlRigMech')
    assert mesh.get_editor_property('skeleton')==skeleton and cr.get_preview_mesh()==mesh
    sub=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    # Confirm persisted LOD settings, sections and ownership after process reload.
    REPORT['asset_reload']={'mesh':path(mesh),'skeleton':path(skeleton),'lods':[{'lod':i,'sections':sub.get_num_sections(mesh,i),'render_vertices':sub.get_num_verts(mesh,i),'screen_size':source.get_editor_property('screen_size').default,'materials':[path(mesh.materials[sub.get_lod_material_slot(mesh,i,j)].material_interface) for j in range(sub.get_num_sections(mesh,i))]} for i,source in enumerate(mesh.get_editor_property('source_models'))],'physics':path(mesh.physics_asset),'sockets':len(unreal.SkeletonService.list_sockets(path(mesh)))}
    assert [x['sections'] for x in REPORT['asset_reload']['lods']]==[2,2,2,1]
    assert all(abs(x['screen_size']-s)<1e-6 for x,s in zip(REPORT['asset_reload']['lods'],[1.,.40,.16,.06]))
    source_bones={str(b.bone_name):b for b in unreal.SkeletonService.list_bones('/Game/Assets/ControlRig/Characters/Mech/Meshes/SK_Mech')}
    for b in unreal.SkeletonService.list_bones(path(skeleton)):
        a=source_bones[str(b.bone_name)];assert str(a.parent_bone_name)==str(b.parent_bone_name)
        assert (a.local_transform.translation-b.local_transform.translation).length()<1e-6
        assert max(abs(x-y) for x,y in zip(a.local_transform.rotation.to_tuple(),b.local_transform.rotation.to_tuple()))<1e-6
    REPORT['copied_skeleton_exact_source_reference']=True
    export_path=O/'FBX/UE_Readback_AllLODs.fbx'
    task=unreal.AssetExportTask();task.set_editor_property('object',mesh);task.set_editor_property('filename',str(export_path));task.set_editor_property('automated',True);task.set_editor_property('prompt',False);task.set_editor_property('replace_identical',True);task.set_editor_property('exporter',unreal.SkeletalMeshExporterFBX())
    options=unreal.FbxExportOption();options.set_editor_property('ascii',False);options.set_editor_property('collision',False);options.set_editor_property('level_of_detail',True);options.set_editor_property('bake_material_inputs',unreal.FbxMaterialBakeMode.DISABLED);task.set_editor_property('options',options)
    assert unreal.Exporter.run_asset_export_task(task)
    REPORT['UE_export_readback_file']=str(export_path.relative_to(O))
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert not level.is_in_play_in_editor()
    current_path=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name().split('.')[0]
    if current_path!=MAP:
        if LIB.does_asset_exist(MAP):assert level.load_level(MAP)
        else:assert level.new_level(MAP)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world();assert world.get_path_name().split('.')[0]==MAP
    assert LIB.get_metadata_tag(world,'GuLi.Owner') in ['',OWNER]
    LIB.set_metadata_tag(world,'GuLi.Owner',OWNER)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in actors.get_all_level_actors():
        if a.get_actor_label().startswith('CRM_'):actors.destroy_actor(a)
    def spawn(cls,label,xyz=(0,0,0),rot=unreal.Rotator()):
        a=actors.spawn_actor_from_class(cls,unreal.Vector(*xyz),rot);assert a,label;a.set_actor_label('CRM_'+label);return a
    actor=spawn(unreal.SkeletalMeshActor,'B_v4');comp=actor.get_component_by_class(unreal.SkeletalMeshComponent);comp.set_skeletal_mesh_asset(mesh)
    comp.set_update_animation_in_editor(True);comp.set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
    comp.set_editor_property('forced_lod_model',1)
    source_actor=spawn(unreal.SkeletalMeshActor,'SourcePlaybackCheck',(0,0,-1000000))
    source_comp=source_actor.get_component_by_class(unreal.SkeletalMeshComponent);source_comp.set_skeletal_mesh_asset(unreal.load_asset('/Game/Assets/ControlRig/Characters/Mech/Meshes/SKM_Mech'))
    source_comp.set_update_animation_in_editor(True);source_comp.set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES);source_comp.set_editor_property('forced_lod_model',1)
    floor=spawn(unreal.StaticMeshActor,'Floor',(0,0,-25));floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'));floor.set_actor_scale3d(unreal.Vector(60,60,.5))
    # Use an owned unlit backdrop to match the Blender review background.
    background_path=BASE+'/Preview/M_ControlRigMech_PreviewBackground'
    background=unreal.load_asset(background_path) if LIB.does_asset_exist(background_path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_ControlRigMech_PreviewBackground',BASE+'/Preview',unreal.Material,unreal.MaterialFactoryNew())
    assert background and LIB.get_metadata_tag(background,'GuLi.Owner') in ['',OWNER]
    LIB.set_metadata_tag(background,'GuLi.Owner',OWNER)
    edit=unreal.MaterialEditingLibrary;edit.delete_all_material_expressions(background)
    background.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT);background.set_editor_property('two_sided',True)
    color=edit.create_material_expression(background,unreal.MaterialExpressionConstant3Vector)
    def linear(c):return c/12.92 if c<=.04045 else ((c+.055)/1.055)**2.4
    color.set_editor_property('constant',unreal.LinearColor(*[linear(c/255.) for c in [247,242,232]],1.))
    assert edit.connect_material_property(color,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.recompile_material(background);assert LIB.save_loaded_asset(background,False)
    REPORT['preview_background_material']=path(background)
    floor.static_mesh_component.set_material(0,background)
    light=spawn(unreal.DirectionalLight,'Key',rot=unreal.Rotator(-55,-25,0));lc=light.get_component_by_class(unreal.DirectionalLightComponent);lc.set_mobility(unreal.ComponentMobility.MOVABLE);lc.set_intensity(4)
    sky=spawn(unreal.SkyLight,'Ambient');sk=sky.get_component_by_class(unreal.SkyLightComponent);sk.set_mobility(unreal.ComponentMobility.MOVABLE);sk.set_intensity(.4)
    pp=spawn(unreal.PostProcessVolume,'Exposure');pp.set_editor_property('unbound',True);settings=pp.settings
    for k,v in dict(override_auto_exposure_method=True,auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,override_auto_exposure_bias=True,auto_exposure_bias=0.,override_auto_exposure_apply_physical_camera_exposure=True,auto_exposure_apply_physical_camera_exposure=False,override_bloom_intensity=True,bloom_intensity=0.,override_vignette_intensity=True,vignette_intensity=0.,override_tone_curve_amount=True,tone_curve_amount=0.).items():settings.set_editor_property(k,v)
    pp.set_editor_property('settings',settings)
    camera=spawn(unreal.CameraActor,'ReviewCamera');cc=camera.get_component_by_class(unreal.CameraComponent);cc.set_editor_property('constrain_aspect_ratio',False);cc.set_field_of_view(45.)
    cameras=json.loads((R/'References_A_v2/reference_setup.json').read_text(encoding='utf-8'))['cameras']
    camera_row=json.loads(Path('D:/UE5.7/test1/Data/Json/DT_GuLiStrikeCommander_Camera.json').read_text(encoding='utf-8'))[0]
    REPORT['camera_source']={'path':'Data/Json/DT_GuLiStrikeCommander_Camera.json','row':camera_row,'no_HUD_overlay':True}
    definition=unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap');mn=definition.get_editor_property('playable_minimum');mx=definition.get_editor_property('playable_maximum')
    # Use the project's actual battlefield extents for the 90-degree overview.
    # The independent review has no HUD, so frame the complete viewport rectangle.
    rx=(mx.x-mn.x)*.5;ry=(mx.y-mn.y)*.5;tan=math.tan(math.radians(camera_row['FieldOfViewDegrees'])*.5)
    overview_height=max(rx/tan,ry/tan)* (1+camera_row['OverviewPaddingFraction'])
    REPORT['overview_framing']={'playable_minimum_cm':vec(mn),'playable_maximum_cm':vec(mx),'height_cm':overview_height,'aspect_ratio':1.,'HUD_effect_on_framing':'not evaluated; full review viewport used'}
    backdrop=spawn(unreal.StaticMeshActor,'Backdrop');backdrop.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    backdrop.set_actor_scale3d(unreal.Vector(.05,max(200.,overview_height/25.),max(200.,overview_height/25.)));backdrop.static_mesh_component.set_material(0,background)
    backdrop.static_mesh_component.set_editor_property('cast_shadow',False)
    capture=spawn(unreal.SceneCapture2D,'NativeCapture');sc=capture.get_component_by_class(unreal.SceneCaptureComponent2D)
    target_rt=unreal.RenderingLibrary.create_render_target2d(capture,2048,2048,unreal.TextureRenderTargetFormat.RTF_RGBA8)
    target_rt.set_editor_property('target_gamma',2.2)
    for k,v in dict(texture_target=target_rt,capture_every_frame=False,capture_on_movement=False,capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR).items():sc.set_editor_property(k,v)
    REPORT['capture_method']='Native SceneCapture2D FinalColorLDR, 2048x2048 RGBA8, target gamma 2.2; no image painting'
    captures=[]
    for view in ['Hero','Front','Left','Back']:captures.append({'name':f'UE_B_v4_{view}.png','view':view,'lod':0,'animation':None,'t':0.})
    for lod in [1,2,3]:captures.append({'name':f'UE_B_v4_LOD{lod}.png','view':'Hero','lod':lod,'animation':None,'t':0.})
    for name in ['Mech_Deploy','Mech_Idle','Mech_Walk']:
        anim=unreal.load_asset(BASE+'/Animations/'+name);assert anim.get_editor_property('skeleton')==skeleton
        for label,t in [('Start',0.),('Mid',anim.get_play_length()*.5),('End',anim.get_play_length())]:captures.append({'name':f'UE_B_v4_{name}_{label}.png','view':'Hero','lod':0,'animation':name,'t':t})
    for view in ['CommanderNear','CommanderTactical','CommanderOverview']:captures.append({'name':f'UE_B_v4_{view}.png','view':view,'lod':None,'animation':None,'t':0.})
    state={'index':0,'configured':False,'ready_at':0.,'started':time.monotonic()}
    def setup(c):
        comp.set_editor_property('forced_lod_model',0 if c['lod'] is None else c['lod']+1)
        if c['animation']:
            anim=unreal.load_asset(BASE+'/Animations/'+c['animation']);comp.set_animation_mode(unreal.AnimationMode.ANIMATION_BLUEPRINT)
            comp.override_animation_data(anim,False,False,c['t'],0.)
            comp.set_update_animation_in_editor(True);comp.set_play_rate(0.);comp.set_position(c['t'],False)
            source_anim=unreal.load_asset('/Game/Assets/ControlRig/Characters/Mech/Animations/'+c['animation'])
            source_comp.set_animation_mode(unreal.AnimationMode.ANIMATION_BLUEPRINT);source_comp.override_animation_data(source_anim,False,False,c['t'],0.)
            source_comp.set_update_animation_in_editor(True);source_comp.set_play_rate(0.);source_comp.set_position(c['t'],False)
        else:
            comp.set_animation_mode(unreal.AnimationMode.ANIMATION_BLUEPRINT);comp.override_animation_data(None,False,False,0.,0.);comp.set_update_animation_in_editor(True)
        v=c['view'];target=unreal.Vector(0,0,330)
        if v in cameras:
            cc.set_projection_mode(unreal.CameraProjectionMode.ORTHOGRAPHIC);cc.set_ortho_width(cameras[v]['ortho_scale_m']*100)
            p=cameras[v]['location_m'];position=unreal.Vector(p[0]*100,-p[1]*100,p[2]*100)
            # The reference look direction targets the same source bound center.
            b=mesh.get_imported_bounds();target=b.origin
        else:
            cc.set_projection_mode(unreal.CameraProjectionMode.PERSPECTIVE);cc.set_field_of_view(camera_row['FieldOfViewDegrees']);target=unreal.Vector(0,0,0)
            if v=='CommanderOverview':position=unreal.Vector(0,0,overview_height)
            else:
                height=camera_row['MinimumHeightMeters'] if v=='CommanderNear' else camera_row['TacticalStartHeightMeters']
                pitch=camera_row['NearPitchDegrees'] if v=='CommanderNear' else camera_row['TacticalPitchDegrees'];position=unreal.Vector(-height*100/math.tan(math.radians(pitch)),0,height*100)
        rotation=unreal.MathLibrary.find_look_at_rotation(position,target);camera.set_actor_location(position,False,False);camera.set_actor_rotation(rotation,False)
        unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(position,rotation)
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).pilot_level_actor(camera)
        c['camera']={'position_cm':vec(position),'rotation':str(rotation),'forced_lod':comp.forced_lod_model}
        # A camera-facing physical backdrop stays within the orthographic far plane.
        backdrop.set_actor_location(position+camera.get_actor_forward_vector()*((position-target).length()+3000.),False,False);backdrop.set_actor_rotation(rotation,False)
        capture.set_actor_location(position,False,False);capture.set_actor_rotation(rotation,False)
        sc.set_editor_property('projection_type',cc.get_editor_property('projection_mode'));sc.set_editor_property('ortho_width',cc.get_editor_property('ortho_width'));sc.set_editor_property('fov_angle',cc.get_editor_property('field_of_view'))
    def pose_check(c):
        original_path='/Game/Assets/ControlRig/Characters/Mech/Animations/'+c['animation'];poses=unreal.AnimSequenceService.get_pose_at_time(original_path,c['t'],True)
        errors=[];raw_errors=[];source_raw_errors=[];selected={}
        def delta(a,b):
            qa=a.rotation.to_tuple();qb=b.rotation.to_tuple();dot=abs(sum(x*y for x,y in zip(qa,qb)))/math.sqrt(sum(x*x for x in qa)*sum(y*y for y in qb))
            return ((a.translation-b.translation).length(),(a.scale3d-b.scale3d).length(),math.degrees(2*math.acos(min(1.,dot))))
        for p in poses:
            name=str(p.bone_name);t=comp.get_socket_transform(name,unreal.RelativeTransformSpace.RTS_COMPONENT)
            source_t=source_comp.get_socket_transform(name,unreal.RelativeTransformSpace.RTS_COMPONENT)
            errors.append((*delta(t,source_t),name));raw_errors.append(delta(t,p.transform));source_raw_errors.append(delta(source_t,p.transform))
            if name in ['root','base','cannon_02','foot_fr_01_l','foot_fr_01_r','foot_bk_01_l','foot_bk_01_r']:selected[name]=tr(t)
        max_t=max(e[0] for e in errors);max_s=max(e[1] for e in errors);max_r=max(e[2] for e in errors)
        record={'animation':c['animation'],'t':c['t'],'bone_count':len(errors),'comparison':'actual delivered component vs actual source component, both UE evaluated playback','max_152_bone_global_position_delta_cm':max_t,'max_scale_delta':max_s,'max_rotation_delta_deg':max_r,'target_component_vs_source_raw':{'position_cm':max(x[0] for x in raw_errors),'scale':max(x[1] for x in raw_errors),'rotation_degrees':max(x[2] for x in raw_errors)},'source_component_vs_source_raw':{'position_cm':max(x[0] for x in source_raw_errors),'scale':max(x[1] for x in source_raw_errors),'rotation_degrees':max(x[2] for x in source_raw_errors)},'selected_bones':selected,'evaluated_anim_instance':path(comp.get_anim_instance()),'source_anim_instance':path(source_comp.get_anim_instance())}
        REPORT['component_pose_checks'].append(record);dump('ue_preview_readback_report.json',REPORT)
        assert len(errors)==152 and max_t<.05 and max_s<.001 and max_r<.15,record
    def finish():
        setup(captures[0]);comp.set_update_animation_in_editor(False)
        sc.set_editor_property('texture_target',None);actors.destroy_actor(capture)
        actors.destroy_actor(source_actor)
        assert level.save_current_level()
        REPORT['success']=True;REPORT['user_review_session_left_open']=True;dump('ue_preview_readback_report.json',REPORT)
        try:unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([mesh])
        except Exception as e:REPORT['asset_editor_note']=str(e);dump('ue_preview_readback_report.json',REPORT)
    def tick(delta):
        try:
            assert time.monotonic()-state['started']<600,'render preview timed out'
            if state['index']==len(captures):unreal.unregister_slate_post_tick_callback(handle);finish();return
            c=captures[state['index']]
            if not state['configured']:setup(c);state.update(configured=True,ready_at=time.monotonic()+(8. if state['index']==0 else 1.5));return
            if time.monotonic()<state['ready_at']:return
            if c['animation']:pose_check(c)
            sc.capture_scene();unreal.RenderingLibrary.export_render_target(capture,target_rt,str(P),c['name'])
            assert (P/c['name']).is_file(),c
            REPORT['captures'].append(c);dump('ue_preview_readback_report.json',REPORT)
            state.update(index=state['index']+1,configured=False)
        except Exception:
            unreal.unregister_slate_post_tick_callback(handle);REPORT['error']=traceback.format_exc();dump('ue_preview_readback_report.json',REPORT);unreal.log_error(REPORT['error'])
    handle=unreal.register_slate_post_tick_callback(tick)
try:main()
except Exception:REPORT['error']=traceback.format_exc();dump('ue_preview_readback_report.json',REPORT);unreal.log_error(REPORT['error'])
if hasattr(unreal,'MCPythonHelper'):unreal.MCPythonHelper.submit_result(json.dumps({'success':'error' not in REPORT,'preview_started':'error' not in REPORT,'report':str(O/'ue_preview_readback_report.json'),'error':REPORT.get('error')}))
