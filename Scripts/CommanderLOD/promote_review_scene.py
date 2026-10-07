"""Save the existing review map with approved formal references and the loaded VAT fixture."""
import json
import re
from pathlib import Path
import unreal

ART = Path('D:/UE5.7/test1/ArtSource/CommanderLOD_20261005')
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not editor.get_game_world() and editor.get_editor_world().get_path_name().split('.')[0] == MAP
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
static = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
lib = unreal.EditorAssetLibrary
before = actors.get_all_level_actors()
deployments = {a.get_actor_label():(a.get_path_name(),a.unit_type_id,list(a.get_actor_location().to_tuple()))
    for a in before if isinstance(a,unreal.GuLiCommanderDeploymentPoint)}
assert all(row[1] != 6 for row in deployments.values())
assert editor.get_editor_world().get_world_settings().get_editor_property('default_game_mode').get_path_name() == '/Script/GuLiStrike.GuLiCommanderGameMode'
groups = json.loads((ART / 'Reports/review_scene.json').read_text(encoding='utf8'))['groups']
ready = {g['name']:json.loads((ART / ('Reports/formal_prepared_' + g['name'] + '.json')).read_text(encoding='utf8')) for g in groups}
changed = []
for actor in before:
    if 'CommanderLODReview20261005' not in [str(t) for t in actor.tags]:
        continue
    name = next((n for n in ready if actor.get_actor_label().startswith('CommanderLOD_' + n + '_LOD')),None)
    if not name:
        continue
    group = ready[name]
    mapping = {a['source']:a['formal'] for a in group['assets']}
    mapping.update({a['formal']:a['formal'] for a in group['assets']})
    for component in actor.get_components_by_class(unreal.MeshComponent):
        if isinstance(component,unreal.SkeletalMeshComponent):
            field = 'skeletal_mesh_asset'
        elif isinstance(component,unreal.StaticMeshComponent):
            field = 'static_mesh'
        else:
            continue
        old_mesh = component.get_editor_property(field)
        if not old_mesh:
            continue
        assert old_mesh.get_path_name() in mapping, (actor.get_actor_label(), component.get_name(), old_mesh.get_path_name())
        mesh = lib.load_asset(mapping[old_mesh.get_path_name()])
        old_overrides = list(component.get_editor_property('override_materials'))
        overrides = {}
        if isinstance(component,unreal.StaticMeshComponent):
            for lod in range(3):
                for section in range(old_mesh.get_num_sections(lod)):
                    old_slot = static.get_lod_material_slot(old_mesh,lod,section)
                    new_slot = static.get_lod_material_slot(mesh,lod,section)
                    if old_slot < len(old_overrides) and old_overrides[old_slot]:
                        material = old_overrides[old_slot]
                        overrides[new_slot] = lib.load_asset(mapping.get(material.get_path_name(),material.get_path_name()))
            component.set_static_mesh(mesh)
        else:
            component.set_skeletal_mesh_asset(mesh)
            overrides = {i:lib.load_asset(mapping.get(m.get_path_name(),m.get_path_name())) for i,m in enumerate(old_overrides) if m}
        component.set_editor_property('override_materials',[])
        for index,material in overrides.items():
            component.set_material(index,material)
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        assert not component.get_editor_property('can_ever_affect_navigation')
        lod = int(re.search(r'_LOD([012])(?:_|$)',actor.get_actor_label()).group(1))
        if isinstance(component,unreal.SkeletalMeshComponent):
            component.set_forced_lod(lod+1)
        else:
            component.set_forced_lod_model(lod+1)
        assert component.get_editor_property(field) == mesh
        changed.append(dict(actor=actor.get_actor_label(), component=component.get_path_name(), mesh=mesh.get_path_name(),
            lod=lod, editor_only=actor.get_editor_property('is_editor_only_actor'), materials=[m.get_path_name() if m else None for m in component.get_materials()]))
assert len(changed) == 60 and all(r['editor_only'] for r in changed)

