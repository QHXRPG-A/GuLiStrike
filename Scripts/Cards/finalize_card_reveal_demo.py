"""Compile/save/read back only the card demo packages after visual verification."""
import json,traceback,unreal
from pathlib import Path
root='/Game/GuLiStrike/Cards/RevealDemo'
out=Path('D:/UE5.7/test1/ArtSource/UI/CardRevealDemo')
report={}
try:
    assert not unreal.WidgetService.is_pie_running(), 'End the dedicated preview first'
    report['compile']={}
    for p in ['Blueprints/BP_ParallaxRevealCard','UI/WBP_CardRevealHUD','Blueprints/BP_CardRevealDirector','Blueprints/BP_CardRevealPlayerController','Blueprints/BP_CardRevealGameMode']:
        r=unreal.BlueprintService.compile_blueprint(root+'/'+p)
        assert r.success,str(r)
        report['compile'][p]={'success':r.success,'errors':list(r.errors),'warnings':list(r.warnings)}
        assert unreal.EditorAssetLibrary.save_asset(root+'/'+p,False)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    director=next(a for a in actors if a.get_class().get_name()=='BP_CardRevealDirector_C')
    assert 'LVL_CardRevealDemo' in director.get_world().get_path_name()
    camera=director.get_component_by_class(unreal.CameraComponent)
    pp=camera.post_process_settings
    gm=director.get_world().get_world_settings().get_editor_property('default_game_mode')
    report['scene']={'map':root+'/Maps/LVL_CardRevealDemo',
        'actors':[{'label':a.get_actor_label(),'class':a.get_class().get_path_name(),'location':list(a.get_actor_location().to_tuple())} for a in actors],
        'game_mode':gm.get_path_name(),'controller':unreal.get_default_object(gm).get_editor_property('player_controller_class').get_path_name(),
        'camera':{'location':list(camera.get_world_location().to_tuple()),'rotation':str(camera.get_world_rotation()),'horizontal_fov':camera.field_of_view,'aspect_constraint':str(camera.aspect_ratio_axis_constraint),'exposure_override':pp.override_auto_exposure_method,'exposure_method':str(pp.auto_exposure_method),'physical_exposure':pp.auto_exposure_apply_physical_camera_exposure,'exposure_bias':pp.auto_exposure_bias},
        'parameters':{k:director.get_editor_property(k) for k in ['EntryDuration','FlipDuration','FlashDuration','ExitDuration','MaximumTilt','HoverInterpSpeed','FlashIntensity','CameraDistance','HorizontalFOV','FarDistance']},
        'runtime_card_count':3,'runtime_cards_spawned_by':'BP_CardRevealDirector.StartPresentation'}
    report['materials']={}
    for name in ['M_CardBack','M_DemoBackdrop','M_CardConfirmFlash']:
        p=root+'/Materials/'+name
        report['materials'][name]=str(unreal.MaterialService.get_material_info(p))
    report['front_materials']={}
    for name in ['Moon','Star','Tower']:
        for suffix in ['', '_UI']:
            p=root+'/Materials/MI_Card_'+name+suffix
            report['front_materials'][p]=str(unreal.MaterialService.get_instance_info(p))
    registry=unreal.AssetRegistryHelpers.get_asset_registry()
    assets=registry.get_assets_by_path(root,True,True)
    report['assets']=[str(a.package_name) for a in assets]
    report['dependencies']={str(a.package_name):list(registry.get_dependencies(str(a.package_name),unreal.AssetRegistryDependencyOptions())) for a in assets}
    missing=[]
    for refs in report['dependencies'].values():
        for ref in refs:
            ref=str(ref)
            if not ref.startswith('/Script/') and not unreal.EditorAssetLibrary.does_asset_exist(ref):missing.append(ref)
    report['missing_asset_dependencies']=sorted(set(missing))
    report['saved_map']=unreal.EditorLoadingAndSavingUtils.save_map(director.get_world(),root+'/Maps/LVL_CardRevealDemo')
    assert report['saved_map'] and not missing
    assert report['scene']['parameters']['MaximumTilt']==12
    assert pp.override_auto_exposure_method and pp.auto_exposure_method==unreal.AutoExposureMethod.AEM_MANUAL
    report['success']=True
except Exception:report.update(success=False,error=traceback.format_exc())
(out/'final-validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2,default=str),encoding='utf-8')
if report.get('success'):
    (out/'saved-map.json').write_text(json.dumps(report['scene'],ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:report[k] for k in ['success','error','missing_asset_dependencies','saved_map'] if k in report}))
