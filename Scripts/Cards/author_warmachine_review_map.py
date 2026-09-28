"""Save/read back only the independent War Machine card review map."""
import json
import traceback
from pathlib import Path
import unreal

ROOT='/Game/GuLiStrike/Cards/WarMachineTarot'
DEMO='/Game/GuLiStrike/Cards/RevealDemo'
MAP=ROOT+'/Maps/LVL_WarMachineTarotReview'
OUT=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/Inspection')
report={'success':False,'map':MAP}
try:
    assert not unreal.WidgetService.is_pie_running(), 'PIE is active'
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Current map has unsaved work'
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(MAP):
        assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP)
        world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        director=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='WarMachine_CardDirector')
    else:
        world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        def spawn(cls,label,location=(0,0,0),rotation=unreal.Rotator()):
            a=actors.spawn_actor_from_class(cls,unreal.Vector(*location),rotation)
            assert a,label
            a.set_actor_label(label)
            a.set_folder_path('WarMachineCardReview')
            return a
        director=spawn(unreal.EditorAssetLibrary.load_blueprint_class(DEMO+'/Blueprints/BP_CardRevealDirector'),'WarMachine_CardDirector')
        sun=spawn(unreal.DirectionalLight,'WarMachine_Key',(0,250,120),unreal.Rotator(pitch=-25,yaw=-70))
        sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        sun.light_component.set_intensity(2.5)
        sky=spawn(unreal.SkyLight,'WarMachine_Ambient',(0,0,100)).light_component
        sky.set_mobility(unreal.ComponentMobility.MOVABLE)
        sky.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        sky.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
        sky.set_intensity(.7)
        spawn(unreal.PlayerStart,'WarMachine_PlayerStart',(0,140,0),unreal.Rotator(yaw=-90))
    gm=unreal.EditorAssetLibrary.load_blueprint_class(DEMO+'/Blueprints/BP_CardRevealGameMode')
    world.get_world_settings().set_editor_property('default_game_mode',gm)
    names=['FireRate','MissileDamage','HighSpeed']
    director.set_editor_property('CardFrontMaterials',[unreal.load_asset(ROOT+'/Materials/MI_'+n) for n in names])
    director.set_editor_property('CardTextMaterials',[unreal.load_asset(ROOT+'/Materials/MI_'+n+'_UI') for n in names])
    director.set_editor_property('SimpleCardFrame',True)
    if unreal.EditorAssetLibrary.does_asset_exist(ROOT+'/Data/DT_CardText'):
        director.set_editor_property('CardTextDataTable',unreal.load_asset(ROOT+'/Data/DT_CardText'))
        director.set_editor_property('CardTextRowNames',[unreal.Name('WarMachine_'+n) for n in names])
    director.set_editor_property('MaximumTilt',12.0)
    camera=director.get_component_by_class(unreal.CameraComponent)
    assert camera
    camera.set_editor_property('override_aspect_ratio_axis_constraint',True)
    camera.set_editor_property('aspect_ratio_axis_constraint',unreal.AspectRatioAxisConstraint.ASPECT_RATIO_MAINTAIN_XFOV)
    camera.set_editor_property('constrain_aspect_ratio',False)
    pp=camera.post_process_settings
    for key,value in {'override_auto_exposure_method':True,'auto_exposure_method':unreal.AutoExposureMethod.AEM_MANUAL,
        'override_auto_exposure_apply_physical_camera_exposure':True,'auto_exposure_apply_physical_camera_exposure':False,
        'override_auto_exposure_bias':True,'auto_exposure_bias':0.0,'override_motion_blur_amount':True,'motion_blur_amount':0.,
        'override_bloom_intensity':True,'bloom_intensity':.25}.items():pp.set_editor_property(key,value)
    camera.set_editor_property('post_process_settings',pp)
    camera.set_editor_property('post_process_blend_weight',1.)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(unreal.Vector(0,140,0),unreal.Rotator(yaw=-90))
    unreal.ViewportService.set_fov(50)
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,MAP)
    report['actors']=[{'label':a.get_actor_label(),'class':a.get_class().get_path_name(),'location':list(a.get_actor_location().to_tuple())} for a in actors.get_all_level_actors()]
    report['config']={name:[m.get_path_name() for m in director.get_editor_property(name)] for name in ['CardFrontMaterials','CardTextMaterials']}
    report['config'].update({name:director.get_editor_property(name) for name in ['SimpleCardFrame','MaximumTilt','EntryDuration','FlipDuration','FlashDuration','ExitDuration']})
    report['config']['text_table']=str(director.get_editor_property('CardTextDataTable'))
    report['config']['text_rows']=[str(n) for n in director.get_editor_property('CardTextRowNames')]
    report['game_mode']=gm.get_path_name()
    report['controller']=unreal.get_default_object(gm).get_editor_property('player_controller_class').get_path_name()
    report['camera']={'location':list(camera.get_world_location().to_tuple()),'fov':camera.field_of_view,'exposure':str(pp.auto_exposure_method)}
    report['success']=True
except Exception:report['error']=traceback.format_exc()
(OUT/'saved-review-map.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
