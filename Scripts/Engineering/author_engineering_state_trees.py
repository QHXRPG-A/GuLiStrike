"""Upgrade engineering trees in place after the new native build; never starts PIE."""
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert editor.get_game_world() is None, 'StateTree authoring requires an idle editor.'
assert hasattr(unreal.GuLiMiningVehicleManager, 'get_cluster_slot_poses'), 'The new native build is not loaded.'
assert hasattr(unreal.GuLiEngineeringPathSubsystem, 'get_budget_debug'), 'The shared path service is not loaded.'
assert hasattr(unreal.GuLiResourceFactoryActor, 'get_unload_points'), 'The four-point factory build is not loaded.'
world = editor.get_editor_world()
table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers')
before = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)
unreal.SystemLibrary.execute_console_command(world, 'gs.Commander.BuildStateTrees')
report = json.loads((root / 'Artifacts/CommanderStateTree/tree-assets.json').read_text(encoding='utf-8'))
assert report['success']
required = {
    'ST_CommanderMiner': {'Mining_WaitForMiningPosition', 'Mining_WaitForBudgetedMiningPath', 'Mining_RepositionForExtraction',
                        'Mining_TryNextUnloadPoint', 'Mining_UnreachableFactory_SelectNext', 'Mining_Unload', 'Mining_FinishCycle'},
    'ST_CommanderBuilder': {'ReserveConstructionPosition', 'WaitForConstructionPosition', 'WaitForBudgetedConstructionPath', 'RetryConstructionPosition'},
}
for name, states in required.items():
    asset = next(a for a in report['assets'] if a['asset'].endswith('.' + name))
    assert asset['saved'] and asset['ready'] and states.issubset(set(asset['states'])), asset
    if name == 'ST_CommanderMiner':
        assert not {'Mining_EnterFactory', 'Mining_ExitFactory'}.intersection(asset['states']), asset
assert before == unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table), 'Unit bindings changed.'
report['unit_bindings_unchanged'] = True
report['gameplay_started'] = False
out = root / 'outputs/engineering-navigation'
out.mkdir(parents=True, exist_ok=True)
(out / 'state-tree-authoring.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'engineering_assets_saved': 2, 'gameplay_started': False}))
