"""Replace SSF's old palette at its palette stage, preserving its base/ink stages."""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
lib = unreal.MaterialEditingLibrary
source = unreal.load_asset('/Game/GuLiStrike/Buildings/SSFStylized/Shared/Materials/M_SSF_ThreeToneLine')
original = next(e for e in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if e.get_outer() == source)
source_inputs = [str(i.get_editor_property('input_name')) for i in original.get_editor_property('inputs')]
base_source = list(lib.get_inputs_for_material_expression(source, original))[source_inputs.index('Base')]
repaired = []
for path in unreal.EditorAssetLibrary.list_assets('/Game/GuLiStrike/Models/TeamColor_v1', True, False):
    parent = unreal.load_asset(path)
    if not isinstance(parent, unreal.Material) or '/SSF_' not in path:
        continue
    tone = next(e for e in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if e.get_outer() == parent)
    inputs = list(tone.get_editor_property('inputs'))
    if 'GuLiPalette' not in [str(i.get_editor_property('input_name')) for i in inputs]:
        palette = unreal.CustomInput()
        palette.set_editor_property('input_name', 'GuLiPalette')
        tone.set_editor_property('inputs', inputs + [palette])
    code = tone.get_editor_property('code')
    tone.set_editor_property('code', code.replace('palette=lerp(palette,Team,teamMask);', 'palette=GuLiPalette;'))
    base_copy = next(e for e in unreal.ObjectIterator(unreal.MaterialExpression)
                     if e.get_outer() == parent and e.get_name() == base_source.get_name())
    if not lib.connect_material_expressions(base_copy, '', tone, 'Base'):
        raise RuntimeError('Original SSF base input did not restore: ' + path)
    repaired.append(path)
(ROOT / 'Artifacts/ModelRegistryRuntime20261008/formal-ssf-palette-repair.json').write_text(
    json.dumps({'parents': repaired, 'success': len(repaired) == 6}, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': len(repaired) == 6, 'parents': len(repaired)}))
