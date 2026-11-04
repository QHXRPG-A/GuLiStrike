"""Build and save the Ground mobility area, then read its entities and current manifests."""
import json
import math
import tempfile
from pathlib import Path

import unreal

root = Path(unreal.Paths.project_dir()).resolve()
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert not level.is_in_play_in_editor()
assert world.get_path_name().split('.')[0] == '/Game/Maps/LVL_CommanderMassPrototype'
subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
mode = unreal.get_default_object(world.get_world_settings().get_editor_property('default_game_mode'))
pawn_class = mode.get_editor_property('default_ground_pawn_class')
assert pawn_class.get_path_name() == '/Game/GuLiStrike/GroundMech/BP_GroundMech_Light.BP_GroundMech_Light_C'
pawn = unreal.get_default_object(pawn_class)
mesh = pawn.get_editor_property('mesh')
production = dict(model_id=pawn.get_editor_property('model_id'),
                  mesh=mesh.get_editor_property('skeletal_mesh_asset').get_path_name(),
                  anim_class=mesh.get_editor_property('anim_class').get_path_name())
base = '/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2'
assert production == dict(model_id=4001, mesh=base+'/Meshes/SK_GroundMech_A4B2.SK_GroundMech_A4B2',
                          anim_class=base+'/Animations/ABP_GroundMech_A4B2.ABP_GroundMech_A4B2_C')
config = json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.configure_mobility_assets(False))
assert config['success'], config
abp = unreal.load_asset(base+'/Animations/ABP_GroundMech_A4B2').get_path_name()
aim = [n for n in unreal.BlueprintService.get_nodes_in_graph(abp, 'AnimGraph')
       if n.node_type == 'AnimGraphNode_GuLiGroundMechAim']
assert len(aim) == 1
pins = unreal.BlueprintService.get_node_pins(abp, 'AnimGraph', aim[0].node_id)
assert any(p.pin_name == 'UpperBodyFrame' and p.is_connected for p in pins)
manifest_dir = root/'Config/Hybrid'
paths = {size: manifest_dir/f'RuntimeManifest-{size}.json' for size in (200, 600)}
original = {size: path.read_bytes() for size, path in paths.items()}
versions = set()
for raw in original.values():
    manifest = json.loads(raw.decode('utf-8-sig'))
    assert manifest['strings']['ground/mobility_configuration'] == config['configuration_fingerprint']
    versions.add(int(manifest['numbers']['adapter/family/GroundPlayer/state_version']))
assert len(versions) == 1

# Use the same permanent labels/geometry so running this twice updates the existing actors.
actors = {}
for actor in subsystem.get_all_level_actors():
    label = actor.get_actor_label()
    if label.startswith(('Ground3C_', 'GroundMobility_')):
        if label in actors:
            raise RuntimeError(f'Duplicate review actor {label}')
        actors[label] = actor
folder = 'Review/GroundMobility_20261024'
cube = unreal.load_asset('/Engine/BasicShapes/Cube')
floor_z = 905.
angle = math.radians(18.)
specs = [
    ('GroundMobility_Flat', (-4000, 50000, floor_z-25), (30, 40, .5), 0),
    ('GroundMobility_Slope18', (-4250, 48900, floor_z+700*math.sin(angle)-20*math.cos(angle)), (14, 10, .4), 18),
    ('GroundMobility_SlopeLanding', (-3320, 48900, floor_z+1400*math.sin(angle)-20), (5.4, 10, .4), 0),
]
for i, height in enumerate((20, 40, 60, 80)):
    specs.append((f'GroundMobility_Step{height}', (-4800+400*i, 50900, floor_z+height/2), (4, 10, height/100), 0))
for suffix, y in (('LookLeft', 49000), ('LookRight', 51000)):
    specs.append((f'Ground3C_{suffix}', (-5300, y, 1055), (1.5, 1.5, 3.), 0))

def add_tag(actor, tag):
    tags = list(actor.get_editor_property('tags'))
    name = unreal.Name(tag)
    if name not in tags:
        actor.set_editor_property('tags', tags+[name])

for name, position, scale, pitch in specs:
    actor = actors.get(name)
    if actor is None:
        actor = subsystem.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*position))
        actors[name] = actor
    assert isinstance(actor, unreal.StaticMeshActor), name
    actor.modify()
    actor.set_actor_label(name)
    actor.set_folder_path(folder)
    actor.set_actor_location(unreal.Vector(*position), False, False)
    actor.set_actor_rotation(unreal.Rotator(pitch=pitch, yaw=0, roll=0), False)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    actor.set_actor_enable_collision(True)
    component = actor.static_mesh_component
    component.modify()
    component.set_static_mesh(cube)
    component.set_mobility(unreal.ComponentMobility.STATIC)
    component.set_collision_profile_name('BlockAll')
    component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    if name.startswith('Ground3C_'):
        material = actors['GroundMobility_Flat'].static_mesh_component.get_material(0)
        if material:
            component.set_material(0, material)
        add_tag(actor, 'Ground3CReview')
    else:
        add_tag(actor, 'GroundMobilityReview')

