"""Compile, save and read back only this card feature and its review map."""
import json
import traceback
from pathlib import Path
import unreal

ROOT='/Game/GuLiStrike/Cards/WarMachineTarot'
SHARED='/Game/GuLiStrike/Cards/RevealDemo'
MAP=ROOT+'/Maps/LVL_WarMachineTarotReview'
OUT=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/Comic_Closeups_v6/Production/Inspection')
r={'success':False,'compile':{},'materials':[]}
try:
    assert not unreal.WidgetService.is_pie_running()
    eal=unreal.EditorAssetLibrary
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().startswith(MAP+'.')
    bps=[SHARED+'/Blueprints/'+n for n in ['BP_ParallaxRevealCard','BP_CardRevealDirector','BP_CardRevealPlayerController','BP_CardRevealGameMode']]
    bps+=[SHARED+'/UI/WBP_CardRevealHUD',ROOT+'/UI/WBP_CardText']
    for bp in bps:
        result=unreal.BlueprintService.compile_blueprint(bp)
        r['compile'][bp]={'success':result.success,'errors':list(result.errors),'warnings':list(result.warnings)}
        assert result.success
        assert eal.save_asset(bp,False)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    d=next(a for a in actors if a.get_actor_label()=='WarMachine_CardDirector')
    assert d.get_editor_property("MaximumTilt")==16.
    camera=d.get_component_by_class(unreal.CameraComponent)
    gm=world.get_world_settings().get_editor_property('default_game_mode')
    r['map']={'path':MAP,'actors':[{'label':a.get_actor_label(),'class':a.get_class().get_path_name(),'location':list(a.get_actor_location().to_tuple())} for a in actors if a.get_actor_label().startswith('WarMachine_')],
        'game_mode':gm.get_path_name(),'player_controller':unreal.get_default_object(gm).get_editor_property('player_controller_class').get_path_name(),
        'camera':{'location':list(camera.get_world_location().to_tuple()),'fov':camera.field_of_view,'exposure':str(camera.post_process_settings.auto_exposure_method)},
        'tuning':{k:d.get_editor_property(k) for k in ['CardAreaMultiplier','CardThicknessMultiplier','MaximumTilt','EntryDuration','FlipDuration','FlashDuration','ExitDuration']},
        'text_table':d.get_editor_property('CardTextDataTable').get_path_name(),'row_ids':[str(n) for n in d.get_editor_property('CardTextRowNames')]}
    assets=list(bps)+[MAP,ROOT+'/Data/DT_CardText',ROOT+'/Data/FCardTextRow',ROOT+'/Materials/M_CelCardEdge',ROOT+'/Materials/M_CelCardUI_Comic_v6',ROOT+'/Materials/M_CelCardParallax_Comic_v6']
    for front,frame in zip(d.get_editor_property('CardFrontMaterials'),d.get_editor_property('CardTextMaterials')):
        depth=unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(front,'Layers global depth')
        assert depth==4.
        tex=unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(front,'BaseColor Map')
        ability=unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(front,'Ability Layer Map')
        assert ability
        assets.append(ability.get_path_name().split('.')[0])
        r['materials'].append({'front':front.get_path_name(),'frame':frame.get_path_name(),'texture':tex.get_path_name(),'global_depth':depth})
        assets.extend([front.get_path_name().split('.')[0],frame.get_path_name().split('.')[0],tex.get_path_name().split('.')[0]])
    cdo=unreal.get_default_object(unreal.load_asset(SHARED+'/Blueprints/BP_CardRevealDirector').generated_class())
    r['original_defaults']={k:cdo.get_editor_property(k) for k in ['CardAreaMultiplier','CardThicknessMultiplier','MaximumTilt','SimpleCardFrame']}
    r['original_fronts']=[m.get_name() for m in cdo.get_editor_property('CardFrontMaterials')]
    assert r['original_defaults']['CardAreaMultiplier']==1 and r['original_defaults']['CardThicknessMultiplier']==1
    assert r['original_fronts']==['MI_Card_Moon','MI_Card_Star','MI_Card_Tower']
    registry=unreal.AssetRegistryHelpers.get_asset_registry()
    missing=[]
    for asset in set(assets):
        assert eal.does_asset_exist(asset),asset
        for dep in registry.get_dependencies(asset,unreal.AssetRegistryDependencyOptions()):
            dep=str(dep)
            if dep.startswith('/Game/') and not eal.does_asset_exist(dep):missing.append(dep)
    r['checked_packages']=len(set(assets))
    r['missing_game_dependencies']=sorted(set(missing))
    assert not missing
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,MAP)
    r['map']['saved']=True
    r['success']=True
except Exception:r['error']=traceback.format_exc()
(OUT/'final-assets.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:r[k] for k in ['success','error','checked_packages','missing_game_dependencies','original_defaults'] if k in r}))

result=json.dumps(r,ensure_ascii=False)
