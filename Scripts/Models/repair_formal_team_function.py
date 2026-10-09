"""Repair and validate the formal function's unnamed output pins after visual QA."""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
lib = unreal.MaterialEditingLibrary
function = unreal.load_asset('/Game/GuLiStrike/Models/TeamColor_v1/Shared/MF_GuLiTeamRegion')
if not function:
    raise RuntimeError('Formal shared function is missing')
nodes = [e for e in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if e.get_outer() == function]
palette = next(e for e in nodes if 'fixedLinear' in e.get_editor_property('code'))
lamp = next(e for e in nodes if 'max(0.0,Strength)' in e.get_editor_property('code'))
repaired = []
for output in unreal.ObjectIterator(unreal.MaterialExpressionFunctionOutput):
    if output.get_outer() != function:
        continue
    name = str(output.get_editor_property('output_name'))
    source = palette if name == 'PaletteBeforeThreeTone' else lamp if name == 'TeamLampStrength' else None
    if source:
        if not lib.connect_material_expressions(source, '', output, ''):
            raise RuntimeError('Function output did not connect: ' + name)
        repaired.append(name)
if set(repaired) != {'PaletteBeforeThreeTone', 'TeamLampStrength'}:
    raise RuntimeError('Formal output set differs')
lib.update_material_function(function)
if not unreal.EditorAssetLibrary.save_loaded_asset(function):
    raise RuntimeError('Formal function did not save')
parents = []
for path in unreal.EditorAssetLibrary.list_assets('/Game/GuLiStrike/Models/TeamColor_v1', True, False):
    asset = unreal.load_asset(path)
    if isinstance(asset, unreal.Material):
        lib.recompile_material(asset)
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
            raise RuntimeError('Formal parent did not save: ' + path)
        parents.append(path)
report = {'success': True, 'cause': 'FunctionOutput input pin is unnamed; A silently rejected the connection',
          'outputs_connected': repaired, 'parents_recompiled': parents, 'formal_assets_changed': True}
(ROOT / 'Artifacts/ModelRegistryRuntime20261008/formal-shader-repair.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'outputs_connected': repaired, 'parents_recompiled': len(parents)}))
