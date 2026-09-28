"""Read saved artwork bindings/material diagnostics; no gameplay run."""
import unreal
import json
import traceback
from pathlib import Path
ROOT='/Game/GuLiStrike/Cards/WarMachineTarot'
SHARED='/Game/GuLiStrike/Cards/RevealDemo'
MAP=ROOT+'/Maps/LVL_WarMachineTarotReview'
OUT=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/ModelComic_v9/Production')
R={'success':False,'gameplay_run':False,'assistant_visual_review':'not_performed_per_user_request'}
EAL=unreal.EditorAssetLibrary
MEL=unreal.MaterialEditingLibrary
try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    R['static_preview']={'status':'incomplete_editor_crash',
        'reason':'2100-pixel render target property edit opened a modal warning inside Slate tick; nested window creation failed with error 87.',
        'visual_review':'not_performed',
        'partial_files':[p.name for p in sorted((OUT/'Previews').glob('*.png'))]}
    dirty=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    assert not dirty,'Preserve newer unsaved map edits: '+str(dirty)
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP)
    WORLD=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    assert not any(a.get_actor_label().startswith('Temp_ModelComicV9_') for a in actors)
    d=next(a for a in actors if a.get_actor_label()=='WarMachine_CardDirector')
    camera=d.get_component_by_class(unreal.CameraComponent)
    R['map']={'path':WORLD.get_path_name(),'reloaded_from_disk':True,
        'actors':[{'label':a.get_actor_label(),'class':a.get_class().get_path_name()} for a in actors],
        'camera':{'location':list(camera.get_world_location().to_tuple()),'fov':camera.field_of_view},
        'game_mode':WORLD.get_world_settings().get_editor_property('default_game_mode').get_path_name(),
        'tuning':{k:d.get_editor_property(k) for k in ['MaximumTilt','CardAreaMultiplier','CardThicknessMultiplier','EntryDuration','FlipDuration','FlashDuration','ExitDuration','SimpleCardFrame']}}
    assert R['map']['tuning']['MaximumTilt']==16.
    assert R['map']['tuning']['CardAreaMultiplier']==2.
    assert R['map']['tuning']['CardThicknessMultiplier']==2.
    R['materials']=[]
    paths=[MAP,ROOT+'/Materials/M_CelCardParallax_ModelComic_v9']
    for i,name in enumerate(['FireRate','MissileDamage','HighSpeed']):
        mi=d.get_editor_property('CardFrontMaterials')[i]
        frame=d.get_editor_property('CardTextMaterials')[i]
        assert mi.get_name()=='MI_'+name+'_ModelComic_v9'
        tex=MEL.get_material_instance_texture_parameter_value(mi,'BaseColor Map')
        ability=MEL.get_material_instance_texture_parameter_value(mi,'Ability Layer Map')
        assert tex==ability and tex.get_name()=='T_'+name+'_Layers_ModelComic_v9'
        assert MEL.get_material_instance_scalar_parameter_value(mi,'Layers global depth')==4.
        layers=[]
        for j in range(1,7):
            layers.append({'index':j,'depth':MEL.get_material_instance_scalar_parameter_value(mi,f'Layer {j} Depth'),
                'opacity':MEL.get_material_instance_scalar_parameter_value(mi,f'Layer {j} Opacity'),
                'offset':list(MEL.get_material_instance_vector_parameter_value(mi,f'Layer {j} Offset').to_tuple()),
                'scale':list(MEL.get_material_instance_vector_parameter_value(mi,f'Layer {j} Scale').to_tuple())})
        assert layers[2]['depth']==0. and all(x['opacity']==1. for x in layers)
        R['materials'].append({'front':mi.get_path_name(),'parent':mi.get_editor_property('parent').get_path_name(),
            'texture':tex.get_path_name(),'frame':frame.get_path_name(),'size':[tex.blueprint_get_size_x(),tex.blueprint_get_size_y()],
            'global_depth':4.,'layers':layers})
        paths.extend([o.get_path_name().split('.')[0] for o in [mi,frame,tex]])
    parent=unreal.load_asset(ROOT+'/Materials/M_CelCardParallax_ModelComic_v9')
    diag=unreal.MaterialNodeService.get_material_diagnostics(parent.get_path_name())
    R['shader']={'compiled_ok':diag.is_compiled_ok,'errors':list(diag.compile_errors),'samples':diag.texture_sample_count}
    assert R['shader']['compiled_ok'] and not R['shader']['errors']
    guard=next(n for n in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if n.get_outer()==parent and str(n.get_editor_property('desc'))=='WM_FIXED_FRAME_APERTURE')
    R['aperture']=guard.get_editor_property('code')
    table=d.get_editor_property('CardTextDataTable')
    R['text']={'table':table.get_path_name(),'rows':[str(x) for x in d.get_editor_property('CardTextRowNames')],
        'csv':unreal.DataTableFunctionLibrary.export_data_table_to_csv_string(table)}
    assert R['text']['rows']==['WarMachine_FireRate','WarMachine_MissileDamage','WarMachine_HighSpeed']
    paths.extend([table.get_path_name().split('.')[0],ROOT+'/UI/WBP_CardText',ROOT+'/Data/FCardTextRow'])
    bp=unreal.load_asset(SHARED+'/Blueprints/BP_CardRevealDirector')
    cdo=unreal.get_default_object(bp.generated_class())
    R['original_tarot_defaults']={k:cdo.get_editor_property(k) for k in ['CardAreaMultiplier','CardThicknessMultiplier','MaximumTilt','SimpleCardFrame']}
    R['original_tarot_fronts']=[m.get_name() for m in cdo.get_editor_property('CardFrontMaterials')]
    assert R['original_tarot_fronts']==['MI_Card_Moon','MI_Card_Star','MI_Card_Tower']
    registry=unreal.AssetRegistryHelpers.get_asset_registry()
    R['missing_dependencies']=[]
    for p in set(paths):
        assert EAL.does_asset_exist(p),p
        for dependency in registry.get_dependencies(p,unreal.AssetRegistryDependencyOptions()):
            dep=str(dependency)
            if dep.startswith('/Game/') and not EAL.does_asset_exist(dep):R['missing_dependencies'].append(dep)
    assert not R['missing_dependencies']
    R['checked_packages']=len(set(paths))
    R['preview_images']=len(R['static_preview']['partial_files'])
    R['success']=True
except Exception:R['error']=traceback.format_exc()
(OUT/'Inspection/final-readback.json').write_text(json.dumps(R,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:R[k] for k in ['success','error','shader','checked_packages','preview_images'] if k in R},ensure_ascii=False))
