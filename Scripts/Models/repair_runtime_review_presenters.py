"""Keep historical LOD previews while removing their gameplay presentation behavior."""
import json
from pathlib import Path
import unreal

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
assert editor.get_editor_world().get_path_name().split('.')[0] == '/Game/Maps/LVL_CommanderMassPrototype'
report = []
for old in list(actors.get_all_level_actors()):
    if not isinstance(old, unreal.GuLiCommanderPresentationActor):
        continue
    assert old.actor_has_tag('CommanderLODReview20261005') and old.get_editor_property('is_editor_only_actor'), old.get_actor_label()
    comp = next(c for c in old.get_components_by_class(unreal.InstancedStaticMeshComponent) if c.get_name() == 'UnitInstances')
    assert comp.get_instance_count() == 1, old.get_actor_label()
    pose = comp.get_instance_transform(0, True)
    label = old.get_actor_label()
    mesh = comp.static_mesh
    materials = list(comp.get_materials())
    lod = comp.get_editor_property('forced_lod_model')
    folder = old.get_folder_path()
    tags = list(old.tags)
    new = actors.spawn_actor_from_class(unreal.StaticMeshActor, pose.translation)
    new.set_actor_transform(pose, False, True)
    new.tags = tags
    new.set_folder_path(folder)
    new.set_editor_property('is_editor_only_actor', True)
    target = new.static_mesh_component
    target.set_static_mesh(mesh)
    for slot, material in enumerate(materials):
        target.set_material(slot, material)
    target.set_forced_lod_model(lod)
    target.set_evaluate_world_position_offset(False)
    target.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    target.set_editor_property('can_ever_affect_navigation', False)
    assert actors.destroy_actor(old), label
    new.set_actor_label(label)
    report.append({'label': label, 'mesh': mesh.get_path_name(), 'forced_lod': lod, 'class': 'StaticMeshActor', 'editor_only': True})
assert levels.save_current_level()
assert unreal.EditorLoadingAndSavingUtils.load_map('/Game/Maps/LVL_CommanderMassPrototype')
assert not any(isinstance(a, unreal.GuLiCommanderPresentationActor) for a in actors.get_all_level_actors())
result = {'success': True, 'converted': report, 'saved_and_reloaded': True, 'mesh_resources_unchanged': True}
Path('D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008/review-presenter-repair.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'converted': len(report), 'saved_and_reloaded': True}))
