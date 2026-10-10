"""Publish all user-authorized combined resources through the existing table importer.

Excel edits and stage-one export precede this editor-only step. Approval is
recorded in the proposal; combined node/GPU/collision variants and the small-batch pool are included.
"""
import contextlib
import io
import json
import math
import sys
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'outputs/performance/20261009-all-optimizations'
proposal_path = OUT / 'formal-reference-proposal.json'
proposal = json.loads(proposal_path.read_text(encoding='utf-8'))
assert proposal['approval'] == 'approved_by_user'
assert proposal['authorization'] == '直接应用所有优化，并补齐整版对照'
assert [row['id'] for row in proposal['rows']] == [4, 5, 36, 38, 45, 52]
assert not unreal.EditorLevelLibrary.get_pie_worlds(True)

for row in proposal['rows']:
    assert isinstance(unreal.load_object(None, row['new_resource']), unreal.NiagaraSystem)

scope = {
    '__name__': '__main__',
    'GULI_TABLE_FILTER': {'DT_GuLiStrikeVfx_Effects'},
    'GULI_IMPORT_REPORT': str(OUT / 'formal-reference-import-report.json'),
    'GULI_IMPORT_PROGRESS': str(OUT / 'formal-reference-import-progress.log'),
}
with contextlib.redirect_stdout(io.StringIO()):
    exec(compile((ROOT / 'Scripts/import_data_to_engine.py').read_text(encoding='utf-8'),
                 'import_data_to_engine.py', 'exec'), scope)
import_report = json.loads((OUT / 'formal-reference-import-report.json').read_text(encoding='utf-8'))
assert not import_report['errors'], import_report
assert len(import_report['tables']) == 1
assert import_report['tables'][0]['imported'], import_report

table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects')
assert scope['vfx_table_matches_source'](
    table, json.loads((ROOT / 'Data/Json/DT_GuLiStrikeVfx_Effects.json').read_text(encoding='utf-8')))
saved_rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
sys.path.insert(0, str(ROOT / 'Scripts/Vfx'))
from configure_registry_bindings import configure_blueprints

blueprints = configure_blueprints()
for binding in blueprints:
    expected_id = 45 if '/ConstructionVehicle/' in binding['path'] else 36
    for side in ['L', 'R']:
        name = 'MiningLaser_' + side
        assert unreal.BlueprintService.get_component_property(binding['path'], name, 'Asset') == 'None'
        assert f'VfxId={expected_id}' in binding['bindings']
    binding['components'] = [{
        'name': 'MiningLaser_' + side,
        **{field: unreal.BlueprintService.get_component_property(binding['path'], 'MiningLaser_' + side, field)
           for field in ['Asset', 'RelativeScale3D', 'bAbsoluteScale']},
    } for side in ['L', 'R']]

registry = unreal.AssetRegistryHelpers.get_asset_registry()
# A same-turn dependency query can still see the pre-import index. Refresh only
# this saved table before checking the soft package references used by Cook.
assert unreal.EditorAssetLibrary.save_loaded_asset(table, False)
registry.scan_files_synchronous([str(ROOT / 'Content/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects.uasset')], True)
options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,
                                                include_hard_package_references=True)
dependencies = {str(path) for path in registry.get_dependencies(table.get_path_name().split('.')[0], options)}
native_rows = []
for row in proposal['rows']:
    saved = next(value for value in saved_rows if value['Id'] == row['id'])
    definition = unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(row['id'])
    assert definition and definition.resource_path.get_path_name() == row['new_resource']
    scale = [getattr(definition.scale, axis) for axis in ['x', 'y', 'z']]
    assert all(math.isclose(a, float(b), abs_tol=1.e-6) for a, b in zip(scale, row['base_scale']))
    assert row['new_resource'] in saved['ResourcePath']
    assert row['new_resource'].split('.')[0] in dependencies
    native_rows.append({'id': row['id'], 'row_name': saved['Name'],
                        'resource_path': definition.resource_path.get_path_name(), 'base_scale': scale,
                        'cook_dependency_present': True})

catalog = unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_CommanderCombatEffects')
assert catalog.machine_gun_impact.vfx_id == 5
assert catalog.machine_gun_impact.maximum_lifetime == 3
report = {'success': True, 'approval': proposal['authorization'],
          'table': table.get_path_name(), 'all_52_rows_match_source': True,
          'rows': native_rows, 'blueprints': blueprints,
          'impact_consumer': {'catalog': catalog.get_path_name(), 'vfx_id': 5, 'maximum_lifetime': 3},
          'pie_worlds': 0, 'scope': 'Authorized combined reference publication and static native/consumer readback.'}
(OUT / 'formal-reference-final-readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
proposal.update(applied=True, applied_date='2026-10-09',
                native_readback='formal-reference-final-readback.json')
proposal_path.write_text(json.dumps(proposal, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'applied_ids': [row['id'] for row in native_rows],
                                              'rows_verified': len(saved_rows), 'blueprints': blueprints}))
