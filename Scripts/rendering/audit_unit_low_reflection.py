"""Read-only UE API audit of the authored material variants; produces evidence, not a new automation case."""
import json
import unreal

ROOT = 'D:/UE5.7/test1/TestResults/CommanderGpuOptimization-20260916'
manifest = json.load(open(ROOT + '/material-variants.json'))
MEL = unreal.MaterialEditingLibrary
report = {'bases': [], 'instances': [], 'original_mesh_materials': {}}
properties = [unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialProperty.MP_NORMAL,
    unreal.MaterialProperty.MP_EMISSIVE_COLOR, unreal.MaterialProperty.MP_METALLIC,
    unreal.MaterialProperty.MP_SPECULAR, unreal.MaterialProperty.MP_OPACITY]


def input_signature(material, prop):
    node = MEL.get_material_property_input_node(material, prop)
    return [node.get_name(), node.get_class().get_name(), MEL.get_material_property_input_node_output_name(material, prop)] if node else None


for entry in manifest['assets']:
    original, variant = (unreal.load_asset(entry[key]) for key in ['source','variant'])
    node = MEL.get_material_property_input_node(variant, unreal.MaterialProperty.MP_ROUGHNESS)
    original_input = MEL.get_material_property_input_node(original, unreal.MaterialProperty.MP_ROUGHNESS)
    inputs = MEL.get_inputs_for_material_expression(variant, node)
    diagnostics = unreal.MaterialNodeService.get_material_diagnostics(entry['variant'])
    row = {'original':original.get_path_name(), 'variant':variant.get_path_name(),
        'floor':node.get_editor_property('const_b'),
        'roughness_keeps_original_input':bool(inputs and inputs[0].get_name() == original_input.get_name()),
        'other_inputs_unchanged':all(input_signature(original,p) == input_signature(variant,p) for p in properties),
        'compiled':diagnostics.success and diagnostics.is_compiled_ok,
        'compile_errors':list(diagnostics.compile_errors)}
    report['bases'].append(row)

for entry in manifest['mappings']:
    original, variant = (unreal.load_asset(entry[key]) for key in ['original','variant'])
    if not isinstance(original,unreal.MaterialInstanceConstant): continue
    parent = variant.get_editor_property('parent')
    comparisons = {}
    for kind in ['scalar','vector','texture','static_switch']:
        names = getattr(MEL,'get_'+kind+'_parameter_names')(original.get_editor_property('parent'))
        getter = getattr(MEL,'get_material_instance_'+kind+'_parameter_value')
        comparisons[kind] = all(getter(original,n) == getter(variant,n) for n in names)
    report['instances'].append({'original':original.get_path_name(), 'variant':variant.get_path_name(),
        'project_parent':parent.get_path_name(), 'parameters_unchanged':comparisons})

for path in ['/Game/Commander/Units/SM_CommanderFourFRobot_Crowd','/Game/Commander/Units/SM_WM01_Crowd','/Game/GuLiStrike/Wingman/SM_Wingman_Mass']:
    mesh=unreal.load_asset(path)
    report['original_mesh_materials'][path]=[s.material_interface.get_path_name() for s in mesh.get_editor_property('static_materials')]
report['all_bases_valid'] = all(r['floor'] >= 0.79999 and r['roughness_keeps_original_input'] and r['other_inputs_unchanged'] and r['compiled'] and not r['compile_errors'] for r in report['bases'])
report['all_instance_parameters_preserved'] = all(all(r['parameters_unchanged'].values()) for r in report['instances'])
open(ROOT+'/material-graph-audit.json','w').write(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k.startswith('all_')}))
