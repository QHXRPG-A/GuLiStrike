"""Save the existing Ground mobility area for player 3C acceptance; never start PIE.

Default stage prepares the scene while native compilation/camera capture are
pending. GULI_3C_FINALIZE=True additionally requires the completed scoped import
and refreshes both manifests from the loaded native registry.
"""
import json
import math
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Artifacts/Ground3C_20261103'
FINALIZE = bool(globals().get('GULI_3C_FINALIZE', False))
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert not level.is_in_play_in_editor()
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0] == '/Game/Maps/LVL_CommanderMassPrototype'
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
existing = {}
for actor in actors.get_all_level_actors():
    label = actor.get_actor_label()
    if label.startswith(('GroundMobility_', 'Ground3C_')):
        assert label not in existing, ('Duplicate review actor', label)
        existing[label] = actor
entry = existing['GroundMobility_Entry']
floor = existing['GroundMobility_Flat']
assert all(math.isclose(a, b, abs_tol=.01) for a, b in zip(entry.get_actor_location().to_tuple(), (-2800., 50000., 1149.04)))
assert isinstance(floor, unreal.StaticMeshActor)
entry.modify()
tags = list(entry.get_editor_property('tags'))
if unreal.Name('Ground3CReview') not in tags:
    entry.set_editor_property('tags', tags + [unreal.Name('Ground3CReview')])
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
material = floor.static_mesh_component.get_material(0)
for suffix, position in [('LookLeft', (-5300., 49000., 1055.)), ('LookRight', (-5300., 51000., 1055.))]:
    name = 'Ground3C_' + suffix
    actor = existing.get(name)
    if actor is None:
        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*position))
    assert isinstance(actor, unreal.StaticMeshActor)
    actor.modify()
    actor.set_actor_label(name)
    actor.set_folder_path('Review/GroundMobility_20261024')
    actor.set_actor_location(unreal.Vector(*position), False, False)
    actor.set_actor_rotation(unreal.Rotator(0, 0, 0), False)
    actor.set_actor_scale3d(unreal.Vector(1.5, 1.5, 3.))
    actor.set_actor_enable_collision(True)
    component = actor.static_mesh_component
    component.modify()
    component.set_static_mesh(cube)
    component.set_mobility(unreal.ComponentMobility.STATIC)
    component.set_collision_profile_name('BlockAll')
    if material:
        component.set_material(0, material)
    actor.set_editor_property('tags', [unreal.Name('Ground3CReview')])

# The new static aiming references participate in the map's existing flight
# collision signature. Refresh stale authored data before saving/exporting.
navigation = []
for volume in actors.get_all_level_actors():
    if not isinstance(volume, unreal.GuLiFlightNavigationVolume):
        continue
    asset = volume.get_editor_property('navigation_data')
    assert asset, volume.get_path_name()
    settings = volume.get_editor_property('authoring_bake_settings')
    rebaked = unreal.GuLiFlightNavigationEditorLibrary.validate_volume(volume, settings) is None
    if rebaked:
        assert unreal.GuLiFlightNavigationEditorLibrary.bake_volume(volume, settings) is not None, volume.get_path_name()
        assert unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
    assert unreal.GuLiFlightNavigationEditorLibrary.validate_volume(volume, settings) is not None
    metadata = asset.get_editor_property('metadata')
    navigation.append(dict(volume=volume.get_path_name(), asset=asset.get_path_name(), rebaked=rebaked,
                           bake_id=metadata.get_editor_property('bake_id').to_string(),
                           geometry_signature=metadata.get_editor_property('geometry_signature')))
assert level.save_current_level()

entities = []
for actor in actors.get_all_level_actors():
    name = actor.get_actor_label()
    if not name.startswith(('GroundMobility_', 'Ground3C_')):
        continue
    item = dict(label=name, type=actor.get_class().get_name(), location=list(actor.get_actor_location().to_tuple()),
                rotation_roll_pitch_yaw=list(actor.get_actor_rotation().to_tuple()), scale=list(actor.get_actor_scale3d().to_tuple()))
    if isinstance(actor, unreal.StaticMeshActor):
        item.update(mesh=actor.static_mesh_component.static_mesh.get_path_name(),
                    collision_profile=str(actor.static_mesh_component.get_collision_profile_name()))
    entities.append(item)
assert {'GroundMobility_Entry', 'GroundMobility_Flat', 'Ground3C_LookLeft', 'Ground3C_LookRight'}.issubset({e['label'] for e in entities})
for item in entities:
    if item['label'].startswith('Ground3C_'):
        assert item['mesh'] == '/Engine/BasicShapes/Cube.Cube' and item['collision_profile'] == 'BlockAll'
        assert item['scale'] == [1.5, 1.5, 3.]
mode = unreal.get_default_object(world.get_world_settings().get_editor_property('default_game_mode'))
pawn_class = mode.get_editor_property('default_ground_pawn_class')
pawn = pawn_class.get_path_name()
assert pawn == '/Game/GuLiStrike/GroundMech/BP_GroundMech_Light.BP_GroundMech_Light_C'
cdo = unreal.get_default_object(pawn_class)
component = cdo.get_editor_property('mesh')
production = dict(model_id=cdo.get_editor_property('model_id'),
                  mesh=component.get_editor_property('skeletal_mesh_asset').get_path_name(),
                  anim_class=component.get_editor_property('anim_class').get_path_name())
assert production['model_id'] == 4001
assert production['mesh'] == '/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Meshes/SK_GroundMech_A4B2.SK_GroundMech_A4B2'
assert production['anim_class'] == '/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Animations/ABP_GroundMech_A4B2.ABP_GroundMech_A4B2_C'
manifests = []
if FINALIZE:
    imported = json.loads((OUT / 'asset-import.json').read_text(encoding='utf8'))
    assert not imported['errors'] and imported['ground_mobility']['success']
    for population in (200, 600):
        path = ROOT / f'Config/Hybrid/RuntimeManifest-{population}.json'
        result = json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population, str(path)))
        assert result['success'], result
        manifest = json.loads(path.read_text(encoding='utf-8-sig'))
        assert manifest['strings']['ground/mobility_configuration'] == imported['ground_mobility']['configuration_fingerprint']
        assert int(manifest['numbers']['adapter/family/GroundPlayer/state_version']) == 12
        manifests.append(dict(population=population, config_hash=manifest['config_hash']))
report = dict(success=True, map=world.get_path_name(), map_saved=True, entities=entities,
              ground_pawn=pawn, production_references=production, navigation=navigation,
              manifests=manifests, new_runtime_ready=FINALIZE,
              pie_started=False, visual_validation='user acceptance pending',
              note='GroundMobility_Entry is a locator; enter the map through the normal Ground seat and use the existing GM teleport to reach it.')
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'scene-delivery.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'map': report['map'], 'entities': len(entities),
    'new_runtime_ready': FINALIZE, 'report': str(OUT / 'scene-delivery.json')}))
