"""Save final delivery in the existing map and read every related entity/reference."""
import json
from pathlib import Path
import unreal

assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
out = Path(unreal.Paths.project_dir())/'outputs/performance/20261009-all-optimizations'
checks = json.loads((out/'static-source-build-check.json').read_text())
assert checks['success']
proposal = json.loads((out/'formal-reference-proposal.json').read_text())
assert proposal['applied'] and proposal['authorization'] == '直接应用所有优化，并补齐整版对照'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_name() == 'LVL_CommanderMassPrototype'
api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = api.get_all_level_actors()
reviews = [a for a in actors if isinstance(a,unreal.GuLiPerformanceReviewActor)]
assert len(reviews) == 24
camera = next(a for a in actors if a.get_actor_label() == 'PerfReview_Camera')
camera.tags = [unreal.Name('GuLiPerformanceReview')]
reference_rows = []
for row in proposal['rows']:
    definition = unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(row['id'])
    assert definition.resource_path.get_path_name() == row['new_resource']
    assert list(definition.scale.to_tuple()) == row['base_scale']
    reference_rows.append({'id':row['id'],'path':definition.resource_path.get_path_name(),'base_scale':row['base_scale']})
rows = []
for actor in reviews:
    label = actor.get_actor_label()
    name = 'PR_' + label.removeprefix('PerfReview_') + '__Actor'
    if actor.get_name() != name:
        assert actor.rename(name)
    actor.tags = [unreal.Name('GuLiPerformanceReview')]
    actor.stop_comparison()
    actor.set_editor_property('EffectCount',1)
    if label.endswith('Impact'):
        actor.set_editor_property('BaseScale',unreal.Vector(2,2,2))
    system = actor.get_editor_property('System')
    assert system
    rows.append({'label':label,'name':name,'system':str(system),
                 'location':list(actor.get_actor_location().to_tuple()),
                 'base_scale':list(actor.get_editor_property('BaseScale').to_tuple()),
                 'tick_enabled':actor.is_actor_tick_enabled(),'effect_count':actor.get_editor_property('EffectCount'),
                 'start':'gs.Perf.Review start '+name,'stop':'gs.Perf.Review stop '+name})
guides = [
    ('PerfReview_VisualGuide',(-16000,-21500,2000),
     'ALL OPTIMIZATIONS: production ID36/45/5/52 and pool ID4/38 applied.\n'
     'Mining 7.5cm / construction 12cm; Opaque+Unlit, actual 8 nodes, Beam GPU.\n'
     'Spark/debris presentation collision disabled; server hit rules retained.\n'
     'gs.Perf.Review start PR_Mining_All__Actor\n'
     'gs.Perf.Review start PR_Construction_All__Actor\n'
     'gs.Perf.Review start PR_Flash_AllMuzzle__Actor (or AllImpact)\n'
     'gs.Perf.Review stop'),
    ('PerfReview_LifecycleGuide',(15000,72000,1800),
     'LIFECYCLE REGION: use existing construction menu, factory type6 and red builder.\n'
     'Look away >0.15s / return: latest beam recovers; scan phase retained.\n'
     'Commander selection: move across screen edge / near plane, inspect rings/routes.\n'
     'Ship: unpossess/repossess/respawn, inspect current panels and late identity.'),
    ('PerfReview_FlightGuide',(0,65000,1600),
     'FOUR-SOURCE SERVER LOAD: gs.Flights.Load 500 45\n'
     'Commander/Ground/Ship/deployed Wingman, 125 per source.\n'
     'gs.Flights.Stop stops replenishment; accepted recipes finish normally.\n'
     'Curve cache, visit stamps, 256 laser / 32 missile batches and Ship domain snapshots active.\n'
     'Scripts/Performance/run_whole_optimization_review.py captures whole builds.')]
for label,position,text in guides:
    actor = next((a for a in actors if a.get_actor_label() == label),None)
    if actor is None:
        actor = api.spawn_actor_from_class(unreal.TextRenderActor,unreal.Vector(*position))
    assert actor
    actor.set_actor_label(label)
    actor.tags = [unreal.Name('GuLiPerformanceReview')]
    actor.set_actor_location(unreal.Vector(*position),False,True)
    component = actor.get_component_by_class(unreal.TextRenderComponent)
    component.set_text(text)
    component.set_world_size(70)
assert unreal.EditorLoadingAndSavingUtils.save_map(world,'/Game/Maps/LVL_CommanderMassPrototype')
entities = []
for actor in api.get_all_level_actors():
    if unreal.Name('GuLiPerformanceReview') not in actor.tags:
        continue
    row = {'name':actor.get_name(),'label':actor.get_actor_label(),'class':actor.get_class().get_name(),
           'map':actor.get_world().get_path_name(),'location':list(actor.get_actor_location().to_tuple())}
    if isinstance(actor,unreal.GuLiPerformanceReviewActor):
        row.update({'system':str(actor.get_editor_property('System')),'tick_enabled':actor.is_actor_tick_enabled(),
                    'base_scale':list(actor.get_editor_property('BaseScale').to_tuple()),
                    'b_laser':actor.get_editor_property('bLaser'),'effect_count':actor.get_editor_property('EffectCount')})
        assert not row['tick_enabled'] and row['effect_count'] == 1
    if isinstance(actor,unreal.TextRenderActor):
        row['text'] = str(actor.get_component_by_class(unreal.TextRenderComponent).get_editor_property('text'))
    if isinstance(actor,unreal.CameraActor):
        row['rotation'] = list(actor.get_actor_rotation().to_tuple())
        row['fov'] = actor.get_component_by_class(unreal.CameraComponent).get_editor_property('field_of_view')
    entities.append(row)
assert len(entities) == 28
marker = next(a for a in api.get_all_level_actors() if unreal.Name('FlightEventsQAOrigin') in a.tags)
report = {'saved':True,'map':'/Game/Maps/LVL_CommanderMassPrototype','reviews':rows,'entities':entities,
          'production_references':reference_rows,'authorization':proposal['authorization'],
          'flight_marker':{'name':marker.get_name(),'map':marker.get_world().get_path_name(),
                           'location':list(marker.get_actor_location().to_tuple()),'tags':[str(t) for t in marker.tags]},
          'visual_acceptance':'Previous Opaque/independent copies approved. New combined visual feedback has not been recorded; directly applied under latest authorization.',
          'scope':'Saved fixture/entity/reference confirmation follows completed source checks, source build and authorized runtime comparisons.'}
(out/'final-saved-scene-readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'saved':report['map'],'reviews':len(rows),'entities':len(entities),'formal_refs':len(reference_rows)}))
