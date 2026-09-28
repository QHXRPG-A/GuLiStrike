"""Explicit one-time hierarchy migration after the approved native build is loaded.

Backs up original packages, migrates Miner -> Builder -> Mass, compiles and saves
those assets only. A version-2 asset is inspected without regenerating it.
Does not launch PIE or any runtime/automated test.
"""
import json
import runpy
from pathlib import Path
import unreal

root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
if editor.get_game_world() is not None:
    raise RuntimeError('An active game world blocks StateTree migration.')
table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers')
before = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)
path = root / 'Artifacts/CommanderStateTree/HierarchyV2/migration.json'
path.unlink(missing_ok=True)
unreal.SystemLibrary.execute_console_command(editor.get_editor_world(), 'gs.Commander.MigrateStateTreesV2')
if not path.exists():
    raise RuntimeError('The V2 migration command is unavailable. Load the approved native build first.')
migration = json.loads(path.read_text(encoding='utf-8-sig'))
if not migration['success']:
    raise RuntimeError('Migration stopped; inspect ' + str(path))
assert before == unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table), 'Unit bindings changed.'
inspect = runpy.run_path(str(root / 'Scripts/inspect_commander_state_trees.py'))['inspect']
report = inspect()
assert report['success'], 'Readback or unit binding validation failed.'
assert all(a['hierarchy_version'] == '2' and a['max_depth'] >= 3
           and a['persistent_task_count'] > 0 and not a['dirty'] for a in report['assets'])
migration['unit_bindings_unchanged'] = True
migration['gameplay_started'] = False
path.write_text(json.dumps(migration, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({
    'success': True, 'migration_report': str(path), 'four_bindings_match': report['four_bindings_match'],
    'assets': [{k: a[k] for k in ('asset', 'hierarchy_version', 'state_count', 'max_depth',
                                'persistent_task_count', 'compiled_matches_editor', 'dirty')}
               for a in report['assets']], 'gameplay_started': False,
}))
