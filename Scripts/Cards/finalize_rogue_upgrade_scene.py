"""Save the specific feature entry note, reload the actual map, and read saved references."""
import json
from pathlib import Path
import unreal

root=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
map_path='/Game/Maps/LVL_CommanderMassPrototype'
system='/Game/GuLiStrike/FX/RogueCards/NS_RogueUpgrade_Lite'
assert not level.is_in_play_in_editor()
assert editor.get_editor_world().get_path_name().startswith(map_path+'.')
marker=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='RogueCards_F4_Entry')
marker.set_editor_property('text',
    'F4: Rogue Cards (commander ready). Left FireRate +20%, middle MissileDamage +20%, right MoveSpeed +50%. '
    'Click to flip, click again to confirm; Esc cancels before submission. Battlefield continues under background blur. '
    'F4 Detail Lighting debug binding removed. Close clears blur/capture/cards/render target and restores role input. '
    'After a successful card and full UI exit, currently living affected War Machines play a 1-second batched upgrade glow. '
    'Friendly: Excel linear HDR color; enemy: red from each viewer team. Newborn units inherit stats, without replay. '
    'Data: GuLiStrikeRogueCards.xlsx, UpgradeVfx / UpgradeVfxScale / UpgradeVfxColor. '
    'Technical regression recorded; artwork approval pending player review.')
assert level.save_current_level()
assert level.load_level(map_path)
world=editor.get_editor_world()
marker=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='RogueCards_F4_Entry')
game_mode=world.get_world_settings().get_editor_property('default_game_mode')
pc_class=unreal.get_default_object(game_mode).get_editor_property('player_controller_class')
presentation=unreal.get_default_object(pc_class).get_component_by_class(unreal.GuLiRogueCardPresentation)
assert presentation
settings=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrike.GuLiRogueCardSettings'))
table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeRogueCards_Cards')
rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
source_rows={r['Id']:r for r in json.loads((root/'data/Json/DT_GuLiStrikeRogueCards_Cards.json').read_text(encoding='utf-8'))}
checked=[]
for row in rows:
    asset=unreal.load_asset(row['UpgradeVfx'])
    assert isinstance(asset,unreal.NiagaraSystem)
    assert asset.get_path_name()==system+'.NS_RogueUpgrade_Lite'
    assert row['UpgradeVfxScale']==source_rows[row['Id']]['UpgradeVfxScale']
    assert row['UpgradeVfxColor']==source_rows[row['Id']]['UpgradeVfxColor']
    front=unreal.load_asset(row['FrontMaterial'])
    depth=unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(front,'Layers global depth')
    assert abs(depth-4)<.001
    checked.append({'id':row['Id'],'vfx':asset.get_path_name(),'scale':row['UpgradeVfxScale'],
                    'color':row['UpgradeVfxColor'],'front':front.get_path_name(),'parallax_depth':depth,'text_ids':row['TextIds']})
emitters=[]
for e in unreal.NiagaraService.summarize(system).emitter_names:
    properties=unreal.NiagaraEmitterService.get_emitter_properties(system,str(e))
    renderer=unreal.NiagaraEmitterService.get_renderer_details(system,str(e),0)
    assert str(properties.sim_target)=='GPUComputeSim'
    emitters.append({'name':str(e),'simulation':str(properties.sim_target),'material':renderer.material_path,'mesh':renderer.mesh_path})
assert len(emitters)==2
diag=unreal.GuLiCombatEffectAuthoringLibrary.get_rogue_upgrade_compile_diagnostics(unreal.load_asset(system))
report={'success':True,'map':world.get_path_name(),'saved_and_reloaded':True,
        'marker':{'name':marker.get_name(),'label':marker.get_actor_label(),'location':str(marker.get_actor_location()),'text':marker.get_editor_property('text')},
        'game_mode':game_mode.get_path_name(),'player_controller':pc_class.get_path_name(),
        'presentation':presentation.get_class().get_path_name(),'cards':checked,'emitters':emitters,
        'compile_diagnostics':str(diag),'blur_strength':settings.get_editor_property('BlurStrength'),
        'fade_seconds':settings.get_editor_property('FadeSeconds'),'candidates':list(settings.get_editor_property('Candidates')),
        'visual_review':'user_pending','process_images_inspected':False,
        'crowd_default':unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrike.GuLiGroundCrowdManager')).get_editor_property('MaxAgents')}
(root/'Artifacts/RogueCards/Upgrade/final-readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'success':True,'map':report['map'],'cards':len(checked),'emitters':len(emitters),'saved_and_reloaded':True}))
