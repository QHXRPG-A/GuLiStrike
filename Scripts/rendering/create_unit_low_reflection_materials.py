"""Run with Scripts/ue_exec.py after baseline capture. Edits only the dedicated variant directory."""
import json
import unreal

DEST = '/Game/GuLiStrike/Rendering/UnitLowReflection'
OUT = 'D:/UE5.7/test1/TestResults/CommanderGpuOptimization-20260916'
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
PROPERTY = unreal.MaterialProperty.MP_ROUGHNESS
MARKER = 'GuLi non-player unit roughness floor 0.8'
assets, mappings = [], []


def duplicate(source, name):
    path = DEST + '/' + name
    if EAL.does_asset_exist(path):
        result = EAL.load_asset(path)
    else:
        result = EAL.duplicate_asset(source, path)
    if not result:
        raise RuntimeError('Could not duplicate ' + source)
    return result


def rough_body(source, name):
    material = duplicate(source, name)
    if material.get_editor_property('use_material_attributes'):
        raise RuntimeError('Material attributes require explicit handling: ' + source)
    node = MEL.get_material_property_input_node(material, PROPERTY)
    output = MEL.get_material_property_input_node_output_name(material, PROPERTY)
    if node and node.get_class() == unreal.MaterialExpressionMax.static_class() and node.get_editor_property('desc') == MARKER:
        floor = node
    else:
        if not node:
            raise RuntimeError('Unconnected roughness requires inspecting authored constant: ' + source)
        floor = MEL.create_material_expression(material, unreal.MaterialExpressionMax, 400, 0)
        floor.set_editor_property('desc', MARKER)
        floor.set_editor_property('const_b', 0.8)
        assert MEL.connect_material_expressions(node, output, floor, 'A')
        assert MEL.connect_material_property(floor, '', PROPERTY)
        MEL.recompile_material(material)
    assert abs(floor.get_editor_property('const_b') - 0.8) < 0.0001
    assert EAL.save_loaded_asset(material, only_if_is_dirty=False)
    assets.append({'source': source, 'variant': material.get_path_name(), 'roughness_floor': 0.8,
                   'input_node': node.get_class().get_name(), 'output': output})
    return material


for source, name in [
    ('/Game/Commander/Units/M_CommanderFourFRobot_Crowd', 'M_CommanderFourFRobot_LowReflection'),
    ('/Game/Commander/Units/M_WM01_Crowd', 'M_WM01_LowReflection'),
    ('/Game/MC_Vehicle_Constructor/Materials/M_Vehicle_Constructor_PBR', 'M_VehicleConstructor_LowReflection')]:
    body = rough_body(source, name)
    mappings.append({'original': EAL.load_asset(source).get_path_name(), 'variant': body.get_path_name()})

parent = rough_body('/InterchangeAssets/Materials/FBXLegacyPhongSurfaceMaterial', 'M_Wingman_LowReflection')
mesh = EAL.load_asset('/Game/GuLiStrike/Wingman/SM_Wingman_Mass')
for slot in mesh.get_editor_property('static_materials'):
    original = slot.material_interface
    assert isinstance(original, unreal.MaterialInstanceConstant), original
    assert original.get_editor_property('parent').get_path_name() == '/InterchangeAssets/Materials/FBXLegacyPhongSurfaceMaterial.FBXLegacyPhongSurfaceMaterial'
    instance = duplicate(original.get_path_name(), 'MI_Wingman_' + original.get_name() + '_LowReflection')
    MEL.set_material_instance_parent(instance, parent)
    MEL.update_material_instance(instance)
    assert EAL.save_loaded_asset(instance, only_if_is_dirty=False)
    mappings.append({'original': original.get_path_name(), 'variant': instance.get_path_name()})

with open(OUT + '/material-variants.json', 'w', encoding='utf-8') as stream:
    json.dump({'assets': assets, 'mappings': mappings}, stream, indent=2)
print(json.dumps({'base_materials': len(assets), 'material_mappings': len(mappings), 'manifest': OUT + '/material-variants.json'}))
