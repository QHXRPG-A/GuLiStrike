"""Editor-only final saved delivery and entity readback in the requested map."""
import json
from pathlib import Path
import unreal

assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
out=Path(unreal.Paths.project_dir())/'outputs/performance/20261009-implementation'
publication=json.loads((out/'formal-reference-proposal.json').read_text(encoding='utf-8'))
assert publication['approval']=='approved_by_user' and publication['applied'] is True
production_rows=[]
for item in publication['rows']:
    definition=unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(item['id'])
    assert definition and definition.resource_path.get_path_name()==item['new_resource']
    production_rows.append({'id':item['id'],'path':definition.resource_path.get_path_name(),
        'base_scale':list(definition.scale.to_tuple())})
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_name()=='LVL_CommanderMassPrototype'
api=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors=api.get_all_level_actors()
reviews=[a for a in actors if isinstance(a,unreal.GuLiPerformanceReviewActor)]
assert len(reviews)==20
markers=[a for a in actors if unreal.Name('FlightEventsQAOrigin') in a.tags]
assert len(markers)==1
camera=next(a for a in actors if a.get_actor_label()=='PerfReview_Camera')
camera.tags=[unreal.Name('GuLiPerformanceReview')]
rows=[]
for actor in reviews:
    label=actor.get_actor_label();assert label.startswith('PerfReview_')
    old_name=actor.get_name()
    # The console accepts a prefix. A terminating delimiter prevents numeric
    # names such as _1 from also matching _10.._19, and Muzzle matching its variant.
    name='PR_'+label.removeprefix('PerfReview_')+'__Actor'
    if actor.get_name()!=name:assert actor.rename(name)
    if label=='PerfReview_Flash_Impact':actor.set_editor_property('BaseScale',unreal.Vector(2,2,2))
    actor.stop_comparison();actor.set_editor_property('EffectCount',1)
    system=actor.get_editor_property('System');assert system
    rows.append({'label':label,'old_name':old_name,'name':name,'system':str(system),
        'location':list(actor.get_actor_location().to_tuple()),'base_scale':list(actor.get_editor_property('BaseScale').to_tuple()),
        'tick_enabled':actor.is_actor_tick_enabled(),'effect_count':actor.get_editor_property('EffectCount'),
        'start':'gs.Perf.Review start '+name,'stop':'gs.Perf.Review stop '+name})
guides=[
    ('PerfReview_VisualGuide',unreal.Vector(-16000,-21500,2000),
     'PIE VFX REVIEW: user-approved ID36/45/5/52 production resources installed.\n'
     'Mining ID36: 5 -> 7.5 cm. Construction ID45: 8 -> 12 cm.\n'
     'gs.Perf.Review start PR_Mining_Opaque__Actor\n'
     'gs.Perf.Review start PR_Construction_Opaque__Actor\n'
     'gs.Perf.Review stop  (stop all review actors in this client)'),
    ('PerfReview_LifecycleGuide',unreal.Vector(15000,72000,1800),
     'PERFORMANCE LIFECYCLE REGION: current valid construction/navigation ground.\n'
     'Place factory type6 near (15000,72000,902); order a red builder nearby.\n'
     'Look away >0.15s / look back: latest beams recover, scan phase retained.\n'
     'Use existing build menu; dedicated server + two clients. Fund if needed.'),
    ('PerfReview_FlightGuide',unreal.Vector(0,65000,1600),
     'THREE-SOURCE FLIGHT LOAD (server): gs.Flights.Load 500 45\n'
     'Requires Commander, Ground and deployed Wingman; 167/167/166 flights.\n'
     'gs.Flights.Stop stops replenishment; existing recipes finish normally.\n'
     'Use Scripts/Performance/run_four_stage_review.py for fixed paired captures.'),
]
for label,location,text in guides:
    actor=next((a for a in actors if a.get_actor_label()==label),None)
    if actor is None:actor=api.spawn_actor_from_class(unreal.TextRenderActor,location)
    assert actor
    actor.set_actor_label(label);actor.tags=[unreal.Name('GuLiPerformanceReview')]
    actor.set_actor_location(location,False,True)
    component=actor.get_component_by_class(unreal.TextRenderComponent)
    component.set_text(text);component.set_world_size(70)
assert unreal.EditorLoadingAndSavingUtils.save_map(world,'/Game/Maps/LVL_CommanderMassPrototype')
readback=[]
for actor in api.get_all_level_actors():
    if unreal.Name('GuLiPerformanceReview') not in actor.tags:continue
    row={'name':actor.get_name(),'label':actor.get_actor_label(),'class':actor.get_class().get_name(),
        'map':actor.get_world().get_path_name(),'location':list(actor.get_actor_location().to_tuple())}
    if isinstance(actor,unreal.GuLiPerformanceReviewActor):
        row.update({'system':str(actor.get_editor_property('System')),'tick_enabled':actor.is_actor_tick_enabled(),
            'base_scale':list(actor.get_editor_property('BaseScale').to_tuple())})
    if isinstance(actor,unreal.TextRenderActor):
        row['text']=str(actor.get_component_by_class(unreal.TextRenderComponent).get_editor_property('text'))
    if isinstance(actor,unreal.CameraActor):
        row['rotation']=list(actor.get_actor_rotation().to_tuple())
        row['fov']=actor.get_component_by_class(unreal.CameraComponent).get_editor_property('field_of_view')
    readback.append(row)
report={'map':'/Game/Maps/LVL_CommanderMassPrototype','saved':True,'reviews':rows,'entities':readback,
    'flight_marker':{'tag':'FlightEventsQAOrigin','name':markers[0].get_name(),'label':markers[0].get_actor_label(),
        'class':markers[0].get_class().get_name(),'map':markers[0].get_world().get_path_name(),
        'location':list(markers[0].get_actor_location().to_tuple()),'tags':[str(t) for t in markers[0].tags]},
    'production_registry_changed':True,'production_references':production_rows,
    'visual_approval':publication['approval_message'],
    'scope':'Saved fixture and actual entity/reference readback after approved publication. Other player functional acceptance remains separate.'}
(out/'final-saved-scene-readback.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'saved':report['map'],'reviews':len(rows),'entities':len(readback)}))
