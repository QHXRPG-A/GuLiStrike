"""Final saved references, material diagnostics and default controls. Read only."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'outputs/performance/20261009-client-three-optimizations'
assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
functional=json.loads((OUT/'applied-functional-receipt.json').read_text(encoding='utf-8'))
formal=json.loads((OUT/'client-three-formal-readback.json').read_text(encoding='utf-8'))
assert functional['success'] and len(functional['checks'])==8 and formal['success'] and formal['rows']==53
assert all(unreal.SystemLibrary.get_console_variable_float_value(k)==v for k,v in formal['controls'].items())
profile=unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_WingmanGroundFlightPresentation')
wingman=unreal.load_asset('/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundMissile')
wm01=unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_Missile')
assert wingman.get_editor_property('FlightPresentationProfile')==profile
assert wingman.get_editor_property('GroundWarningStyle')
assert not wm01.get_editor_property('FlightPresentationProfile')
legacy=unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(2)
assert legacy.resource_path.get_path_name()=='/Game/GuLiStrike/FX/CommanderWeapons/NS_WM01_MissileFlight.NS_WM01_MissileFlight'
assert all(abs(v-.4)<.000001 for v in legacy.scale.to_tuple())
row53=unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(53)
assert row53.resource_path.get_path_name()==profile.get_editor_property('BatchSystem').get_path_name()
assert row53.scale.to_tuple()==(1.,1.,1.)
materials=[]
for name in ('M_WingmanGroundCore_Opaque','M_WingmanGroundTail_Opaque'):
    path='/Game/GuLiStrike/FX/CommanderWeapons/'+name
    mat=unreal.load_asset(path);assert mat
    assert mat.get_editor_property('blend_mode')==unreal.BlendMode.BLEND_OPAQUE
    assert mat.get_editor_property('shading_model')==unreal.MaterialShadingModel.MSM_UNLIT
    diag=unreal.MaterialNodeService.get_material_diagnostics(path);assert diag.is_compiled_ok,str(diag)
    materials.append({'path':path,'blend':'Opaque','shading':'Unlit','compiled':True})
versions={}
for suffix in ('','_Reduced','_Minimal'):
    path='/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunImpact_Batch'+suffix
    info=unreal.NiagaraService.get_parameter(path,'User.GuLiImpactInputVersion')
    lifetime=unreal.NiagaraService.get_parameter(path,'User.GuLiImpactMaximumLifetime')
    assert info and float(info.current_value)==1
    assert lifetime and float(lifetime.current_value)==1.25
    versions[path]={'version':1,'lifetime':1.25}
scene=json.loads((OUT/'manual-review-scene-readback.json').read_text(encoding='utf-8'))
assert scene['success'] and scene['saved'] and len(scene['entities'])==14
api=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
entities=[a for a in api.get_all_level_actors() if unreal.Name('GuLiClientThreeReview') in a.tags]
assert len(entities)==14 and all(not a.is_actor_tick_enabled() for a in entities if isinstance(a,unreal.GuLiPerformanceReviewActor))
guide=next(a for a in entities if a.get_actor_label()=='ClientThreeReview_Guide')
guide_text=str(guide.get_component_by_class(unreal.TextRenderComponent).get_editor_property('text'))
assert 'new production effects and logic applied' in guide_text
receipt={'success':True,'production_references_applied':True,'protocol_checks':functional['checks'],
    'impact_resources':versions,'materials':materials,'wingman_vfx_id':53,'shared_id_2_and_wm01_preserved':True,
    'controls':formal['controls'],'map':'/Game/Maps/LVL_CommanderMassPrototype','saved_entities':len(entities),
    'preview_actors_default_stopped':True,'pie_worlds':0,'visual_and_gameplay_acceptance':'pending_user_pie',
    'performance':'deferred_by_user','build_receipt':'build30-loaded-receipt.json'}
(OUT/'client-three-delivery-readback.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'saved_entities':14,'formal_rows':53,
    'protocol_checks':len(functional['checks']),'pie_worlds':0,'remaining':'User PIE acceptance; FPS deferred.'}))
