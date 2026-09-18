"""Actual UE screenshots and evaluated 7-turret motion on formal meshes."""
import json
import math
import sys
import time
import traceback
from pathlib import Path
import unreal

PROJECT=Path('D:/UE5.7/test1')
ROOT=PROJECT/'ArtSource/Ships/ShipComponentStyle_20260917'
OUT=ROOT/'UE_Integration'
PRE=OUT/'Previews'
PRE.mkdir(exist_ok=True)
BASE='/Game/GuLiStrike/Ship/StylizedComponents'
MAP=BASE+'/Review/L_ShipComponents_Style'
LIB=unreal.EditorAssetLibrary
LEVEL=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
sys.path.insert(0,str(PROJECT/'Scripts'))
import author_ship_component_rigs as rigs
import preview_ship_component_rigs as existing_motion
existing_motion.TEMP=BASE+'/Review/Animations'
REPORT={'success':False,'map':MAP,'pictures':[],'motion':{},'assembly':[]}

def write():
    (OUT/'UE_visual_motion_report.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
def spawn(actors,cls,label,loc=(0,0,0),rot=(0,0,0)):
    obj=actors.spawn_actor_from_class(cls,unreal.Vector(*loc),unreal.Rotator(*rot));assert obj,label
    obj.set_actor_label('SCStyle_'+label);return obj

def run():
    assert '-ShipComponentStylePreviewWorker' in unreal.SystemLibrary.get_command_line()
    still_refresh='-ShipComponentStyleStillRefresh' in unreal.SystemLibrary.get_command_line()
    previous=json.loads((OUT/'UE_visual_motion_report.json').read_text(encoding='utf-8')) if still_refresh else None
    if previous:
        assert previous['success']
        REPORT['motion']=previous['motion']
        REPORT['assembly']=previous['assembly']
        for row in REPORT['assembly']:
            if row.get('installed') is False and row['slots'] and row['slots'][0]=='bottom_mid_0':
                row['reason']='Existing gameplay hull has no bottom_mid_0 socket; confirmed by the original install function warning. No hull or compatibility socket changes in this surface task.'
    imported=json.loads((OUT/'formal_import_report.json').read_text(encoding='utf-8'))
    assert imported['success'] and len(imported['parts'])==13
    assert LEVEL.load_level(MAP) if LIB.does_asset_exist(MAP) else LEVEL.new_level(MAP)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for obj in actors.get_all_level_actors():
        if obj.get_actor_label().startswith('SCStyle_'):actors.destroy_actor(obj)
    floor=spawn(actors,unreal.StaticMeshActor,'Floor',(0,0,-20))
    floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    floor.static_mesh_component.set_material(0,unreal.load_asset('/Game/Commander/Units/Tactical/Preview/Sweeper/M_ReviewFloor'))
    floor.set_actor_scale3d(unreal.Vector(500,500,.3))
    background_path=BASE+'/Review/M_StudioBackground'
    background=unreal.load_asset(background_path)
    if not background:
        background=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_StudioBackground',BASE+'/Review',unreal.Material,unreal.MaterialFactoryNew())
        background.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
        background.set_editor_property('two_sided',True)
        node=unreal.MaterialEditingLibrary.create_material_expression(background,unreal.MaterialExpressionConstant3Vector)
        node.set_editor_property('constant',unreal.LinearColor(.012,.022,.033,1))
        unreal.MaterialEditingLibrary.connect_material_property(node,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        unreal.MaterialEditingLibrary.recompile_material(background);assert LIB.save_loaded_asset(background,False)
    dome=spawn(actors,unreal.StaticMeshActor,'Backdrop',(5700,6300,0))
    dome.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Sphere'))
    dome.static_mesh_component.set_material(0,background);dome.static_mesh_component.set_cast_shadow(False)
    dome.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    dome.set_actor_scale3d(unreal.Vector(4000,4000,4000))
    light=spawn(actors,unreal.DirectionalLight,'Key',rot=(-48,130,0)).get_component_by_class(unreal.DirectionalLightComponent)
    light.set_mobility(unreal.ComponentMobility.MOVABLE);light.set_intensity(5.)
    sky=spawn(actors,unreal.SkyLight,'Sky').get_component_by_class(unreal.SkyLightComponent)
    sky.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
    sky.set_intensity(.8)
    camera=spawn(actors,unreal.CameraActor,'Camera')
    cc=camera.get_component_by_class(unreal.CameraComponent)
    cc.set_field_of_view(30);cc.set_editor_property('aspect_ratio',4/3)
    pp=unreal.PostProcessSettings()
    for k,v in dict(override_auto_exposure_method=True,auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
        override_auto_exposure_apply_physical_camera_exposure=True,auto_exposure_apply_physical_camera_exposure=False,
        override_auto_exposure_bias=True,auto_exposure_bias=0.,override_motion_blur_amount=True,motion_blur_amount=0.,
        override_bloom_intensity=True,bloom_intensity=0.).items():pp.set_editor_property(k,v)
    cc.set_editor_property('post_process_settings',pp);cc.set_editor_property('post_process_blend_weight',1.)
    subjects={};animations={};shots=[]
    for i,(key,row) in enumerate(imported['parts'].items()):
        mesh=unreal.load_asset(row['mesh']);sk=isinstance(mesh,unreal.SkeletalMesh)
        bounds=mesh.get_imported_bounds() if sk else mesh.get_bounds()
        extent=max((bounds.box_extent*2).to_tuple());scale=1600/extent
        center=unreal.Vector((i%4)*3800,(i//4)*4200,0)
        location=center-bounds.origin*scale+unreal.Vector(0,0,bounds.box_extent.z*scale+12)
        actor=spawn(actors,unreal.SkeletalMeshActor if sk else unreal.StaticMeshActor,key,location.to_tuple())
        actor.set_actor_scale3d(unreal.Vector(scale,scale,scale))
        component=actor.get_component_by_class(unreal.SkeletalMeshComponent if sk else unreal.StaticMeshComponent)
        component.set_mobility(unreal.ComponentMobility.MOVABLE)
        if sk:
            component.set_skeletal_mesh_asset(mesh)
            anim_path=existing_motion.TEMP+'/A_'+key
            anim=unreal.load_asset(anim_path) if LIB.does_asset_exist(anim_path) else existing_motion.make_animation(key,mesh)
            assert LIB.save_loaded_asset(anim,False)
            animations[key]=anim
            component.set_update_animation_in_editor(True)
            component.play_animation(anim,False);component.set_play_rate(0);component.set_position(.5,False)
            if not still_refresh:REPORT['motion'][key]={'samples':0,'socket_samples':0,'max_socket_error_cm':0.,'max_angle_error_deg':0.,'root_stable':True}
        else:component.set_static_mesh(mesh)
        target=center+unreal.Vector(0,0,bounds.box_extent.z*scale+12)
        expected_path=(ROOT/row['source_fbx']).parent.parent/'Validation'/f'{key}_export_expected.json'
        expected=json.loads(expected_path.read_text(encoding='utf-8'))
        subjects[key]={'actor':actor,'component':component,'mesh':mesh,'target':target,'scale':scale,
            'points':[unreal.Vector(p[0]*100,-p[1]*100,p[2]*100) for p in expected['points']],
            'owners':expected['rigid_owners']}
        shots.append({'name':key+'_hero','key':key,'angle':0,'direction':(1.1,1.7,1.45)})
        if key=='Incendiary_Bomb_LaunchBay':
            shots.extend({'name':key+'_'+name,'key':key,'angle':0,'direction':direction} for name,direction in [('flame_front',(0,2.6,.2)),('flame_rear',(0,-2.6,.2))])
        if key=='Drone_LaunchBay':shots.append({'name':key+'_exterior_mark','key':key,'angle':0,'direction':(-2.2,1.5,1.4)})
        if sk:
            for angle in (-15,30,75):shots.append({'name':key+'_pitch_'+str(angle),'key':key,'angle':angle,'direction':(1.1,1.7,1.0)})
    # Exercise the actual formal component Blueprints through the ship's own
    # installation function wherever the catalogue defines an installation slot.
    ship_class=unreal.load_asset('/Game/GuLiStrike/Ship/BP_CombatAvatarFly01').generated_class()
    ship=spawn(actors,ship_class,'AssemblyProbe',(-16000,0,7000))
    for row in ([] if still_refresh else imported['blueprints']):
        bp=unreal.load_asset(row['path']);cls=bp.generated_class();cdo=unreal.get_default_object(cls)
        allowed=[str(n) for n in cdo.get_editor_property('compatible_sockets')]
        rec={'blueprint':row['path'],'slots':allowed}
        if allowed:
            slot=allowed[0]
            if ship.get_part_at(slot):ship.uninstall_part(slot)
            installed=ship.install_part(cls,slot)
            rec['installed']=installed
            if installed:
                part=ship.get_part_at(slot);visual=part.get_visual_mesh_component()
                rec['visual_ready']=bool(visual)
                assert visual,row['path']
                assigned=visual.get_skeletal_mesh_asset() if isinstance(visual,unreal.SkeletalMeshComponent) else visual.static_mesh
                assert assigned.get_path_name().split('.')[0]==row['formal_mesh']
                rec['formal_mesh']=assigned.get_path_name()
                rec['material']=visual.get_material(0).get_path_name()
                assert '/StylizedComponents/Materials/' in rec['material']
                ship.uninstall_part(slot)
        else:rec['status']='catalogue has no assigned compatible socket; direct formal mesh and CDO checked'
        REPORT['assembly'].append(rec)
    actors.destroy_actor(ship)
    world.get_world_settings().set_editor_property('default_game_mode',unreal.GameModeBase)
    actors.set_selected_level_actors([])
    LEVEL.editor_set_viewport_realtime(True)
    for command in ('r.ScreenPercentage 100','r.Streaming.FullyLoadUsedTextures 1','r.AntiAliasingMethod 2','r.VSync 0'):
        unreal.SystemLibrary.execute_console_command(world,command)
    gallery=unreal.Vector(5700,6300,600)
    gallery_pos=gallery+unreal.Vector(1.1,1.5,1.4)*11500
    gallery_rot=unreal.MathLibrary.find_look_at_rotation(gallery_pos,gallery)
    camera.set_actor_location(gallery_pos,False,False);camera.set_actor_rotation(gallery_rot,False)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(gallery_pos,gallery_rot)
    assert LEVEL.save_current_level()
    sweep=[(key,angle) for key in animations for angle in range(-15,76)]
    state={'start':time.monotonic(),'phase':'warm','index':0,'configured':False,'ticks':0,'shot':None,'last':0}
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    def finish(error=None):
        if error:REPORT['error']=error
        else:REPORT['success']=True
        write();unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
    def tick(delta):
        try:
            if time.monotonic()-state['start']>900:raise RuntimeError('UE style preview timeout')
            if state['phase']=='warm':
                if time.monotonic()-state['start']<25:return
                state.update(phase='camera' if still_refresh else 'motion',index=0)
            if state['phase']=='motion':
                if state['index']==len(sweep):
                    for subject in subjects.values():
                        if isinstance(subject['component'],unreal.SkeletalMeshComponent):subject['component'].set_position(.5,False)
                    write();state.update(phase='camera',index=0);return
                key,angle=sweep[state['index']];subject=subjects[key];component=subject['component'];mesh=subject['mesh']
                if not state['configured']:
                    component.set_position((angle+15)/30,False);state.update(configured=True,ticks=0);return
                state['ticks']+=1
                if state['ticks']<2:return
                row=REPORT['motion'][key]
                root=component.get_socket_transform('Root',unreal.RelativeTransformSpace.RTS_COMPONENT)
                assert rigs.transforms_match(root,unreal.Transform()),(key,'root moved')
                bone=component.get_socket_transform('BarrelPitch',unreal.RelativeTransformSpace.RTS_COMPONENT)
                ref=unreal.SkeletonService.list_bones(mesh.get_path_name())[1].global_transform
                expected=unreal.Transform(rotation=unreal.Rotator(pitch=angle)).multiply(ref)
                dot=abs(sum(getattr(bone.rotation,c)*getattr(expected.rotation,c) for c in ('x','y','z','w')))
                err=math.degrees(2*math.acos(min(1,dot)))
                assert err<.3,(key,angle,err)
                row['max_angle_error_deg']=max(row['max_angle_error_deg'],err)
                for i in range(1,rigs.COUNTS[key]+1):
                    socket=mesh.find_socket('Socket_'+str(i))
                    desired=unreal.Transform(location=socket.relative_location,rotation=socket.relative_rotation,scale=socket.relative_scale).multiply(bone)
                    actual=component.get_socket_transform(socket.socket_name,unreal.RelativeTransformSpace.RTS_COMPONENT)
                    error=(actual.translation-desired.translation).length();assert error<.005,(key,angle,i,error)
                    row['max_socket_error_cm']=max(row['max_socket_error_cm'],error);row['socket_samples']+=1
                row['samples']+=1;state.update(index=state['index']+1,configured=False);return
            if state['index']==len(shots):finish();return
            task=shots[state['index']];subject=subjects[task['key']]
            if state['phase']=='camera':
                for name,entry in subjects.items():entry['component'].set_visibility(name==task['key'],True)
                if isinstance(subject['component'],unreal.SkeletalMeshComponent):subject['component'].set_position((task['angle']+15)/30,False)
                points=subject['points']
                if subject['owners']:
                    reference=unreal.SkeletonService.list_bones(subject['mesh'].get_path_name())[1].global_transform
                    posed=unreal.Transform(rotation=unreal.Rotator(pitch=task['angle'])).multiply(reference)
                    points=[posed.transform_location(reference.inverse_transform_location(p)) if owner=='BarrelPitch' else p for p,owner in zip(points,subject['owners'])]
                transform=subject['actor'].get_actor_transform()
                points=[transform.transform_location(p) for p in points]
                low=unreal.Vector(*[min(getattr(p,a) for p in points) for a in ('x','y','z')])
                high=unreal.Vector(*[max(getattr(p,a) for p in points) for a in ('x','y','z')])
                target=(low+high)*.5
                floor.set_actor_location(unreal.Vector(0,0,low.z-25),False,False)
                direction=unreal.Vector(*task['direction']).normal()
                # Fit the actual projected posed vertices with a real margin.
                look=unreal.MathLibrary.find_look_at_rotation(target+direction*100,target)
                right=unreal.MathLibrary.get_right_vector(look)
                up=unreal.MathLibrary.get_up_vector(look)
                def dot(a,b):return a.x*b.x+a.y*b.y+a.z*b.z
                projected_x=[dot(p-target,right) for p in points]
                projected_y=[dot(p-target,up) for p in points]
                width=max(projected_x)-min(projected_x);height=max(projected_y)-min(projected_y)
                # A perspective camera preserves the editor's lit view mode.
                # Fit each posed vertex including its depth, not just neutral
                # bounds, so bottom-mounted 75-degree poses remain uncropped.
                cc.set_editor_property('projection_mode',unreal.CameraProjectionMode.PERSPECTIVE)
                tan_h=math.tan(math.radians(30)/2);tan_v=tan_h/(4/3)
                distance=max(dot(p-target,direction)+max(abs(x)*1.18/tan_h,abs(y)*1.18/tan_v)
                    for p,x,y in zip(points,projected_x,projected_y))
                pos=target+direction*distance
                rot=unreal.MathLibrary.find_look_at_rotation(pos,target)
                camera.set_actor_location(pos,False,False);camera.set_actor_rotation(rot,False)
                unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(pos,rot)
                unreal.SystemLibrary.execute_console_command(world,'viewmode lit')
                state.update(phase='settle',last=time.monotonic());return
            if state['phase']=='settle' and time.monotonic()-state['last']>2:
                state['shot']=unreal.AutomationLibrary.take_high_res_screenshot(1440,1080,str(PRE/(task['name']+'.png')),camera,delay=1.)
                state['phase']='capture';return
            if state['phase']=='capture' and state['shot'].is_task_done():
                file=PRE/(task['name']+'.png');assert file.is_file() and file.stat().st_size>10000
                REPORT['pictures'].append(str(file.relative_to(OUT)));write()
                state.update(index=state['index']+1,phase='camera')
        except Exception:finish(traceback.format_exc())
    handle=unreal.register_slate_post_tick_callback(tick)

if __name__=='__main__':
    try:run()
    except Exception:
        REPORT['error']=traceback.format_exc();write();unreal.SystemLibrary.quit_editor()
