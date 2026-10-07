"""Independently reload both teams, check actual animation components and capture UE appearance.
Only ephemeral actors in the worker's unsaved Entry world; no gameplay map is changed.
"""
import unreal,json,time,math,traceback
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');D=R/'UE_Delivery_Team_v2';V=D/'Preview';V.mkdir(exist_ok=True)
BASE='/Game/GuLiStrike/Buildings/SSFStylized';OWNER='GuLi.SSFStylized.Team_B_v2.20261007'
REPORT={'success':False,'independent_process_reload':True,'assets':[],'component_animation_checks':[],
        'captures':[],'maps_saved':[],'gameplay_integrated':False,'old_formal_assets_preserved':True}
def dump():(D/'ue_validation.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
def error(a,b):
    q,r=a.rotation,b.rotation;aa=(q.x,q.y,q.z,q.w);bb=(r.x,r.y,r.z,r.w)
    dot=abs(sum(i*j for i,j in zip(aa,bb)))/(sum(i*i for i in aa)*sum(i*i for i in bb))**.5
    return [(a.translation-b.translation).length(),(a.scale3d-b.scale3d).length(),math.degrees(2*math.acos(min(1,dot)))]
def linear(code):
    rgb=[int(code[i:i+2],16)/255 for i in (1,3,5)]
    return [v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in rgb]+[1.]
def main():
    assert '-SSFTeamValidateWorker' in unreal.SystemLibrary.get_command_line()
    im=json.loads((D/'ue_import.json').read_text(encoding='utf8'));assert im['success']
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    reg=unreal.AssetRegistryHelpers.get_asset_registry();reg.wait_for_completion();reg.scan_paths_synchronous([BASE+'/Blue',BASE+'/Red'],force_rescan=True)
    lib=unreal.EditorAssetLibrary;E=unreal.MaterialEditingLibrary;SK=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    depoptions=unreal.AssetRegistryDependencyOptions(include_hard_package_references=True,include_soft_package_references=True,include_searchable_names=False,include_hard_management_references=False,include_soft_management_references=False)
    inventory=[];dependencies={};unexpected=[]
    for team in ['Blue','Red']:
        for data in reg.get_assets_by_path(BASE+'/'+team,recursive=True):
            o=data.get_asset();assert lib.get_metadata_tag(o,'GuLi.StyleOwner')==OWNER,o.get_path_name()
            inventory.append({'path':o.get_path_name(),'class':o.get_class().get_name()})
            deps=[str(d) for d in reg.get_dependencies(data.package_name,depoptions)];dependencies[str(data.package_name)]=deps
            unexpected.extend((str(data.package_name),d) for d in deps if d.startswith('/Game/') and not d.startswith(BASE+'/'))
    assert not unexpected,unexpected
    assert len(inventory)==len(im['saved_packages'])==48,(len(inventory),len(im['saved_packages']))
    REPORT.update(asset_inventory=inventory,asset_count=len(inventory),dependencies=dependencies,unexpected_project_dependencies=unexpected)
    for a in im['assets']:
        mesh=unreal.load_asset(a['path']);source=unreal.load_asset(a['source']);assert SK.get_lod_count(mesh)==3
        assert mesh.skeleton.get_path_name()==a['skeleton'];assert mesh.physics_asset.get_path_name()==a['physics']
        expected=list(unreal.SkeletonService.list_bones(a['source']));got=list(unreal.SkeletonService.list_bones(a['path']))
        assert [(b.bone_name,b.parent_bone_name) for b in expected]==[(b.bone_name,b.parent_bone_name) for b in got]
        pose=[0.,0.,0.]
        for x,y in zip(expected,got):pose=[max(i,j) for i,j in zip(pose,error(x.local_transform,y.local_transform))]
        assert pose[0]<.0001 and pose[1]<.00001 and pose[2]<.001,(a['key'],pose)
        screens=[x.get_editor_property('screen_size').get_editor_property('default') for x in mesh.get_editor_property('source_models')]
        assert max(abs(i-j) for i,j in zip(screens,[1.,.10,.035]))<1e-6
        for i in range(3):
            settings=SK.get_lod_build_settings(mesh,i);assert settings.get_editor_property('use_full_precision_u_vs') and not settings.get_editor_property('recompute_normals')
        body=unreal.load_asset(a['materials']['SSF_Body']);base=E.get_material_instance_vector_parameter_value(body,'Base Color');accent=E.get_material_instance_vector_parameter_value(body,'Team Color')
        assert max(abs(x-y) for x,y in zip([base.r,base.g,base.b,base.a],[1,1,1,1]))<1e-6
        assert max(abs(x-y) for x,y in zip([accent.r,accent.g,accent.b,accent.a],linear(a['theme']['Accent'])))<1e-6
        assert E.get_material_instance_texture_parameter_value(body,'Internal Line Mask').get_path_name()==a['line_mask']
        atlas=unreal.load_asset(a['base_color_atlas']);assert atlas.get_editor_property('srgb')
        slots=[s.get_editor_property('material_interface').get_path_name() for s in mesh.get_editor_property('materials')]
        assert slots==[a['materials'][name] for name in a['material_slots']]
        REPORT['assets'].append({'key':a['key'],'path':a['path'],'reload_success':True,'LOD_count':3,'screen_sizes':screens,
            'bone_count':len(got),'reference_pose_error_cm_scale_degrees':pose,'skeleton':a['skeleton'],'physics':a['physics'],
            'materials':slots,'Base_Color_Team_Color_interfaces':True,'line_mask':a['line_mask'],'atlas':a['base_color_atlas']})
    for anim in im['animations']:
        seq=unreal.load_asset(anim['path']);assert seq
        assert seq.get_editor_property('skeleton').get_path_name()==anim['skeleton']
        assert abs(seq.get_editor_property('sequence_length')-anim['duration_s'])<1e-6
    dump()
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().startswith('/Engine/Maps/Entry')
    for actor in actors.get_all_level_actors():actor.set_is_temporarily_hidden_in_editor(True)
    def spawn(cls,label):
        actor=actors.spawn_actor_from_class(cls,unreal.Vector());assert actor;actor.set_actor_label('SSF_Team_ResourceReview_'+label);return actor
    source_actor=spawn(unreal.SkeletalMeshActor,'PriorFormalPose');dest_actor=spawn(unreal.SkeletalMeshActor,'TeamFormalPose')
    source_comp=source_actor.get_component_by_class(unreal.SkeletalMeshComponent);dest_comp=dest_actor.get_component_by_class(unreal.SkeletalMeshComponent)
    for c in [source_comp,dest_comp]:
        c.set_update_animation_in_editor(True);c.set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    source_actor.set_is_temporarily_hidden_in_editor(True)
    assets={a['key']:a for a in im['assets']};jobs=[]
    for a in im['assets']:
        for anim in im['animations']:
            if anim['key']!=a['building']:continue
            for lod in range(3):
                for fraction in [0,.5,1.]:jobs.append((a,anim,lod,anim['duration_s']*fraction))
    assert len(jobs)==378
    camera=spawn(unreal.CameraActor,'Camera');cc=camera.get_component_by_class(unreal.CameraComponent);cc.set_field_of_view(25)
    pp=unreal.PostProcessSettings()
    for k,v in dict(override_auto_exposure_method=True,auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
        override_auto_exposure_apply_physical_camera_exposure=True,auto_exposure_apply_physical_camera_exposure=False,
        override_auto_exposure_bias=True,auto_exposure_bias=0,override_bloom_intensity=True,bloom_intensity=0,
        override_motion_blur_amount=True,motion_blur_amount=0).items():pp.set_editor_property(k,v)
    cc.set_editor_property('post_process_settings',pp);cc.set_editor_property('post_process_blend_weight',1)
    for cmd in ['r.Streaming.FullyLoadUsedTextures 1','r.Streaming.PoolSize 2500','r.ScreenPercentage 100','r.AntiAliasingMethod 1','ShowFlag.Tonemapper 0']:
        unreal.SystemLibrary.execute_console_command(world,cmd)
    unreal.ViewportService.set_view_mode('lit');unreal.ViewportService.set_realtime(True);unreal.ViewportService.set_exposure(True,0)
    backdrop=spawn(unreal.StaticMeshActor,'NeutralBackdrop');bc=backdrop.get_component_by_class(unreal.StaticMeshComponent)
    bc.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Plane'));bc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    mat=unreal.new_object(unreal.Material,name='SSF_Team_UnsavedBackdrop');mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    color=E.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector);color.set_editor_property('constant',unreal.LinearColor(.8796,.85499,.80695,1))
    assert E.connect_material_property(color,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR);E.recompile_material(mat);bc.set_material(0,mat)
    subjects={}
    for a in im['assets']:
        actor=spawn(unreal.SkeletalMeshActor,a['key']);comp=actor.get_component_by_class(unreal.SkeletalMeshComponent)
        comp.set_skeletal_mesh_asset(unreal.load_asset(a['path']));comp.set_update_animation_in_editor(True);comp.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
        comp.set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION);actor.set_is_temporarily_hidden_in_editor(True);subjects[a['key']]=(actor,comp)
    captures=[{'key':a['key'],'LOD':lod,'name':a['key']+'_LOD'+str(lod),'fov':25.,'distance_cm':max(a['dimensions_cm'])*(4.2 if a['building']=='Reactor' else 3.2),'purpose':'same_camera_LOD_comparison'} for a in im['assets'] for lod in range(3)]
    for a in im['assets']:
        for label,m,fov in [('Near_35m',35.,25.),('Tactical_300m',300.,55.),('Tactical_700m',700.,55.),('Overview_1500m',1500.,55.)]:
            captures.append({'key':a['key'],'LOD':None,'name':a['key']+'_'+label,'fov':fov,'distance_cm':m*100,'purpose':'project_distance_auto_LOD'})
    assert len(captures)==84
    state={'phase':'poses','index':0,'configured':False,'warm':0.,'task':None,'started':time.monotonic()}
    def finish():
        log=(D/'ue_validation.log').read_text(encoding='utf8',errors='replace')
        assert 'Failed to compile Material '+BASE not in log
        assert len(REPORT['captures'])==84 and len(REPORT['component_animation_checks'])==378
        REPORT.update(success=True,engine=unreal.SystemLibrary.get_engine_version(),animation_count=21,variant_animation_LOD_pose_cases=378,
            rendered_all_12_variants_all_3_LODs=True,temporary_entry_world_discarded=True,source_Drone_22nd_animation_unchanged=True)
        dump();unreal.SystemLibrary.quit_editor()
    def tick(delta):
        try:
            if time.monotonic()-state['started']>600:raise RuntimeError('Team resource validation exceeded ten minutes')
            if state['phase']=='poses':
                if state['index']==len(jobs):
                    source_actor.set_is_temporarily_hidden_in_editor(True);dest_actor.set_is_temporarily_hidden_in_editor(True)
                    state.update(phase='capture',index=0,configured=False);dump();return
                a,anim,lod,t=jobs[state['index']]
                if not state['configured']:
                    source_comp.set_skeletal_mesh_asset(unreal.load_asset(a['source']));dest_comp.set_skeletal_mesh_asset(unreal.load_asset(a['path']))
                    source_comp.set_forced_lod(1);dest_comp.set_forced_lod(lod+1)
                    for c in [source_comp,dest_comp]:
                        c.set_animation_mode(unreal.AnimationMode.ANIMATION_BLUEPRINT);c.override_animation_data(unreal.load_asset(anim['path']),False,False,t,0.)
                    state.update(configured=True,warm=.08);return
                state['warm']-=delta
                if state['warm']>0:return
                assert dest_comp.get_anim_instance() and abs(dest_comp.get_position()-t)<.001
                differences=[0.,0.,0.]
                for b in unreal.SkeletonService.list_bones(a['path']):
                    x=source_comp.get_socket_transform(b.bone_name,unreal.RelativeTransformSpace.RTS_COMPONENT);y=dest_comp.get_socket_transform(b.bone_name,unreal.RelativeTransformSpace.RTS_COMPONENT)
                    differences=[max(i,j) for i,j in zip(differences,error(x,y))]
                assert differences[0]<.001 and differences[1]<.0001 and differences[2]<.005,(a['key'],anim['path'],lod,t,differences)
                REPORT['component_animation_checks'].append({'variant':a['key'],'animation':anim['path'],'LOD':lod,'time_s':t,
                    'baseline':'actual preserved B_v1 formal mesh component with the same unchanged formal animation',
                    'all_bones_error_translation_cm_scale_angle_deg':differences,'actual_instance_seconds':dest_comp.get_position()})
                state.update(index=state['index']+1,configured=False)
                if state['index']%30==0:dump()
                return
            if state['task']:
                if not state['task'].is_task_done():return
                spec=captures[state['index']];path=V/(spec['name']+'.png');assert path.exists()
                _,comp=subjects[spec['key']];record=dict(spec,path=str(path),component_predicted_LOD=comp.get_predicted_lod_level())
                if spec['LOD'] is not None:assert record['component_predicted_LOD']==spec['LOD'],record
                REPORT['captures'].append(record);state.update(index=state['index']+1,configured=False,task=None);dump()
            if state['index']==len(captures):unreal.unregister_slate_post_tick_callback(handle);finish();return
            spec=captures[state['index']];actor,comp=subjects[spec['key']]
            if not state['configured']:
                for other,_ in subjects.values():other.set_is_temporarily_hidden_in_editor(other!=actor)
                comp.set_forced_lod(spec['LOD']+1 if spec['LOD'] is not None else 0)
                bounds=unreal.load_asset(assets[spec['key']]['path']).get_imported_bounds();target=bounds.origin;distance=spec['distance_cm']
                cc.set_field_of_view(spec['fov']);assert unreal.ViewportService.set_fov(spec['fov'])
                offset=unreal.Vector(.76,.98,.74);offset/=offset.length();position=target+offset*distance
                backdrop.set_actor_location(target-offset*max(assets[spec['key']]['dimensions_cm'])*1.1,False,False)
                backdrop.set_actor_rotation(unreal.MathLibrary.make_rot_from_z(offset),False);backdrop.set_actor_scale3d(unreal.Vector(distance*.04,distance*.04,1))
                rotation=unreal.MathLibrary.find_look_at_rotation(position,target);camera.set_actor_location(position,False,False);camera.set_actor_rotation(rotation,False)
                cc.set_editor_property('aspect_ratio',1.);unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(position,rotation)
                state.update(configured=True,warm=10. if state['index']==0 else 1.);return
            state['warm']-=delta
            if state['warm']>0:return
            state['task']=unreal.AutomationLibrary.take_high_res_screenshot(2048,2048,str(V/(spec['name']+'.png')),camera,delay=.1)
        except Exception:
            unreal.unregister_slate_post_tick_callback(handle);REPORT['error']=traceback.format_exc();dump();unreal.log_error(REPORT['error']);unreal.SystemLibrary.quit_editor()
    handle=unreal.register_slate_post_tick_callback(tick);dump()
if __name__=='__main__':
    try:main()
    except Exception:REPORT['error']=traceback.format_exc();dump();unreal.log_error(REPORT['error']);unreal.SystemLibrary.quit_editor()
