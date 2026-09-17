"""Owned studio map and rendered evidence for one imported tactical model."""
import unreal,json,time,traceback,re
from pathlib import Path

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'ArtSource/TacticalStyle_20260916/Production_Handbuilt'
OWNER='GuLiStrike.Tactical.Handbuilt.20260916'

def main():
    command=unreal.SystemLibrary.get_command_line()
    if '-TacticalPreviewWorker' not in command:raise RuntimeError('Separate rendered preview worker required')
    unit=re.search(r'-TacticalUnit=(Sweeper|WarMachine)',command).group(1)
    base='/Game/Commander/Units/Tactical/Preview/'+unit;map_path=base+'/LVL_'+unit+'_Review'
    lib=unreal.EditorAssetLibrary;level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if lib.does_asset_exist(map_path):
        if not level.load_level(map_path):raise RuntimeError('Could not open owned preview map')
    elif not level.new_level(map_path):raise RuntimeError('Could not create preview map')
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    owner=lib.get_metadata_tag(world,'GuLi.ModelProduction.Owner')
    if owner and owner!=OWNER:raise RuntimeError('Unowned preview map')
    lib.set_metadata_tag(world,'GuLi.ModelProduction.Owner',OWNER)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label().startswith('TacticalReview_'):actors.destroy_actor(actor)
    def spawn(cls,name,location=(0,0,0),rotation=(0,0,0)):
        actor=actors.spawn_actor_from_class(cls,unreal.Vector(*location),unreal.Rotator(*rotation));actor.set_actor_label('TacticalReview_'+name);return actor
    mesh=unreal.load_asset('/Game/Commander/Units/Tactical/'+unit+'/Meshes/SM_'+unit+'_Crowd')
    subject=spawn(unreal.StaticMeshActor,unit);subject.static_mesh_component.set_static_mesh(mesh)
    bounds=mesh.get_bounds();span=max((bounds.box_extent*2).to_tuple());height=bounds.box_extent.z*2
    floorpath=base+'/M_ReviewFloor';floor_mat=unreal.load_asset(floorpath)
    if floor_mat and lib.get_metadata_tag(floor_mat,'GuLi.ModelProduction.Owner')!=OWNER:raise RuntimeError('Unowned floor')
    if not floor_mat:
        floor_mat=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_ReviewFloor',base,unreal.Material,unreal.MaterialFactoryNew())
        edit=unreal.MaterialEditingLibrary
        color=edit.create_material_expression(floor_mat,unreal.MaterialExpressionConstant3Vector,-250,0);color.set_editor_property('constant',unreal.LinearColor(.17,.20,.24,1));edit.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
        rough=edit.create_material_expression(floor_mat,unreal.MaterialExpressionConstant,-250,150);rough.set_editor_property('r',.9);edit.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS);edit.recompile_material(floor_mat)
        lib.set_metadata_tag(floor_mat,'GuLi.ModelProduction.Owner',OWNER);lib.save_loaded_asset(floor_mat,False)
    floor=spawn(unreal.StaticMeshActor,'Floor',(0,0,-15));floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'));floor.static_mesh_component.set_material(0,floor_mat);floor.set_actor_scale3d(unreal.Vector(span*.1,span*.1,.25))
    for i,(rotation,power,color) in enumerate([((-45,130,0),9.,(1.,.96,.89)),((-20,-50,0),3.5,(.84,.91,1.))]):
        ob=spawn(unreal.DirectionalLight,'Light'+str(i),rotation=rotation);lc=ob.get_component_by_class(unreal.DirectionalLightComponent);lc.set_mobility(unreal.ComponentMobility.MOVABLE);lc.set_intensity(power);lc.set_light_color(unreal.LinearColor(*color,1))
    sky=spawn(unreal.SkyLight,'Ambient').get_component_by_class(unreal.SkyLightComponent);sky.set_mobility(unreal.ComponentMobility.MOVABLE);sky.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP);sky.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'));sky.set_intensity(1.5)
    camera=spawn(unreal.CameraActor,'Camera');cc=camera.get_component_by_class(unreal.CameraComponent);cc.set_field_of_view(36);cc.set_editor_property('aspect_ratio',1.5)
    pp=unreal.PostProcessSettings()
    for k,v in dict(override_auto_exposure_method=True,auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,override_auto_exposure_apply_physical_camera_exposure=True,auto_exposure_apply_physical_camera_exposure=False,override_auto_exposure_bias=True,auto_exposure_bias=0,override_motion_blur_amount=True,motion_blur_amount=0,override_bloom_intensity=True,bloom_intensity=.1).items():pp.set_editor_property(k,v)
    cc.set_editor_property('post_process_settings',pp);cc.set_editor_property('post_process_blend_weight',1.)
    world.get_world_settings().set_editor_property('default_game_mode',unreal.GameModeBase)
    target=unreal.Vector(0,0,height*.47);position=target+unreal.Vector(1.45,-2.,1.32)*span
    rotation=unreal.MathLibrary.find_look_at_rotation(position,target);camera.set_actor_location(position,False,False);camera.set_actor_rotation(rotation,False)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(position,rotation)
    actors.set_selected_level_actors([]);unreal.ViewportService.set_view_mode('lit');unreal.ViewportService.set_realtime(True);unreal.ViewportService.set_exposure(True,-1.)
    for cmd in ['r.ScreenPercentage 100','r.Streaming.FullyLoadUsedTextures 1','r.AntiAliasingMethod 2']:unreal.SystemLibrary.execute_console_command(world,cmd)
    if not level.save_current_level():raise RuntimeError('Preview map save failed')
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    state={'started':time.monotonic(),'task':None}
    def tick(delta):
        try:
            if time.monotonic()-state['started']>160:raise RuntimeError('Preview screenshot timeout')
            if time.monotonic()-state['started']<12:return
            if state['task'] is None:
                state['task']=unreal.AutomationLibrary.take_high_res_screenshot(1800,1200,str(OUT/('UE_'+unit+'_Review.png')),camera,delay=2.);return
            if not state['task'].is_task_done():return
            image=OUT/('UE_'+unit+'_Review.png')
            result={'success':image.is_file(),'unit':unit,'map':map_path,'mesh':mesh.get_path_name(),'image':str(image),'dimensions_cm':list((bounds.box_extent*2).to_tuple())}
            (OUT/(unit+'_ue_preview.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
            unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
        except Exception:
            (OUT/(unit+'_ue_preview_error.txt')).write_text(traceback.format_exc(),encoding='utf8');unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
    handle=unreal.register_slate_post_tick_callback(tick)

if __name__=='__main__':
    try:main()
    except Exception:
        (OUT/'tactical_preview_error.txt').write_text(traceback.format_exc(),encoding='utf8');unreal.SystemLibrary.quit_editor()
