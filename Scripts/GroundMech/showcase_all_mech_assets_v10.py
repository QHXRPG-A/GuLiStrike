"""Read the saved batch in a fresh rendered editor, build its gallery and capture it.

Only the owned gallery map and floor material are saved. Production meshes,
animation assets and gameplay Blueprints are read without modification.
"""
import unreal,json,math,time,traceback
from pathlib import Path

OUT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/UE_AllAssets_v10')
BASE='/Game/GuLiStrike/Mechs/StyleShowcase'
MAP=BASE+'/LVL_MechAsset_Showcase'
OWNER='GuLi.MechAllAssets.v10'
GROUND='/Game/GuLiStrike/GroundMech/BP_GroundMech_Light'
SPIDER='/Game/GuLiStrike/Mechs/SpiderMech/BP_SpiderMech_Styled'
REPORT={'success':False,'map':MAP,'assets':{},'poses':[],'captures':[]}

def checkpoint():
    (OUT/'ue_showcase.json').write_text(json.dumps(REPORT,indent=2,ensure_ascii=False),encoding='utf-8')

def main():
    assert '-MechAllAssetsPreviewWorker' in unreal.SystemLibrary.get_command_line()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    lib=unreal.EditorAssetLibrary
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    exists=lib.does_asset_exist(MAP)
    if exists:assert level.load_level(MAP)
    else:assert level.new_level(MAP)
    world=editor.get_editor_world()
    if exists:assert lib.get_metadata_tag(world,'GuLi.StyleOwner')==OWNER
    lib.set_metadata_tag(world,'GuLi.StyleOwner',OWNER)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label().startswith('MSS_'):actors.destroy_actor(actor)
    def spawn(cls,label,location=(0,0,0),rotation=unreal.Rotator(),transient=False):
        a=actors.spawn_actor_from_class(cls,unreal.Vector(*location),rotation,transient)
        a.set_actor_label('MSS_'+label)
        return a
    def skeletal(a):return a.get_component_by_class(unreal.SkeletalMeshComponent)
    def freeze(comp,animation,seconds=0):
        comp.set_animation_mode(unreal.AnimationMode.ANIMATION_BLUEPRINT)
        comp.override_animation_data(animation,False,False,seconds,0)
        comp.set_update_animation_in_editor(True)
        comp.set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
    def mesh_bounds(a):
        boxes=[]
        if a.get_actor_label()=='MSS_Mech_Lightest':
            components=[a.get_editor_property(p) for p in ('mesh','armor','shoulder','machinegun')]
        elif a.get_actor_label()=='MSS_Missile_01':components=[a.static_mesh_component]
        else:components=[skeletal(a)]
        for comp in components:
            o,e,_=unreal.SystemLibrary.get_component_bounds(comp)
            boxes.append((o-e,o+e))
        low=unreal.Vector(*(min(v[0].to_tuple()[i] for v in boxes) for i in range(3)))
        high=unreal.Vector(*(max(v[1].to_tuple()[i] for v in boxes) for i in range(3)))
        return (low+high)*.5,(high-low)*.5
    def seat(a):
        center,extent=mesh_bounds(a)
        a.set_actor_location(a.get_actor_location()+unreal.Vector(0,0,2-center.z+extent.z),False,False)
    imported=json.loads((OUT/'ue_import.json').read_text(encoding='utf-8'))['parts']
    old=json.loads((OUT.parent/'UE_StyleSync_v9/ue_import.json').read_text(encoding='utf-8'))['parts']
    ground=unreal.load_asset(GROUND)
    abp=unreal.load_asset('/Game/GuLiStrike/GroundMech/Animations/ABP_GroundMech')
    cdo=unreal.get_default_object(ground.generated_class())
    instance=unreal.new_object(abp.generated_class(),outer=cdo.mesh)
    assert instance.get_editor_property('root_motion_mode')==unreal.RootMotionMode.IGNORE_ROOT_MOTION
    for key,prop in [('Legs','mesh'),('Armor','armor'),('Shoulder','shoulder'),('Machinegun','machinegun')]:
        comp=cdo.get_editor_property(prop)
        mesh=comp.get_skinned_asset() if key in ('Legs','Machinegun') else comp.static_mesh
        assert mesh.get_path_name().split('.')[0]==old[key]['path']
    REPORT['ground_root_motion_before_compile']=str(instance.get_editor_property('root_motion_mode'))
    REPORT['production_blueprints_compiled']=[]
    for path in [GROUND,abp.get_path_name(),SPIDER]+[v['blueprint'] for v in imported.values()]:
        assert unreal.BlueprintService.compile_blueprint(path)
        REPORT['production_blueprints_compiled'].append(path)
    subjects={}
    placements={'Mecha_01':(-1250,0,0),'Mecha_02':(-650,0,0),'Mech_Lightest':(100,0,380),'SpiderMech':(1100,0,190),
                'FireWeapon_01':(-1150,800,0),'MissileWeapon_01':(-450,800,0),'Machinegun_lvl1':(300,800,0),'Missile_01':(1000,800,0)}
    animation_pairs={}
    for name,row in imported.items():
        bp=unreal.load_asset(row['blueprint'])
        a=spawn(bp.generated_class(),name,placements[name]);subjects[name]=a
        comp=skeletal(a) if 'bones' in row else a.static_mesh_component
        mesh=comp.get_skinned_asset() if 'bones' in row else comp.static_mesh
        assert mesh.get_path_name().split('.')[0]==row['path']
        assert comp.get_num_materials()==row['material_slots']
        sockets=[]
        if 'bones' in row:
            original=unreal.load_asset(row['source'])
            assert mesh.skeleton==original.skeleton
            source_sockets=unreal.SkeletonService.list_sockets(original.get_path_name())
            actual={str(s.socket_name):s for s in unreal.SkeletonService.list_sockets(mesh.get_path_name())}
            for s in source_sockets:
                t=actual[str(s.socket_name)]
                assert s.bone_name==t.bone_name and (s.relative_location-t.relative_location).length()<.01
                assert (s.relative_scale-t.relative_scale).length()<.001
                sockets.append(str(s.socket_name))
        if row['idle_animation']:
            idle=unreal.load_asset(row['idle_animation'])
            freeze(comp,idle)
            src=spawn(unreal.SkeletalMeshActor,name+'_SourcePose',(-6000,-6000,0),transient=True)
            original_comp=skeletal(src);original_comp.set_skeletal_mesh_asset(original)
            src.set_is_temporarily_hidden_in_editor(True)
            freeze(original_comp,idle)
            animation_pairs[name]=(comp,original_comp,unreal.SkeletonService.list_bones(row['path']))
        seat(a)
        REPORT['assets'][name]={'mesh':mesh.get_path_name(),'material_slots':comp.get_num_materials(),'sockets':sockets,'saved_blueprint_reference_matches':True}
    light=spawn(ground.generated_class(),'Mech_Lightest',placements['Mech_Lightest'],unreal.Rotator(yaw=90))
    light.get_editor_property('upper_body_pivot').set_world_rotation(unreal.Rotator(),False,False)
    light.mesh.set_update_animation_in_editor(True);subjects['Mech_Lightest']=light
    seat(light)
    REPORT['assets']['Mech_Lightest']={'blueprint':GROUND,'four_component_references_match_v9':True,'actor_scale':list(light.get_actor_scale3d().to_tuple())}
    sp=spawn(lib.load_blueprint_class(SPIDER),'SpiderMech',placements['SpiderMech'],unreal.Rotator(yaw=90))
    spcomp=skeletal(sp)
    assert spcomp.get_skinned_asset().get_path_name().split('.')[0]==old['SpiderMech']['path']
    assert spcomp.get_num_materials()==11
    freeze(spcomp,unreal.load_asset('/Game/Assets/Mech_Project/Characters/SpiderMech/Animations/SpiderMech_Idle'))
    subjects['SpiderMech']=sp;seat(sp)
    REPORT['assets']['SpiderMech']={'blueprint':SPIDER,'mesh':spcomp.get_skinned_asset().get_path_name(),'material_slots':11,'showcase_pose':'original idle; instance override only'}
    gun=spawn(unreal.SkeletalMeshActor,'Machinegun_lvl1',placements['Machinegun_lvl1'])
    skeletal(gun).set_skeletal_mesh_asset(unreal.load_asset(old['Machinegun']['path']))
    subjects['Machinegun_lvl1']=gun;seat(gun)
    REPORT['assets']['Machinegun_lvl1']={'mesh':old['Machinegun']['path'],'also_installed_on_ground_player':True}
    # The gallery uses an ordinary neutral studio floor; all model materials remain unchanged.
    floorpath=BASE+'/M_ShowcaseFloor'
    if lib.does_asset_exist(floorpath):
        floor_mat=unreal.load_asset(floorpath);assert lib.get_metadata_tag(floor_mat,'GuLi.StyleOwner')==OWNER
    else:
        floor_mat=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_ShowcaseFloor',BASE,unreal.Material,unreal.MaterialFactoryNew())
        edit=unreal.MaterialEditingLibrary
        n=edit.create_material_expression(floor_mat,unreal.MaterialExpressionConstant3Vector)
        n.set_editor_property('constant',unreal.LinearColor(.11,.13,.16,1))
        assert edit.connect_material_property(n,'',unreal.MaterialProperty.MP_BASE_COLOR)
        n=edit.create_material_expression(floor_mat,unreal.MaterialExpressionConstant);n.r=.85
        assert edit.connect_material_property(n,'',unreal.MaterialProperty.MP_ROUGHNESS)
        edit.recompile_material(floor_mat);lib.set_metadata_tag(floor_mat,'GuLi.StyleOwner',OWNER)
        assert lib.save_loaded_asset(floor_mat,False)
    floor=spawn(unreal.StaticMeshActor,'Floor',(0,0,-10))
    floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    floor.set_actor_scale3d(unreal.Vector(300,300,.2));floor.static_mesh_component.set_material(0,floor_mat)
    for i,(rotation,intensity,color) in enumerate([((-45,125,0),12,(1,.94,.85)),((-25,-40,0),5,(.78,.88,1))]):
        a=spawn(unreal.DirectionalLight,'Light_'+str(i),rotation=unreal.Rotator(*rotation))
        c=a.get_component_by_class(unreal.DirectionalLightComponent);c.set_mobility(unreal.ComponentMobility.MOVABLE)
        c.set_intensity(intensity);c.set_light_color(unreal.LinearColor(*color,1))
    sky=spawn(unreal.SkyLight,'AmbientSky').get_component_by_class(unreal.SkyLightComponent)
    sky.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
    sky.set_intensity(2)
    camera=spawn(unreal.CameraActor,'GalleryCamera')
    cc=camera.get_component_by_class(unreal.CameraComponent)
    pp=unreal.PostProcessSettings()
    for k,v in dict(override_auto_exposure_method=True,auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
        override_auto_exposure_apply_physical_camera_exposure=True,auto_exposure_apply_physical_camera_exposure=False,
        override_auto_exposure_bias=True,auto_exposure_bias=0,override_motion_blur_amount=True,motion_blur_amount=0,
        override_bloom_intensity=True,bloom_intensity=.1).items():pp.set_editor_property(k,v)
    cc.set_editor_property('post_process_settings',pp);cc.set_editor_property('post_process_blend_weight',1)
    world.get_world_settings().set_editor_property('default_game_mode',unreal.GameModeBase)
    actors.set_selected_level_actors([])
    unreal.ViewportService.set_game_view(True);unreal.ViewportService.set_view_mode('lit')
    unreal.ViewportService.set_exposure(True,-1);unreal.ViewportService.set_realtime(True)
    for command in ['r.ScreenPercentage 100','r.Streaming.FullyLoadUsedTextures 1','r.Streaming.PoolSize 2500','r.AntiAliasingMethod 2','r.PostProcessAAQuality 6']:
        unreal.SystemLibrary.execute_console_command(world,command)
    captures=[('UE_All_Assets.png',None,None,0)]
    for name in ['Mecha_01','Mecha_02','Mech_Lightest','SpiderMech','FireWeapon_01','MissileWeapon_01','Machinegun_lvl1','Missile_01']:
        captures.append(('UE_'+name+'.png',name,'Idle',0))
    for name in animation_pairs:captures.append(('UE_'+name+'_Walk.png',name,'Walk',.45))
    state={'index':0,'configured':False,'warm':0,'task':None,'started':time.monotonic()}
    def configure(subject,pose,seconds):
        for name,a in subjects.items():a.set_is_temporarily_hidden_in_editor(subject is not None and name!=subject)
        if subject in animation_pairs:
            c,src,_=animation_pairs[subject]
            animation=unreal.load_asset(imported[subject]['idle_animation'].split('.')[0].replace('_Idle','_'+pose))
            freeze(c,animation,seconds);freeze(src,animation,seconds)
        if subject:
            center,extent=mesh_bounds(subjects[subject]);span=max(extent.to_tuple())*2
            position=center+unreal.Vector(.95,1.7,.85)*span*1.25
            cc.set_field_of_view(40);cc.set_editor_property('aspect_ratio',1.5)
        else:
            center=unreal.Vector(0,300,280);position=center+unreal.Vector(1350,4300,2550)
            cc.set_field_of_view(45);cc.set_editor_property('aspect_ratio',2)
        rotation=unreal.MathLibrary.find_look_at_rotation(position,center)
        camera.set_actor_location(position,False,False);camera.set_actor_rotation(rotation,False)
        editor.set_level_viewport_camera_info(position,rotation)
    def pose_record(name,pose,seconds):
        comp,original,bones=animation_pairs[name]
        scales=[];translations=[];angles=[];sample={}
        for b in bones:
            a=comp.get_socket_transform(b.bone_name,unreal.RelativeTransformSpace.RTS_COMPONENT)
            z=original.get_socket_transform(b.bone_name,unreal.RelativeTransformSpace.RTS_COMPONENT)
            scales.append((a.scale3d-z.scale3d).length());translations.append((a.translation-z.translation).length())
            q,r=a.rotation,z.rotation
            angles.append(math.degrees(2*math.acos(min(1,abs(q.x*r.x+q.y*r.y+q.z*r.z+q.w*r.w)))))
            sample[str(b.bone_name)]={'position_cm':list(a.translation.to_tuple()),'scale':list(a.scale3d.to_tuple()),'rotation_xyzw':[getattr(q,k) for k in ('x','y','z','w')]}
        assert max(scales)<.001 and max(translations)<.02 and max(angles)<.1
        REPORT['poses'].append({'asset':name,'pose':pose,'seconds':seconds,'evaluated_seconds':comp.get_position(),
            'source_pose_max_translation_error_cm':max(translations),'source_pose_max_scale_error':max(scales),
            'source_pose_max_angle_error_deg':max(angles),'bones':sample})
    def finish():
        for name,(c,src,_) in animation_pairs.items():
            freeze(c,unreal.load_asset(imported[name]['idle_animation']),0)
            c.set_update_animation_in_editor(False);actors.destroy_actor(src.get_owner())
            poses=[p for p in REPORT['poses'] if p['asset']==name]
            delta=max((unreal.Vector(*poses[0]['bones'][b]['position_cm'])-unreal.Vector(*poses[1]['bones'][b]['position_cm'])).length() for b in poses[0]['bones'])
            assert delta>1,(name,'walk not evaluated')
            REPORT['assets'][name]['idle_to_walk_max_bone_motion_cm']=delta
        spcomp.set_update_animation_in_editor(False)
        configure(None,None,0)
        assert level.save_current_level()
        REPORT.update(success=True,stage='complete',all_eight_saved=True,unique_production_meshes=10,
            validation='saved references, 8 Blueprint compiles, sockets, original idle/walk bone poses, rendered gallery; no gameplay or performance rerun')
        checkpoint();unreal.SystemLibrary.quit_editor()
    def tick(delta):
        try:
            assert time.monotonic()-state['started']<240,'Gallery capture timeout'
            if state['task']:
                if not state['task'].is_task_done():return
                filename=captures[state['index']][0];assert (OUT/filename).is_file()
                REPORT['captures'].append(filename);checkpoint()
                state.update(index=state['index']+1,configured=False,task=None)
            if state['index']==len(captures):
                unreal.unregister_slate_post_tick_callback(handle);finish();return
            filename,subject,pose,seconds=captures[state['index']]
            if not state['configured']:
                configure(subject,pose,seconds);state.update(configured=True,warm=8 if state['index']==0 else 1.5);return
            state['warm']-=delta
            if state['warm']>0:return
            if subject in animation_pairs:pose_record(subject,pose,seconds)
            state['task']=unreal.AutomationLibrary.take_high_res_screenshot(2400 if subject is None else 1800,1200,str(OUT/filename),camera,delay=1)
        except Exception:
            unreal.unregister_slate_post_tick_callback(handle)
            REPORT['error']=traceback.format_exc();checkpoint();unreal.log_error(REPORT['error']);unreal.SystemLibrary.quit_editor()
    checkpoint()
    handle=unreal.register_slate_post_tick_callback(tick)

if __name__=='__main__':
    try:main()
    except Exception:
        REPORT['error']=traceback.format_exc();checkpoint();unreal.log_error(REPORT['error']);unreal.SystemLibrary.quit_editor()