entry_name = 'GroundMobility_Entry'
entry_location = unreal.Vector(-2800, 50000, 1149.04)
entry = actors.get(entry_name)
if entry is None:
    entry = subsystem.spawn_actor_from_class(unreal.TargetPoint, entry_location)
assert isinstance(entry, unreal.TargetPoint)
entry.modify()
entry.set_actor_label(entry_name)
entry.set_folder_path(folder)
entry.set_actor_location(entry_location, False, False)
entry.set_actor_rotation(unreal.Rotator(pitch=0, yaw=180, roll=0), False)
add_tag(entry, 'Ground3CReview')

navigation = []
for volume in subsystem.get_all_level_actors():
    if not isinstance(volume, unreal.GuLiFlightNavigationVolume):
        continue
    asset = volume.get_editor_property('navigation_data')
    settings = volume.get_editor_property('authoring_bake_settings')
    assert asset, volume.get_path_name()
    nav = unreal.GuLiFlightNavigationEditorLibrary
    rebaked = nav.validate_volume(volume, settings) is None
    if rebaked:
        assert nav.bake_volume(volume, settings) is not None
        assert unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
    assert nav.validate_volume(volume, settings) is not None
    meta = asset.get_editor_property('metadata')
    navigation.append(dict(volume=volume.get_path_name(), asset=asset.get_path_name(), rebaked=rebaked,
                           bake_id=meta.get_editor_property('bake_id').to_string(),
                           geometry_signature=meta.get_editor_property('geometry_signature')))
assert level.save_current_level()
entities = []
expected = {name for name, *_ in specs} | {entry_name}
for actor in subsystem.get_all_level_actors():
    label = actor.get_actor_label()
    if label not in expected:
        continue
    row = dict(label=label, type=actor.get_class().get_name(), location=list(actor.get_actor_location().to_tuple()),
               rotation_roll_pitch_yaw=list(actor.get_actor_rotation().to_tuple()), scale=list(actor.get_actor_scale3d().to_tuple()))
    if label == entry_name:
        assert math.dist(row['location'], entry_location.to_tuple()) < .01
    else:
        part = actor.static_mesh_component
        row.update(mesh=part.static_mesh.get_path_name(), collision_profile=str(part.get_collision_profile_name()))
        assert row['mesh'] == cube.get_path_name() and row['collision_profile'] == 'BlockAll', label
    entities.append(row)
assert len(entities) == len(expected) and {e['label'] for e in entities} == expected

manifests = []
# Export both sizes to scratch first. An Editor loaded from an older build must not replace the active configuration.
with tempfile.TemporaryDirectory(dir=manifest_dir, prefix='ground-mobility-') as directory:
    scratch = Path(directory)
    for size,path in paths.items():
        candidate = scratch/path.name
        result = json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(size, str(candidate)))
        assert result['success'], result
        manifest = json.loads(candidate.read_text(encoding='utf-8-sig'))
        fingerprint = manifest['strings']['ground/mobility_configuration']
        version = int(manifest['numbers']['adapter/family/GroundPlayer/state_version'])
        assert fingerprint == config['configuration_fingerprint'] and version in versions
        manifests.append(dict(population=size, config_hash=manifest['config_hash'], fingerprint=fingerprint, state_version=version))
    changed = []
    try:
        for size,path in paths.items():
            candidate = scratch/path.name
            if path.read_bytes() != original[size]:
                raise RuntimeError(f'Concurrent change to {path.name}')
            if candidate.read_bytes() != original[size]:
                candidate.replace(path)
                changed.append(size)
    except (OSError, RuntimeError):
        for size in changed:
            backup = scratch/paths[size].name
            backup.write_bytes(original[size])
            backup.replace(paths[size])
        raise
report = dict(success=True, map=world.get_path_name(), saved=True, map_saved=True, entities=entities,
              default_ground_pawn=pawn_class.get_path_name(), ground_pawn=pawn_class.get_path_name(),
              production_references=production, manifests=manifests, navigation=navigation, pie_started=False)
if 'GULI_SCENE_REPORT' in globals():
    # Existing callers can supply an absolute path or the old UE_Export-relative report name.
    path = root/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export'/GULI_SCENE_REPORT
else:
    path = root/'Artifacts/GroundMobility/scene-delivery.json'
path.parent.mkdir(parents=True, exist_ok=True)
path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True, entities=len(entities), report=str(path))))
