"""Author Commander trees and import Soldiers in an idle editor after the approved native build.

Uses the existing CSV import pipeline. Does not start gameplay or run automation.
"""
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
out = root / 'Artifacts/CommanderStateTree'
out.mkdir(parents=True, exist_ok=True)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
if editor.get_game_world() is not None:
    raise RuntimeError('Stop authoring: an active gameplay world exists.')
world = editor.get_editor_world()
tree_report = out / 'tree-assets.json'
if tree_report.exists():
    tree_report.unlink()
unreal.SystemLibrary.execute_console_command(world, 'gs.Commander.BuildStateTrees')
trees = json.loads(tree_report.read_text(encoding='utf-8'))
if not trees['success'] or len(trees['assets']) != 3 or not all(a.get('ready') for a in trees['assets']):
    raise RuntimeError('Commander tree authoring failed; see tree-assets.json')

namespace = {'__name__': 'commander_state_tree_import'}
source = (root / 'Scripts/import_data_to_engine.py').read_text(encoding='utf-8')
definitions = source.split('report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}', 1)[0]
exec(compile(definitions, 'import_data_to_engine.py', 'exec'), namespace)
namespace['PROGRESS'] = str(out / 'import-progress.log')
manifest = json.loads((root / 'data/Json/manifest.json').read_text(encoding='utf-8'))
name = 'DT_GuLiStrikeCommander_Soldiers'
report = {'table': namespace['import_table'](name, manifest['tables'][name])}
table = unreal.load_asset('/Game/GuLiStrike/Data/' + name)
report['rows'] = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
expected = {r['Name']: r for r in json.loads((root / ('data/Json/' + name + '.json')).read_text(encoding='utf-8'))}
report['bindings_match'] = len(report['rows']) == len(expected) and all(
    expected[row['Name']]['StateTreeAsset'] in str(row.get('StateTreeAsset', '')) for row in report['rows'])
report['success'] = bool(report['table'].get('imported')) and report['bindings_match']
(out / 'soldiers-import.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
if not report['success']:
    raise RuntimeError('Soldiers import/binding readback failed; see soldiers-import.json')

retired_report = out / 'retired-table.json'
if retired_report.exists():
    retired_report.unlink()
unreal.SystemLibrary.execute_console_command(world, 'gs.Commander.RetireSpecialTaskTable')
retired = json.loads(retired_report.read_text(encoding='utf-8'))
if not retired['deleted_or_absent']:
    raise RuntimeError('Retired task DataTable still has references; see retired-table.json')
print('Commander assets saved: three compiled trees, four Soldiers bindings, retired table removed.')
