"""Read back only current muzzle references and saved review entities; no asset writes."""
import json
from pathlib import Path
import unreal

assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
out = Path(unreal.Paths.project_dir()) / 'outputs/performance/20261010-muzzle-batch'
table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects')
rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
row = next(r for r in rows if r['Id'] == 52)
catalog = unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_CommanderCombatEffects')
dest = '/Game/GuLiStrike/FX/CommanderWeapons/'
assert list(unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(52).scale.to_tuple()) == [1, 1, 1]
for field, suffix in [('BatchResourcePath', ''), ('ReducedBatchResourcePath', '_Reduced'),
                      ('MinimalBatchResourcePath', '_Minimal')]:
    assert row[field] == dest + 'NS_MachineGunMuzzle_Batch' + suffix + '.NS_MachineGunMuzzle_Batch' + suffix
systems = []
for suffix in ('', '_Reduced', '_Minimal'):
    path = dest + 'NS_MachineGunMuzzle_Batch' + suffix
    version = float(unreal.NiagaraService.get_parameter(path, 'User.GuLiMuzzleInputVersion').current_value)
    life = float(unreal.NiagaraService.get_parameter(path, 'User.GuLiMuzzleMaximumLifetime').current_value)
    assert version == 1 and life >= 1
    systems.append({'path': path, 'input_version': version, 'maximum_lifetime': life,
                    'emitters': [str(e.emitter_name) for e in unreal.NiagaraService.list_emitters(path)]})
assert catalog.get_editor_property('MuzzleChannel').get_path_name() == dest + 'NDC_CommanderMuzzles.NDC_CommanderMuzzles'
cert = json.loads((out / 'muzzle-certification-readback.json').read_text(encoding='utf-8-sig'))
assert catalog.get_editor_property('ImpactChannel').get_path_name() == cert['impact_channel_unchanged']
api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
entities = []
for actor in api.get_all_level_actors():
    if unreal.Name('GuLiMuzzleBatchReview') not in actor.tags:
        continue
    entry = {'label': actor.get_actor_label(), 'class': actor.get_class().get_name(),
             'map': actor.get_world().get_path_name(), 'location': list(actor.get_actor_location().to_tuple())}
    if isinstance(actor, unreal.GuLiPerformanceReviewActor):
        entry.update(mode=actor.get_editor_property('MuzzleMode'),
                     heavy=actor.get_editor_property('bHeavyMuzzle'),
                     tick_enabled=actor.is_actor_tick_enabled(),
                     system=actor.get_editor_property('System').get_path_name())
        assert not entry['tick_enabled']
    entities.append(entry)
assert len(entities) == 8
mode = unreal.SystemLibrary.get_console_variable_float_value('gs.Muzzles.BatchMode')
assert mode == 2
proof = {'success': True, 'current_mode': mode, 'official_id52_row': row,
         'muzzle_channel': catalog.get_editor_property('MuzzleChannel').get_path_name(),
         'impact_channel_unchanged': catalog.get_editor_property('ImpactChannel').get_path_name(),
         'systems': systems, 'saved_review_entities': entities, 'player_visual_approval': False,
         'scope': 'Saved resources, certified input version and map entities read back after final build and visual preview.'}
(out / 'delivery-readback.json').write_text(json.dumps(proof, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'mode': mode, 'entities': len(entities),
                                             'certified_systems': len(systems)}))
