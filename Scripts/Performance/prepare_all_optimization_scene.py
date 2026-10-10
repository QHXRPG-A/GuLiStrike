"""Prepare four final review actors in the existing map; save only at finalization."""
import json
from pathlib import Path
import unreal

assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_name() == 'LVL_CommanderMassPrototype'
api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = api.get_all_level_actors()
out = Path(unreal.Paths.project_dir()) / 'outputs/performance/20261009-all-optimizations'
proposal = json.loads((out / 'formal-reference-proposal.json').read_text())
assert proposal['applied'] and proposal['authorization'] == '直接应用所有优化，并补齐整版对照'
rows = []
for label, effect_id, location, laser in [
    ('PerfReview_Mining_All',36,(-4800,-20500,1500),True),
    ('PerfReview_Construction_All',45,(-4800,-18500,1500),True),
    ('PerfReview_Flash_AllMuzzle',52,(-11200,-16500,1500),False),
    ('PerfReview_Flash_AllImpact',5,(-9600,-16500,1500),False)]:
    definition = unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(effect_id)
    assert definition.resource_path
    actor = next((a for a in actors if a.get_actor_label() == label), None)
    if actor is None:
        actor = api.spawn_actor_from_class(unreal.GuLiPerformanceReviewActor,unreal.Vector(*location))
    assert actor
    actor.set_actor_label(label)
    name = 'PR_' + label.removeprefix('PerfReview_') + '__Actor'
    if actor.get_name() != name:
        assert actor.rename(name)
    actor.set_actor_location(unreal.Vector(*location),False,True)
    actor.tags = [unreal.Name('GuLiPerformanceReview')]
    actor.set_editor_property('System',definition.resource_path)
    actor.set_editor_property('BaseScale',definition.scale)
    actor.set_editor_property('bLaser',laser)
    actor.set_editor_property('EffectCount',1)
    actor.set_editor_property('ActiveSeconds',2.4)
    actor.set_editor_property('CycleSeconds',4.0)
    actor.set_editor_property('RelativeBeamEnd',unreal.Vector(1600,0,0))
    actor.stop_comparison()
    rows.append({'name':name,'label':label,'id':effect_id,'path':definition.resource_path.get_path_name(),
                 'scale':list(definition.scale.to_tuple()),'location':list(location),'tick_enabled':actor.is_actor_tick_enabled()})
(out / 'prepared-scene-readback.json').write_text(json.dumps({'saved':False,'actors':rows},ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'prepared':len(rows),'reviews':len([a for a in api.get_all_level_actors() if isinstance(a,unreal.GuLiPerformanceReviewActor)])}))
