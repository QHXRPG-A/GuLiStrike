"""Save only the opt-in muzzle comparison entities in the existing review map."""
import json
from pathlib import Path
import unreal

assert not unreal.EditorLevelLibrary.get_pie_worlds(True), 'Finish the owned capture PIE first'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_name() == 'LVL_CommanderMassPrototype'
api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
existing = {a.get_actor_label(): a for a in api.get_all_level_actors()}
tag = unreal.Name('GuLiMuzzleBatchReview')
dest = '/Game/GuLiStrike/FX/CommanderWeapons/'
full = unreal.load_asset(dest + 'NS_MachineGunMuzzle_AllOptimizations')
batch = unreal.load_asset(dest + 'NS_MachineGunMuzzle_Batch')
assert full and batch
row = unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(52)
assert list(row.scale.to_tuple()) == [1, 1, 1]


def entity(label, cls, location):
    a = existing.get(label)
    if a is None:
        a = api.spawn_actor_from_class(cls, unreal.Vector(*location))
        assert a
        a.set_actor_label(label)
        existing[label] = a
    assert isinstance(a, cls), label
    if tag not in a.tags:
        a.tags = [*a.tags, tag]
    a.set_actor_location(unreal.Vector(*location), False, True)
    return a


for label, mode, heavy, location in [
    ('MuzzleReview_Old_Normal', 0, False, (-21500, -18000, 1700)),
    ('MuzzleReview_Batch_Normal', 2, False, (-18000, -18000, 1700)),
    ('MuzzleReview_Old_Heavy', 0, True, (-21500, -14500, 1700)),
    ('MuzzleReview_Batch_Heavy', 2, True, (-18000, -14500, 1700))]:
    a = entity(label, unreal.GuLiPerformanceReviewActor, location)
    a.set_actor_rotation(unreal.Rotator(0, -30 if heavy else 30, 0), True)
    for key, value in {'System': full if mode == 0 else batch, 'BaseScale': row.scale,
                       'bLaser': False, 'bCatalogImpacts': False, 'bCatalogMuzzles': True,
                       'MuzzleMode': mode, 'bHeavyMuzzle': heavy, 'PolicyEffectId': 0,
                       'EffectCount': 1, 'ImpactEventsPerSecond': 20., 'ActiveSeconds': 2.4,
                       'CycleSeconds': 4., 'MuzzleFollowVelocity': unreal.Vector(600, 150, 0)}.items():
        a.set_editor_property(key, value)
    a.stop_comparison()

for tier, location in [('Near', (-19750, -21100, 3700)),
                       ('Middle', (-19750, -27000, 7000)),
                       ('Far', (-19750, -33000, 7000))]:
    cam = entity('MuzzleReview_Camera_' + tier, unreal.CameraActor, location)
    cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(
        cam.get_actor_location(), unreal.Vector(-19750, -16250, 2000)), True)
    cam.get_component_by_class(unreal.CameraComponent).set_editor_property('field_of_view', 75.)

guide = entity('MuzzleReview_Guide', unreal.TextRenderActor, (-22500, -20100, 2300))
text = guide.get_component_by_class(unreal.TextRenderComponent)
text.set_text('MUZZLE REVIEW: old immediate / new F+1 batch.\n'
              'Left: old. Right: batch. Upper row: WM01 x2 once.\n'
              'Execute Scripts/Performance/muzzle_player_review.py\n'
              'guli_muzzle_review_view("near") / ("middle") / ("far")\n'
              'guli_muzzle_review_view("offscreen") / ("game")\n'
              'guli_muzzle_review_stop() - stop emission, let tails finish.\n'
              'Opt-in cosmetic preview only; no gameplay damage or network events.')
text.set_world_size(50.)
assert unreal.EditorLoadingAndSavingUtils.save_map(world, '/Game/Maps/LVL_CommanderMassPrototype')

entities = []
for a in api.get_all_level_actors():
    if tag not in a.tags:
        continue
    data = {'label': a.get_actor_label(), 'name': a.get_name(),
            'class': a.get_class().get_name(), 'map': a.get_world().get_path_name(),
            'location': list(a.get_actor_location().to_tuple()),
            'rotation': str(a.get_actor_rotation())}
    if isinstance(a, unreal.GuLiPerformanceReviewActor):
        data.update(system=a.get_editor_property('System').get_path_name(),
                    base_scale=list(a.get_editor_property('BaseScale').to_tuple()),
                    catalog_muzzles=a.get_editor_property('bCatalogMuzzles'),
                    mode=a.get_editor_property('MuzzleMode'), heavy=a.get_editor_property('bHeavyMuzzle'),
                    tick_enabled=a.is_actor_tick_enabled(),
                    events_per_second=a.get_editor_property('ImpactEventsPerSecond'),
                    follow_velocity=list(a.get_editor_property('MuzzleFollowVelocity').to_tuple()),
                    active_seconds=a.get_editor_property('ActiveSeconds'),
                    cycle_seconds=a.get_editor_property('CycleSeconds'))
        assert data['catalog_muzzles'] and not data['tick_enabled']
        assert data['base_scale'] == [1, 1, 1]
    entities.append(data)
assert len(entities) == 8, entities
out = Path(unreal.Paths.project_dir()) / 'outputs/performance/20261010-muzzle-batch'
out.mkdir(parents=True, exist_ok=True)
(out / 'manual-review-scene-readback.json').write_text(json.dumps({
    'success': True, 'saved': True, 'map': '/Game/Maps/LVL_CommanderMassPrototype',
    'entities': entities, 'technical_entity_readback': True, 'player_visual_approval': False,
    'entry': 'Scripts/Performance/muzzle_player_review.py -> guli_muzzle_review_view("near")'
}, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'saved': True,
                                             'entities': len(entities)}))
