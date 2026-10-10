"""Save opt-in review entities for the formally applied version in the existing map."""
import json
from pathlib import Path
import unreal

assert not unreal.EditorLevelLibrary.get_pie_worlds(True), 'Finish PIE before preparing the review area'
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_name()=='LVL_CommanderMassPrototype'
api=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
existing={a.get_actor_label():a for a in api.get_all_level_actors()}
dest='/Game/GuLiStrike/FX/CommanderWeapons'
rows=[]

def entity(label,cls,position):
    a=existing.get(label)
    if a is None:
        a=api.spawn_actor_from_class(cls,unreal.Vector(*position))
        assert a
        a.set_actor_label(label)
        existing[label]=a
    assert isinstance(a,cls),label
    a.tags=[unreal.Name('GuLiClientThreeReview')]
    a.set_actor_location(unreal.Vector(*position),False,True)
    return a

for effect,name,path,pos in [
    (36,'Mining','/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Optimized_GPU_All_ThreeTier',(-17000,-20500,1500)),
    (45,'Construction','/Game/GuLiStrike/Buildings/Construction/NS_ConstructionLaser_Optimized_GPU_All_ThreeTier',(-17000,-18300,1500))]:
    a=entity('PerfReview_Client3_'+name,unreal.GuLiPerformanceReviewActor,pos)
    asset=unreal.load_asset(path);assert asset,path
    a.set_editor_property('System',asset)
    a.set_editor_property('BaseScale',unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(effect).scale)
    a.set_editor_property('bLaser',True);a.set_editor_property('bCatalogImpacts',False)
    a.set_editor_property('PolicyEffectId',effect);a.set_editor_property('EffectCount',1)
    a.set_editor_property('ActiveSeconds',2.4);a.set_editor_property('CycleSeconds',4)
    a.stop_comparison()
for role,effect,stem,x in [('Muzzle',52,'NS_MachineGunMuzzle_AllOptimizations',-14500),('Impact',5,'NS_MachineGunImpact_AllOptimizations',-12000)]:
    for tier,suffix,y in [('Full','',-20500),('Reduced','_Reduced',-19400),('Minimal','_Minimal',-18300)]:
        a=entity('PerfReview_Client3_'+role+'_'+tier,unreal.GuLiPerformanceReviewActor,(x,y,1500))
        asset=unreal.load_asset(dest+'/'+stem+suffix);assert asset
        a.set_editor_property('System',asset)
        a.set_editor_property('BaseScale',unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(effect).scale)
        a.set_editor_property('bLaser',False);a.set_editor_property('bCatalogImpacts',False)
        # The three explicit comparison columns stay at their authored tiers.
        a.set_editor_property('PolicyEffectId',0);a.set_editor_property('EffectCount',1)
        a.set_editor_property('ActiveSeconds',1);a.set_editor_property('CycleSeconds',2)
        a.stop_comparison()
a=entity('PerfReview_Client3_CatalogImpacts',unreal.GuLiPerformanceReviewActor,(-12000,-16800,1500))
a.set_editor_property('System',unreal.load_asset(dest+'/NS_MachineGunImpact_Batch'))
a.set_editor_property('BaseScale',unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(5).scale)
a.set_editor_property('bCatalogImpacts',True);a.set_editor_property('bLaser',False)
a.set_editor_property('PolicyEffectId',0);a.set_editor_property('EffectCount',1)
a.set_editor_property('ImpactEventsPerSecond',12);a.stop_comparison()

for label,loc in [('Near',(-14800,-25400,5600)),('Middle',(-14800,-29000,7600)),('Far',(-14800,-33800,7600))]:
    cam=entity('ClientThreeReview_Camera_'+label,unreal.CameraActor,loc)
    cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(unreal.Vector(*loc),unreal.Vector(-14800,-19200,1500)),True)
    cam.get_component_by_class(unreal.CameraComponent).set_editor_property('field_of_view',90)
marker=next(a for a in api.get_all_level_actors() if unreal.Name('FlightEventsQAOrigin') in a.tags)
o=marker.get_actor_location();loc=unreal.Vector(o.x-6000,o.y-12000,o.z+13000)
cam=entity('ClientThreeReview_Camera_Wingman',unreal.CameraActor,loc.to_tuple())
cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(loc,unreal.Vector(o.x,o.y+1200,o.z+4500)),True)

guide=entity('ClientThreeReview_Guide',unreal.TextRenderActor,(-17500,-22200,2100))
guide.get_component_by_class(unreal.TextRenderComponent).set_text(
    'CLIENT THREE REVIEW: new production effects and logic applied.\n'
    'py guli_client_three_review_view("fx")\n'
    'py guli_client_three_review_view("middle") / ("far") / ("offscreen")\n'
    'py guli_client_three_review_view("game")\n'
    'py guli_client_three_review_ground(125)\n'
    'py guli_client_three_review_stop_fx()\n'
    'Normal gameplay uses saved references. FPS comparisons deferred by user.')
guide.get_component_by_class(unreal.TextRenderComponent).set_world_size(60)
assert unreal.EditorLoadingAndSavingUtils.save_map(world,'/Game/Maps/LVL_CommanderMassPrototype')
for a in api.get_all_level_actors():
    if unreal.Name('GuLiClientThreeReview') not in a.tags:continue
    row={'name':a.get_name(),'label':a.get_actor_label(),'class':a.get_class().get_name(),
         'map':a.get_world().get_path_name(),'location':list(a.get_actor_location().to_tuple())}
    if isinstance(a,unreal.GuLiPerformanceReviewActor):
        asset=a.get_editor_property('System');assert asset
        row.update(system=asset.get_path_name(),base_scale=list(a.get_editor_property('BaseScale').to_tuple()),
                   policy_id=a.get_editor_property('PolicyEffectId'),catalog=a.get_editor_property('bCatalogImpacts'),
                   tick_enabled=a.is_actor_tick_enabled(),count=a.get_editor_property('EffectCount'))
        assert not row['tick_enabled'] and row['count']==1
    rows.append(row)
assert len(rows)==14,rows
out=Path(unreal.Paths.project_dir())/'outputs/performance/20261009-client-three-optimizations'
out.mkdir(parents=True,exist_ok=True)
(out/'manual-review-scene-readback.json').write_text(json.dumps({'success':True,'saved':True,
    'map':'/Game/Maps/LVL_CommanderMassPrototype','entities':rows,'runtime_and_visual_acceptance':False},indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'saved':True,'entities':len(rows),'formal_references_changed':False}))
