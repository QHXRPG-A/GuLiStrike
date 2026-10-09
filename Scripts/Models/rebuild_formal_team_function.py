"""Replace ambiguous duplicate function pins, preserving each model's tone/ink/VAT graph."""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
lib = unreal.MaterialEditingLibrary
namespace = {'GULI_TEAM_FUNCTION_DEST': '/Game/GuLiStrike/Models/TeamColor_v1/Shared', 'GULI_TEAM_FUNCTION_NAME': 'MF_GuLiTeamRegion_v3'}
function = unreal.load_asset('/Game/GuLiStrike/Models/TeamColor_v1/Shared/MF_GuLiTeamRegion_v3')
if not function or lib.get_num_material_expressions_in_function(function) != 10:
    exec((ROOT / 'Scripts/Models/author_team_region_function.py').read_text(encoding='utf8'), namespace)
    function = namespace['function']
if lib.get_num_material_expressions_in_function(function) != 10:
    raise RuntimeError('Clean team function must contain exactly 10 expressions')


def connect(source, output, target, pin):
    if not lib.connect_material_expressions(source, output, target, pin):
        raise RuntimeError('Rejected parent connection: ' + target.get_path_name() + '/' + pin)


parents = []
for path in unreal.EditorAssetLibrary.list_assets('/Game/GuLiStrike/Models/TeamColor_v1', True, False):
    parent = unreal.load_asset(path)
    if not isinstance(parent, unreal.Material):
        continue
    calls = [e for e in unreal.ObjectIterator(unreal.MaterialExpressionMaterialFunctionCall) if e.get_outer() == parent and
             (not e.get_editor_property('material_function') or e.get_editor_property('material_function').get_name().startswith('MF_GuLiTeamRegion'))]
    for call in calls:
        if not call.set_material_function(function):
            raise RuntimeError('Formal function call did not rebuild its input/output pins: ' + path)
        vertex = next(e for e in unreal.ObjectIterator(unreal.MaterialExpressionVertexColor) if e.get_outer() == parent)
        connect(vertex, '', call, 'FixedPalette')
        connect(vertex, 'A', call, 'RoleAlpha')
        emission = lib.get_material_property_input_node(parent, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        sources = list(lib.get_inputs_for_material_expression(parent, emission))
        if len(sources) != 2:
            raise RuntimeError('Expected original tone stage plus scoped lamp: ' + path)
        tone = next(e for e in sources if isinstance(e, unreal.MaterialExpressionCustom))
        pins = set(lib.get_material_expression_input_names(tone))
        color_pin = next(n for n in ['GuLiPalette', 'Color', 'Base', 'Palette'] if n in pins)
        connect(call, 'PaletteBeforeThreeTone', tone, color_pin)
        if 'Lamp' in pins:
            connect(call, 'TeamLampStrength', tone, 'Lamp')
        lamp = next(e for e in sources if isinstance(e, unreal.MaterialExpressionMultiply))
        connect(call, 'PaletteBeforeThreeTone', lamp, 'A')
        connect(call, 'TeamLampStrength', lamp, 'B')
    if calls:
        lib.recompile_material(parent)
        if not unreal.EditorAssetLibrary.save_loaded_asset(parent):
            raise RuntimeError('Parent save failed: ' + path)
        parents.append(path)
report = {'success': True, 'function': function.get_path_name(), 'expression_count': 10,
          'cause': 'repair missing moved function references, duplicate function input names, rejected output pins and B face-role fixed palette interpretation',
          'parents': parents, 'source_tone_ink_vat_graphs_preserved': True}
(ROOT / 'Artifacts/ModelRegistryRuntime20261008/formal-shader-rebuild.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'function': function.get_path_name(), 'parents': len(parents)}))
