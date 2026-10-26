"""Preserve original impact emitters/material masks. Only color, scale and fade change."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=ROOT/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/BlinkImpact_B1_20261026'
BASE='/Game/GuLiStrike/FX/GroundMech/BlinkImpact_B1'
SOURCE='/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunImpact_AllOptimizations'
LIB=unreal.EditorAssetLibrary
EDIT=unreal.MaterialEditingLibrary

def author():
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    rows=json.loads((OUT/'original_impact_materials.json').read_text(encoding='utf8'))
    master_source=rows[0]['parents'][0]
    master_path=BASE+'/M_GroundImpact_OriginalShape'
    master=unreal.load_asset(master_path)
    if not master:
        master=LIB.duplicate_asset(master_source,master_path)
        assert master
    if LIB.get_metadata_tag(master,'GroundImpactOutputVersion')!='original-shape-v1':
        original={p: (EDIT.get_material_property_input_node(master,p),EDIT.get_material_property_input_node_output_name(master,p))
                  for p in (unreal.MaterialProperty.MP_EMISSIVE_COLOR,unreal.MaterialProperty.MP_OPACITY)}
        assert all(node for node,name in original.values())
        master.modify()
        dp=EDIT.create_material_expression(master,unreal.MaterialExpressionDynamicParameter,2200,300)
        dp.set_editor_property('parameter_index',3)
        dp.set_editor_property('param_names',['GroundHeatR','GroundHeatG','GroundHeatB','GroundFade'])
        tint=unreal.LinearColor(.8549926,.3371636,.0975873,1)
        dp.set_editor_property('default_value',tint)
        recolor=EDIT.create_material_expression(master,unreal.MaterialExpressionCustom,2500,0)
        pins=[]
        for name in ('Source','R','G','B'):
            pin=unreal.CustomInput();pin.set_editor_property('input_name',name);pins.append(pin)
        recolor.set_editor_property('inputs',pins)
        recolor.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        recolor.set_editor_property('description','Original shape and intensity; bullet-orange hue only')
        recolor.set_editor_property('code','float v=max(Source.r,max(Source.g,Source.b)); return v*float3(R,G,B)/max(max(R,G),max(B,0.0001));')
        node,name=original[unreal.MaterialProperty.MP_EMISSIVE_COLOR]
        assert EDIT.connect_material_expressions(node,name,recolor,'Source')
        for channel in 'RGB':
            assert (EDIT.connect_material_expressions(dp,'GroundHeat'+channel,recolor,channel)
                    or EDIT.connect_material_expressions(dp,channel,recolor,channel))
        assert EDIT.connect_material_property(recolor,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        fade=EDIT.create_material_expression(master,unreal.MaterialExpressionMultiply,2500,300)
        node,name=original[unreal.MaterialProperty.MP_OPACITY]
        assert EDIT.connect_material_expressions(node,name,fade,'A')
        assert (EDIT.connect_material_expressions(dp,'GroundFade',fade,'B')
                or EDIT.connect_material_expressions(dp,'A',fade,'B'))
        assert EDIT.connect_material_property(fade,'',unreal.MaterialProperty.MP_OPACITY)
        LIB.set_metadata_tag(master,'GroundImpactOutputVersion','original-shape-v1')
        EDIT.recompile_material(master)
        assert LIB.save_loaded_asset(master,False)
    originals=[];variants=[]
    for row in rows:
        source=unreal.load_asset(row['material'])
        target=BASE+'/MI_GroundImpact_'+row['emitter']
        variant=unreal.load_asset(target) or LIB.duplicate_asset(row['material'],target)
        assert variant
        variant.modify(); EDIT.set_material_instance_parent(variant,master)
        EDIT.update_material_instance(variant)
        assert LIB.save_loaded_asset(variant,False)
        originals.append(source);variants.append(variant)
    path=BASE+'/NS_GroundMech_Impact_Adjusted'
    system=unreal.load_asset(path) or LIB.duplicate_asset(SOURCE,path)
    assert system
    result=json.loads(unreal.GuLiGroundMechFireEditorLibrary.configure_impact_variant(system,originals,variants,True))
    assert result['success'],result
    compiled=unreal.NiagaraService.compile_with_results(path)
    assert compiled.success and compiled.error_count==0,str(compiled)
    assert unreal.NiagaraService.save_system(path)
    diagnostics=[]
    for material in [master]+variants:
        if isinstance(material,unreal.Material):
            d=unreal.MaterialNodeService.get_material_diagnostics(material.get_path_name())
            assert d.success and d.is_compiled_ok and not d.compile_errors,str(d)
            diagnostics.append({'asset':material.get_path_name(),'compile_errors':list(d.compile_errors)})
        else:
            assert material.get_editor_property('parent')==master
            diagnostics.append({'asset':material.get_path_name(),'parent':master.get_path_name(),'instance_overrides':'copied from original'})
    result.update({'source':SOURCE,'asset':path,'materials':diagnostics,'preserves':'all six original emitters, five renderers, spawn counts, motion, size curves, textures, static instance overrides',
                   'changes':'VFX scale 2 -> 4; orange-yellow output hue; common 0.20s opacity fade',
                   'supersedes':'NS_GroundMech_Impact_B1 (shape redesign rejected; not referenced)',
                   'PIE':False,'visual_acceptance':'pending user'})
    (OUT/'preserved_impact_authoring.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    return result

if __name__=='__main__': unreal.MCPythonHelper.submit_result(json.dumps(author()))
