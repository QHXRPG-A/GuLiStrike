"""Rendered studio evidence, scoped to a new review map; no gameplay map edits."""
import unreal,json,time,traceback
from pathlib import Path
ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/StylePass_20260917'
BASE='/Game/Commander/Units/Tactical/Cel/Review';MAP=BASE+'/LVL_CelModels_Review'
LIB=unreal.EditorAssetLibrary;LEVEL=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
OWNER='GuLi.CelPass.20260917'

def main():
    assert '-CelPreviewWorker' in unreal.SystemLibrary.get_command_line()
    assert LEVEL.load_level(MAP) if LIB.does_asset_exist(MAP) else LEVEL.new_level(MAP)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in actors.get_all_level_actors():
        if a.get_actor_label().startswith('CelReview_'):actors.destroy_actor(a)
    def spawn(cls,name,loc=(0,0,0),rot=(0,0,0)):
        a=actors.spawn_actor_from_class(cls,unreal.Vector(*loc),unreal.Rotator(*rot));a.set_actor_label('CelReview_'+name);return a
    floor_mat=unreal.load_asset('/Game/Commander/Units/Tactical/Preview/Sweeper/M_ReviewFloor')
    floor=spawn(unreal.StaticMeshActor,'Floor',(0,0,-20));floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'));floor.static_mesh_component.set_material(0,floor_mat);floor.set_actor_scale3d(unreal.Vector(2000,2000,.3))
    for i,(rot,power) in enumerate([((-45,130,0),7),((-25,-60,0),3)]):
        light=spawn(unreal.DirectionalLight,'Light'+str(i),rot=rot).get_component_by_class(unreal.DirectionalLightComponent);light.set_mobility(unreal.ComponentMobility.MOVABLE);light.set_intensity(power)
    sky=spawn(unreal.SkyLight,'Ambient').get_component_by_class(unreal.SkyLightComponent);sky.set_mobility(unreal.ComponentMobility.MOVABLE);sky.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP);sky.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'));sky.set_intensity(1.2)
    camera=spawn(unreal.CameraActor,'Camera');cc=camera.get_component_by_class(unreal.CameraComponent);cc.set_field_of_view(32);cc.set_editor_property('aspect_ratio',1.5)
    pp=unreal.PostProcessSettings()
    for k,v in dict(override_auto_exposure_method=True,auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,override_auto_exposure_apply_physical_camera_exposure=True,auto_exposure_apply_physical_camera_exposure=False,override_auto_exposure_bias=True,auto_exposure_bias=0,override_motion_blur_amount=True,motion_blur_amount=0,override_bloom_intensity=True,bloom_intensity=.12).items():pp.set_editor_property(k,v)
    cc.set_editor_property('post_process_settings',pp);cc.set_editor_property('post_process_blend_weight',1.)
    world.get_world_settings().set_editor_property('default_game_mode',unreal.GameModeBase)
    tasks=[];report={'success':False,'map':MAP,'subjects':[],'images':[]}
    for index,unit in enumerate(('Sweeper','WarMachine','Ship')):
        path='/Game/Commander/Units/Tactical/Cel/'+unit+'/Meshes/SM_'+unit+'_Cel' if unit!='Ship' else '/Game/GuLiStrike/Ship/Stylized/Meshes/SM_Ship_AnimeHull'
        mesh=unreal.load_asset(path);assert mesh,path
        bounds=mesh.get_bounds();span=max((bounds.box_extent*2).to_tuple());s=2000/span
        center=unreal.Vector((index-1)*6000,0,0)
        loc=center-bounds.origin*s+unreal.Vector(0,0,bounds.box_extent.z*s+15)
        ob=spawn(unreal.StaticMeshActor,unit,loc.to_tuple());ob.static_mesh_component.set_static_mesh(mesh);ob.set_actor_scale3d(unreal.Vector(s,s,s))
        if unit=='Ship':
            edge=spawn(unreal.StaticMeshActor,'ShipContour',loc.to_tuple());edge.static_mesh_component.set_static_mesh(unreal.load_asset('/Game/GuLiStrike/Ship/Stylized/Meshes/SM_Ship_AnimeContour'));edge.set_actor_scale3d(unreal.Vector(s,s,s));edge.static_mesh_component.set_cast_shadow(False)
        target=center+unreal.Vector(0,0,bounds.box_extent.z*s+15)
        report['subjects'].append({'unit':unit,'asset':path,'review_scale':s,'dimensions_cm':(bounds.box_extent*2).to_tuple()})
        views=[('three_quarter',(1.9,-2,1.45)),('front',(2.9,0,.85)),('side',(0,-2.9,.8))]
        if unit=='Ship':views=[('three_quarter',(1.5,2,1.7)),('front',(0,2.9,.85)),('side',(2.9,0,.8))]
        for view,v in views:tasks.append((unit+'_'+view,target+unreal.Vector(*v)*2400,target))
    actors.set_selected_level_actors([]);unreal.ViewportService.set_view_mode('lit');unreal.ViewportService.set_realtime(True)
    for cmd in ['r.ScreenPercentage 100','r.Streaming.FullyLoadUsedTextures 1','r.AntiAliasingMethod 2']:unreal.SystemLibrary.execute_console_command(world,cmd)
    LIB.set_metadata_tag(world,'GuLi.ModelProduction.Owner',OWNER);assert LEVEL.save_current_level()
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    state={'start':time.monotonic(),'last':time.monotonic(),'i':0,'phase':0,'shot':None}
    def tick(dt):
        try:
            if time.monotonic()-state['start']>240:raise RuntimeError('Preview timeout')
            if time.monotonic()-state['start']<15:return
            if state['i']>=len(tasks):
                report['success']=True;(OUT/'cel_ue_preview.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor();return
            name,pos,target=tasks[state['i']]
            if state['phase']==0:
                rot=unreal.MathLibrary.find_look_at_rotation(pos,target);camera.set_actor_location(pos,False,False);camera.set_actor_rotation(rot,False);unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(pos,rot);state.update(phase=1,last=time.monotonic());return
            if state['phase']==1 and time.monotonic()-state['last']>2:
                state['shot']=unreal.AutomationLibrary.take_high_res_screenshot(1500,1000,str(OUT/('UE_'+name+'.png')),camera,delay=1.);state['phase']=2;return
            if state['phase']==2 and state['shot'].is_task_done():
                report['images'].append(str(OUT/('UE_'+name+'.png')));state.update(i=state['i']+1,phase=0)
        except Exception:
            report['error']=traceback.format_exc();(OUT/'cel_ue_preview.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
    handle=unreal.register_slate_post_tick_callback(tick)

if __name__=='__main__':
    try:main()
    except Exception:
        (OUT/'cel_ue_preview_error.txt').write_text(traceback.format_exc(),encoding='utf8');unreal.SystemLibrary.quit_editor()