# Complete the already delivered BiZhiMao feature fixture; no gameplay is started.
qa = {a.get_actor_label():a for a in before if a.get_actor_label().startswith('BiZhiMaoQA_')}
assert len(qa) == 13
owner = 'GuLi.BiZhiMao.Review.v1'
assert all(owner in [str(t) for t in a.tags] for a in qa.values())
sample = qa['BiZhiMaoQA_Sample']
position = sample.get_actor_location()
if not isinstance(sample,unreal.GuLiBiZhiMaoReviewActor):
    assert isinstance(sample,unreal.StaticMeshActor)
    assert actors.destroy_actor(sample)
    sample = actors.spawn_actor_from_class(unreal.GuLiBiZhiMaoReviewActor,position)
    sample.set_actor_label('BiZhiMaoQA_Sample')
    sample.tags = [unreal.Name(owner)]
    sample.set_folder_path('BiZhiMaoReview')
    qa['BiZhiMaoQA_Sample'] = sample
base = ready['BiZhiMao']['formal_root']
builders = [qa['BiZhiMaoQA_Builder'+c].get_actor_location()-unreal.Vector(0,0,5) for c in ('A','B')]
for field,value in dict(mesh=lib.load_asset(ready['BiZhiMao']['model_asset']), animation=lib.load_asset(ready['BiZhiMao']['vat_definition']),
    target=qa['BiZhiMaoQA_MoveTarget'], model_scale=2., local_velocity=unreal.Vector(0,0,0),
    prepare_construction_review=True, construction_review_team=unreal.GuLiTeam.RED, builder_ground_locations=builders).items():
    sample.set_editor_property(field,value)
component = sample.get_component_by_class(unreal.InstancedStaticMeshComponent)
component.set_static_mesh(sample.get_editor_property('mesh'))
sample.call_method('ResetPreview')
assert component.get_instance_count() == 1 and component.get_editor_property('static_mesh') == sample.get_editor_property('mesh')
assert not sample.get_components_by_class(unreal.SkeletalMeshComponent)
qa['BiZhiMaoQA_Notes_Status'].get_component_by_class(unreal.TextRenderComponent).set_text('FORMAL THREE-TIER VAT LOADED / MANUAL GAMEPLAY REVIEW PENDING')
assert levels.save_current_level()
actual = actors.get_all_level_actors()
assert deployments == {a.get_actor_label():(a.get_path_name(),a.unit_type_id,list(a.get_actor_location().to_tuple()))
    for a in actual if isinstance(a,unreal.GuLiCommanderDeploymentPoint)}
qa_rows = []
for a in actual:
    if not a.get_actor_label().startswith('BiZhiMaoQA_'):
        continue
    row = dict(label=a.get_actor_label(),class_name=a.get_class().get_name(),position=list(a.get_actor_location().to_tuple()),tags=[str(t) for t in a.tags])
    if a == sample:
        row.update(mesh=a.get_editor_property('mesh').get_path_name(),animation=a.get_editor_property('animation').get_path_name(),
            target=a.get_editor_property('target').get_path_name(),model_scale=a.get_editor_property('model_scale'),
            local_velocity=list(a.get_editor_property('local_velocity').to_tuple()),runtime_skeletal_components=0,
            construction_review=True,builders=[list(p.to_tuple()) for p in a.get_editor_property('builder_ground_locations')])
    qa_rows.append(row)
assert len(qa_rows) == 13
assert not [p for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages() if p.get_name() == MAP]
report = dict(success=True,map=MAP,saved=True,formal_review_groups=18,formal_mesh_components=changed,
    feature_fixture_entities=qa_rows,original_deployments_preserved=len(deployments),play_started=False,
    approval_B='approved',feature_gameplay_review='pending',native_compile='passed',pie='not_run',network='not_run',fps='not_run')
(ART / 'Reports/formal_review_scene.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,map=MAP,formal_groups=18,mesh_components=60,
    fixture_entities=13,zero_skeletal_fixture=True,saved=True,play_started=False)))
