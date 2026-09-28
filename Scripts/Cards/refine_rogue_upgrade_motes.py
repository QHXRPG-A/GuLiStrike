"""Replace repeated glint clusters with independent GPU particles; preserve ring/glow."""
import ast
import json
import traceback
from pathlib import Path
import unreal

root=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
out=root/'Artifacts/RogueCards/Upgrade/mote-randomization.json'
system_path='/Game/GuLiStrike/FX/RogueCards/NS_RogueUpgrade_Lite'
material_path='/Game/GuLiStrike/FX/RogueCards/M_UpgradeRingMotes'
sp=unreal.NiagaraScratchPadService
ns=unreal.NiagaraService
em=unreal.NiagaraEmitterService
assets=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
report={'success':False,'visual_review':'user_pending','images_inspected':False,
        'gpu_motes_per_unit':18,'glints_per_particle':1,'high_quality_glints':18,
        'medium_quality_glints':6,'low_quality_glints':0,'gpu_capacity_per_block':20480,
        'native_build':'runtime_quality_count_requires_matching_editor_build'}


def custom(emitter,module):
    nodes=[n for n in sp.list_nodes(system_path,emitter,module) if str(n.node_type)=='CustomHlsl']
    assert len(nodes)==1, 'Expected exactly one custom HLSL node: '+module
    node=str(nodes[0].node_id)
    return node,sp.get_custom_hlsl_code(system_path,emitter,module,node)


try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    tree=ast.parse((root/'Scripts/Cards/author_rogue_upgrade_vfx.py').read_text(encoding='utf-8'))
    definitions=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in {'short_mote_mask_code','short_mote_update_code','mote_slot_assignment_code'}]
    assert len(definitions)==3
    exec(compile(ast.Module(body=definitions,type_ignores=[]),'mote_authoring_functions','exec'),globals())
    node,before=custom('UpgradeRingMotes','ShapeUpgradeRingMotes')
    _,glow_before=custom('UpgradeGlow','ShapeUpgradeGlow')
    assign_node,assign_before=custom('UpgradeRingMotes','AssignUpgradeSlot')
    assert 'Part<=Params.z' in before and 'float2(320,320)*(.78+.32*t)' in before
    material=unreal.load_asset(material_path)
    masks=[o for o in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if o.get_outer()==material]
    assert len(masks)==1
    report['previous_mask']=masks[0].get_editor_property('code')
    report['previous_motion']=before
    report['previous_assignment']=assign_before
    mask_code='float Shape=Dynamic.x; float Phase=Dynamic.y; '+short_mote_mask_code()
    masks[0].set_editor_property('code',mask_code)
    unreal.MaterialEditingLibrary.recompile_material(material)
    d=unreal.MaterialNodeService.get_material_diagnostics(material_path)
    report['material']={'compiled_ok':d.is_compiled_ok,'errors':list(d.compile_errors)}
    assert d.is_compiled_ok and not d.compile_errors
    motion_code=short_mote_update_code()
    assert sp.set_custom_hlsl_code(system_path,'UpgradeRingMotes','ShapeUpgradeRingMotes',node,motion_code)
    assignment_code=mote_slot_assignment_code()
    assert sp.set_custom_hlsl_code(system_path,'UpgradeRingMotes','AssignUpgradeSlot',assign_node,assignment_code)
    assert ns.set_rapid_iteration_param_by_stage(system_path,'UpgradeRingMotes','EmitterUpdate',
        'Constants.UpgradeRingMotes.SpawnBurst_Instantaneous.Spawn Count','19456')
    assert sp.apply_changes(system_path)
    result=ns.compile_with_results(system_path)
    report['compile']={'success':result.success,'errors':list(result.errors),'warnings':list(result.warnings)}
    assert result.success and not result.errors
    report['native_diagnostics']=unreal.GuLiCombatEffectAuthoringLibrary.get_rogue_upgrade_compile_diagnostics(unreal.load_asset(system_path))
    assert not report['native_diagnostics']
    assert custom('UpgradeGlow','ShapeUpgradeGlow')[1]==glow_before
    assert custom('UpgradeRingMotes','AssignUpgradeSlot')[1]==assignment_code
    assert assets.save_asset(material_path,only_if_is_dirty=True)
    assert assets.save_asset(system_path,only_if_is_dirty=True)
    assert custom('UpgradeRingMotes','ShapeUpgradeRingMotes')[1]==motion_code
    assert masks[0].get_editor_property('code')==mask_code
    report['saved']=[material_path,system_path]
    report['readback_motion']=motion_code
    report['readback_mask']=mask_code
    report['readback_assignment']=custom('UpgradeRingMotes','AssignUpgradeSlot')[1]
    report['burst_counts']={str(e):[str(p.value) for p in ns.list_rapid_iteration_params(system_path,str(e))
                                  if 'Spawn Count' in str(p.parameter_name)]
                            for e in ns.summarize(system_path).emitter_names}
    assert '19456' in report['burst_counts']['UpgradeRingMotes']
    assert '1024' in report['burst_counts']['UpgradeGlow']
    report['emitters']=[{'name':str(e),'simulation':str(em.get_emitter_properties(system_path,str(e)).sim_target),
                        'material':em.get_renderer_details(system_path,str(e),0).material_path}
                       for e in ns.summarize(system_path).emitter_names]
    report['ring_glow_preserved']=True
    report['success']=True
except Exception:
    report['error']=traceback.format_exc()
out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in {'previous_mask','previous_motion','previous_assignment','readback_mask','readback_motion','readback_assignment'}},ensure_ascii=False))
