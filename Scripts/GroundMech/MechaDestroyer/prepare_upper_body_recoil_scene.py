"""Save a firing target in the existing Ground area and read only related entities."""
import json
from pathlib import Path

import unreal


def main():
    root = Path(unreal.Paths.project_dir()).resolve()
    out = root / 'Artifacts/GroundUpperBodyRecoil20261028'
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if level.is_in_play_in_editor():
        raise RuntimeError('Scene authoring must not interrupt an active play session')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world.get_path_name().split('.')[0] != '/Game/Maps/LVL_CommanderMassPrototype':
        raise RuntimeError('Open the existing Ground review map')
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    labels = ('GroundMobility_Entry', 'GroundMobility_Flat', 'GroundUpperBodyRecoil_Target')
    related = {}
    for actor in actors.get_all_level_actors():
        name = actor.get_actor_label()
        if name in labels:
            if name in related:
                raise RuntimeError(f'Duplicate review entity: {name}')
            related[name] = actor
    if any(name not in related for name in labels[:2]):
        raise RuntimeError('The existing Ground entry/flat platform is missing')
    game_mode = unreal.get_default_object(world.get_world_settings().get_editor_property('default_game_mode'))
    pawn_class = game_mode.get_editor_property('default_ground_pawn_class')
    if 'BP_GroundMech_Light' not in str(pawn_class):
        raise RuntimeError('This map is not routed to the production Ground mech')
    entry = related['GroundMobility_Entry']
    entry.modify()
    tags = list(entry.get_editor_property('tags'))
    marker = unreal.Name('GuLi.Review.GroundUpperBodyRecoil')
    if marker not in tags:
        tags.append(marker)
    entry.set_editor_property('tags', tags)
    target = related.get(labels[2])
    if target is None:
        target = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(-5000, 50000, 1155))
    if not isinstance(target, unreal.StaticMeshActor):
        raise RuntimeError('Review target must be a StaticMeshActor')
    target.modify()
    target.set_actor_label(labels[2])
    target.set_folder_path('Review/GroundMobility_20261024')
    target.set_actor_location(unreal.Vector(-5000, 50000, 1155), False, False)
    target.set_actor_rotation(unreal.Rotator(0, 0, 0), False)
    target.set_actor_scale3d(unreal.Vector(.5, 4, 5))
    target.set_actor_enable_collision(True)
    target.set_editor_property('tags', [marker])
    component = target.static_mesh_component
    component.modify()
    component.set_editor_property('can_ever_affect_navigation', False)
    component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    component.set_mobility(unreal.ComponentMobility.STATIC)
    component.set_collision_profile_name('BlockAll')
    component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    if not level.save_current_level():
        raise RuntimeError('Could not save the review map')
    # Read back only the entry, existing platform and newly authored target.
    readback = []
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label() not in labels:
            continue
        row = {'label': actor.get_actor_label(), 'class': actor.get_class().get_name(),
               'location': list(actor.get_actor_location().to_tuple()),
               'rotation': list(actor.get_actor_rotation().to_tuple()),
               'scale': list(actor.get_actor_scale3d().to_tuple()),
               'tags': list(map(str, actor.get_editor_property('tags')))}
        if isinstance(actor, unreal.StaticMeshActor):
            mesh = actor.static_mesh_component
            row.update(mesh=mesh.static_mesh.get_path_name(),
                       collision=str(mesh.get_collision_enabled()), profile=str(mesh.get_collision_profile_name()))
            if row['label'] == labels[2]:
                row['affects_navigation'] = mesh.get_editor_property('can_ever_affect_navigation')
                if row['affects_navigation'] or row['profile'] != 'BlockAll':
                    raise RuntimeError('Target navigation/collision configuration was not saved')
        readback.append(row)
    if len(readback) != len(labels):
        raise RuntimeError('Incomplete scene readback')
    install_path = out / 'asset_install.json'
    install = json.loads(install_path.read_text(encoding='utf8')) if install_path.exists() else {}
    report = {'success': True, 'map': world.get_path_name(), 'saved': True, 'entities': readback,
              'ground_pawn_class': str(pawn_class), 'assets_installed': install.get('success', False),
              'new_effect_playable': install.get('success', False), 'play_started': False,
              'visual_acceptance': 'pending_user',
              'entry': 'Select Ground in local PIE. Existing red Ground spawn is near (-2175,58575,1148.8); walk south to the existing platform. Entry (-2800,50000,1149.04) faces the target (-5000,50000,1155). The entry marker does not change spawn rules.'}
    out.mkdir(parents=True, exist_ok=True)
    (out / 'scene_delivery.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    return report


unreal.MCPythonHelper.submit_result(json.dumps(main(), ensure_ascii=False))
