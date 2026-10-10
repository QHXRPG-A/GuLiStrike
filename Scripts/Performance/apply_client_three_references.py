"""Save the explicitly authorized production references; no PIE-local overrides.

Acceptance stays separate: setting an input protocol version enables its
runtime path; the actual resource checks are recorded by the validation run.
"""
import contextlib
import io
import json
import math
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'outputs/performance/20261009-client-three-optimizations'
DEST = '/Game/GuLiStrike/FX/CommanderWeapons'
AS = unreal.EditorAssetLibrary
NS = unreal.NiagaraService
NATIVE = unreal.GuLiCombatEffectAuthoringLibrary
proposal_path = OUT / 'client-three-formal-proposal.json'
proposal = json.loads(proposal_path.read_text(encoding='utf-8'))
assert proposal['authorization'] == '应用所有新的变更'
assert not unreal.EditorLevelLibrary.get_pie_worlds(True), 'Asset publication requires no PIE Worlds'

# Clear the earlier preview callback before changing saved assets. Its old
# restoration values must never overwrite this new production configuration.
old_review = globals().get('guli_client_three_review_state', {})
if old_review.get('handle'):
    guli_client_three_review_close()
old_review['handle'] = None
old_review['phase'] = 'superseded_by_formal_application'

table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects')
before = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
source = json.loads((ROOT / 'Data/Json/DT_GuLiStrikeVfx_Effects.json').read_text(encoding='utf-8'))
assert len(before) in (52, 53) and len(source) == 53
assets, diagnostics = {}, []
for row in proposal['rows']:
    for change in row['changes'].values():
        path = change['after']
        asset = unreal.load_object(None, path)
        assert isinstance(asset, unreal.NiagaraSystem), path
        assets[path] = asset
for path, asset in assets.items():
    diagnostic = NATIVE.get_war_machine_hover_compile_diagnostics(asset)
    assert 'VM ERROR:' not in diagnostic and 'GPU ERROR:' not in diagnostic, (path, diagnostic)
    diagnostics.append({'path': path, 'diagnostics': diagnostic})
channel = unreal.load_asset(DEST + '/NDC_CommanderImpacts')
profile = unreal.load_asset(DEST + '/DA_WingmanGroundFlightPresentation')
definition = unreal.load_asset('/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundMissile')
style = unreal.load_asset('/Game/GuLiStrike/FX/GroundWarning/DA_GroundWarning_Red')
catalog = unreal.load_asset(DEST + '/DA_CommanderCombatEffects')
assert channel and profile and definition and style and catalog
assert profile.get_editor_property('BatchSystem').get_path_name() == next(
    r for r in source if r['Id'] == 53)['ResourcePath']
assert catalog.machine_gun_impact.vfx_id == 5
assert catalog.machine_gun_impact.maximum_lifetime == 3
batch_metadata = []
for suffix in ('', '_Reduced', '_Minimal'):
    path = DEST + '/NS_MachineGunImpact_Batch' + suffix
    parameter = NS.get_parameter(path, 'User.GuLiImpactInputVersion')
    assert parameter, path
    assert NS.set_parameter(path, 'User.GuLiImpactInputVersion', '1'), path
    assert NS.save_system(path), path
    assert float(NS.get_parameter(path, 'User.GuLiImpactInputVersion').current_value) == 1, path
    batch_metadata.append({'path': path, 'input_version': 1, 'saved': True})
catalog.set_editor_property('ImpactChannel', channel)
definition.set_editor_property('FlightPresentationProfile', profile)
definition.set_editor_property('GroundWarningStyle', style)
assert AS.save_loaded_asset(catalog, False)
assert AS.save_loaded_asset(definition, False)

scope = {'__name__': '__main__', 'GULI_TABLE_FILTER': {'DT_GuLiStrikeVfx_Effects'},
         'GULI_IMPORT_REPORT': str(OUT / 'client-three-formal-import-report.json'),
         'GULI_IMPORT_PROGRESS': str(OUT / 'client-three-formal-import-progress.log')}
