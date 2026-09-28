"""Read the saved card/review/battle configuration without starting gameplay."""
import json
import traceback
from pathlib import Path
import unreal

OUT=Path(unreal.Paths.project_dir())/'Artifacts/RogueCards'
SYSTEM='/Game/GuLiStrike/CardSystem'
BATTLE='/Game/Maps/LVL_CommanderMassPrototype'
R={'success':False,'gameplay':'not_run','visual_review':'user_pending'}
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
try:
    assert not level.is_in_play_in_editor()
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Preserve unsaved map changes'
    review=SYSTEM+'/WarMachineTarot/Maps/LVL_WarMachineTarotReview'
    assert level.load_level(review)
    director=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='WarMachine_CardDirector')
    tuning={key:director.get_editor_property(key) for key in ['MaximumTilt','CardAreaMultiplier','CardThicknessMultiplier',
        'EntryDuration','FlipDuration','FlashDuration','ExitDuration','ExternalConfirmation','UseLiveCardData']}
    assert tuning['MaximumTilt']==16 and tuning['CardAreaMultiplier']==2 and tuning['CardThicknessMultiplier']==2
    assert not tuning['ExternalConfirmation'] and not tuning['UseLiveCardData']
    R['review']={'map':review,'director':director.get_path_name(),'tuning':tuning,
        'fronts':[v.get_path_name() for v in director.get_editor_property('CardFrontMaterials')],
        'frames':[v.get_path_name() for v in director.get_editor_property('CardTextMaterials')],
        'legacy_text_table':director.get_editor_property('CardTextDataTable').get_path_name()}
    assert all('/Cards/Commander/WM01/' in p for p in R['review']['fronts'])
    blueprint=unreal.load_asset(SYSTEM+'/RevealDemo/Blueprints/BP_CardRevealDirector')
    defaults=unreal.get_default_object(blueprint.generated_class())
    R['original_demo_fronts']=[v.get_name() for v in defaults.get_editor_property('CardFrontMaterials')]
    assert R['original_demo_fronts']==['MI_Card_Moon','MI_Card_Star','MI_Card_Tower']
    options=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrike.GuLiRogueCardSettings'))
    R['candidates']=[str(v) for v in options.get_editor_property('Candidates')]
    assert R['candidates']==['01.01','03.01','02.01']
    R['settings']={k:options.get_editor_property(k).get_path_name() for k in ['Cards','Texts','DirectorClass','CaptureMaterial','FrameMaterial']}
    R['shaders']={}
    for path in [R['settings']['CaptureMaterial'],SYSTEM+'/WarMachineTarot/Materials/M_CelCardParallax_ModelComic_v9']:
        result=unreal.MaterialNodeService.get_material_diagnostics(path)
        R['shaders'][path]={'compiled_ok':result.is_compiled_ok,'errors':list(result.compile_errors)}
        assert result.is_compiled_ok and not result.compile_errors,path
    registry=unreal.AssetRegistryHelpers.get_asset_registry()
    R['legacy_entries']=[{'path':str(d.package_name),'class':str(d.asset_class_path.asset_name)}
        for root in ['/Game/GuLiStrike/Cards/RevealDemo','/Game/GuLiStrike/Cards/WarMachineTarot']
        for d in registry.get_assets_by_path(root,recursive=True)]
    assert all(d['class']=='ObjectRedirector' for d in R['legacy_entries'])
    assert level.load_level(BATTLE)
    marker=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='RogueCards_F4_Entry')
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    R['battle']={'map':world.get_path_name(),'reloaded_from_disk':True,'marker':marker.get_path_name(),
        'label':marker.get_actor_label(),'position':list(marker.get_actor_location().to_tuple()),'instructions':marker.get_editor_property('text')}
    R['success']=True
except Exception:
    R['error']=traceback.format_exc()
finally:
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not world.get_path_name().startswith(BATTLE+'.'):
        level.load_level(BATTLE)
    (OUT/'final-readback.json').write_text(json.dumps(R,ensure_ascii=False,indent=2,default=str),encoding='utf-8')
print(json.dumps({'success':R['success'],'error':R.get('error')},ensure_ascii=False))
