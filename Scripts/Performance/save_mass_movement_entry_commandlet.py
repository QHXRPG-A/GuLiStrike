"""Correct the final editor-only map instructions without starting PIE or a GUI."""
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
map_path = '/Game/Maps/LVL_CommanderMassPrototype'
world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
assert world and world.get_path_name() == map_path + '.LVL_CommanderMassPrototype'
script = (root / 'Scripts/Performance/author_mass_movement_review_entry.py').read_text(encoding='utf-8')
submit = 'unreal.MCPythonHelper.submit_result(json.dumps('
assert script.count(submit) == 1
# The same authoring checks run in a commandlet; persist its result directly.
script = script.replace(submit, "(root / 'Artifacts/MassAvoidance20261011/DirectApply/MapEntry.json').write_text(json.dumps(")
script = script.replace('ensure_ascii=False))', "ensure_ascii=False, indent=2), encoding='utf-8')")
exec(compile(script, 'author_mass_movement_review_entry.py', 'exec'), globals())
result = json.loads((root / 'Artifacts/MassAvoidance20261011/DirectApply/MapEntry.json').read_text(encoding='utf-8'))
assert result['saved'] and '三来源' in result['note']['text']
assert '无本轮运行时开关' in result['note']['text'] and 'mass/old' not in result['note']['text']
unreal.log('MASS_ENTRY_CORRECTION_SAVED')