with contextlib.redirect_stdout(io.StringIO()):
    exec(compile((ROOT / 'Scripts/import_data_to_engine.py').read_text(encoding='utf-8'),
                 'client_three_formal_import', 'exec'), scope)
imported = json.loads((OUT / 'client-three-formal-import-report.json').read_text(encoding='utf-8'))
assert not imported['errors'] and len(imported['tables']) == 1 and imported['tables'][0]['imported'], imported
assert scope['vfx_table_matches_source'](table, source)
after = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
old_rows = {r['Id']: r for r in before}
changed = {r['id']: set(r['changes']) for r in proposal['rows']}
for row in after:
    if row['Id'] not in old_rows:
        assert row['Id'] == 53 and all(float(v) == 1. for v in row['Scale'].values()), row
        continue
    old = old_rows[row['Id']]
    assert row['Scale'] == old['Scale'], row
    for key in set(old) | set(row):
        if key not in changed.get(row['Id'], set()):
            assert row.get(key) == old.get(key), (row['Id'], key)
assert AS.save_loaded_asset(table, False)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_files_synchronous([str(ROOT / 'Content/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects.uasset')], True)
options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,
                                                include_hard_package_references=True)
dependencies = {str(p) for p in registry.get_dependencies(table.get_path_name().split('.')[0], options)}
native_rows = []
for row in proposal['rows']:
    result = unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(row['id'])
    assert result
    scale = [result.scale.x, result.scale.y, result.scale.z]
    assert all(math.isclose(a, b, abs_tol=1.e-6) for a, b in zip(scale, row['base_scale']))
    paths = {}
    for field, change in row['changes'].items():
        value = result.get_editor_property(field)
        assert value.get_path_name() == change['after'], (row['id'], field, value)
        assert change['after'].split('.')[0] in dependencies, change
        paths[field] = value.get_path_name()
    native_rows.append({'id': row['id'], 'paths': paths, 'scale': scale, 'cook_dependencies': True})
assert catalog.get_editor_property('ImpactChannel') == channel
assert definition.get_editor_property('FlightPresentationProfile') == profile
assert definition.get_editor_property('GroundWarningStyle') == style
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
controls = {'gs.Effects.BoundsCull': 1, 'gs.Effects.OffscreenLifecycle': 1,
            'gs.Effects.ThreeTierLOD': 1, 'gs.Units.Offscreen5Hz': 1,
            'gs.Flights.OffscreenPresentation': 1, 'gs.Impacts.BatchMode': 2,
            'gs.WingmanFlight.Presentation': 1, 'gs.WingmanFlight.Warnings': 1}
for key, value in controls.items():
    unreal.SystemLibrary.execute_console_command(world, f'{key} {value}')
assert all(unreal.SystemLibrary.get_console_variable_float_value(k) == v for k, v in controls.items())
receipt = {'success': True, 'authorization': proposal['authorization'], 'applied_date': '2026-10-10',
           'table': table.get_path_name(), 'rows': len(after), 'all_rows_match_source': True,
           'native_rows': native_rows, 'batch_metadata': batch_metadata,
           'catalog_channel': channel.get_path_name(), 'ground_definition': definition.get_path_name(),
           'flight_profile': profile.get_path_name(), 'warning_style': style.get_path_name(),
           'controls': controls, 'compile_diagnostics': diagnostics,
           'visual_acceptance': 'pending_user_pie', 'performance': 'deferred_by_user', 'pie_worlds': 0}
(OUT / 'client-three-formal-readback.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2), encoding='utf-8')
proposal.update(applied=True, applied_date='2026-10-10', readback='client-three-formal-readback.json')
proposal_path.write_text(json.dumps(proposal, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'applied_ids': [5, 36, 45, 52, 53],
                                              'production_ground_profile': True, 'batch_input_version': 1}))
