"""PIE-only candidates: preserve approved full resources and all registered base scales."""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'outputs/performance/20261009-client-three-optimizations'
DEST='/Game/GuLiStrike/FX/CommanderWeapons'

VALIDATION_KEYS=['guli.stronghold.TeamUnitCap','guli.stronghold.CaptureSeconds',
 't.IdleWhenNotForeground','Slate.bAllowThrottling','gs.Flights.DataPool',
 'gs.Avoidance.QueryOptimizations','gs.Avoidance.SparseCells','gs.Avoidance.VerifyPrefix',
 'gs.SceneUI.ProjectionCache','gs.Effects.BoundsCull','gs.Effects.OffscreenLifecycle',
 'gs.Effects.ThreeTierLOD','gs.Units.Offscreen5Hz','gs.Flights.OffscreenPresentation',
 'gs.Impacts.BatchMode','gs.WingmanFlight.Presentation','gs.WingmanFlight.Warnings']

def capture_controls(review):
 return review.run("assert not unreal.EditorLevelLibrary.get_pie_worlds(True)\n"+
  f"unreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'values':{{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in {VALIDATION_KEYS!r}}}}}))")['values']

def restore_controls(review,values):
 review.result("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"+
  '\n'.join(f'unreal.SystemLibrary.execute_console_command(w,{f"{k} {v:g}"!r})' for k,v in values.items()))

INSTALL=r'''
ws=unreal.EditorLevelLibrary.get_pie_worlds(True)
assert len(ws)==3,'One dedicated server and two clients required'
dest='/Game/GuLiStrike/FX/CommanderWeapons'
channel=unreal.load_asset(dest+'/NDC_CommanderImpacts')
profile=unreal.load_asset(dest+'/DA_WingmanGroundFlightPresentation')
definition=unreal.load_asset('/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundMissile')
installed=[]
for w in ws[1:]:
 reg=next(s for s in unreal.ObjectIterator(unreal.GuLiVfxRegistrySubsystem) if s.get_outer()==w)
 for effect,stem in [(5,'NS_MachineGunImpact_AllOptimizations'),(52,'NS_MachineGunMuzzle_AllOptimizations')]:
  row=unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(effect)
  row.set_editor_property('ReducedResourcePath',unreal.load_asset(dest+'/'+stem+'_Reduced'))
  row.set_editor_property('MinimalResourcePath',unreal.load_asset(dest+'/'+stem+'_Minimal'))
  if effect==5:
   for field,suffix in [('BatchResourcePath',''),('ReducedBatchResourcePath','_Reduced'),('MinimalBatchResourcePath','_Minimal')]:
    row.set_editor_property(field,unreal.load_asset(dest+'/NS_MachineGunImpact_Batch'+suffix))
  assert reg.apply_review_definition(row)
 for effect,path in [(36,'/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Optimized_GPU_All_ThreeTier'),(45,'/Game/GuLiStrike/Buildings/Construction/NS_ConstructionLaser_Optimized_GPU_All_ThreeTier')]:
  row=unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(effect)
  row.set_editor_property('ResourcePath',unreal.load_asset(path))
  assert reg.apply_review_definition(row)
 fx=next(s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if s.get_outer()==w)
 assert fx.set_review_impact_channel(channel)
 if profile:assert fx.set_review_flight_profile(definition,profile)
 installed.append(w.get_path_name())
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'worlds':installed,'wingman_profile':bool(profile),'formal_references_changed':False}))
'''

COUNTERS=r'''
ws=unreal.EditorLevelLibrary.get_pie_worlds(True)
report={'success':True,'worlds':[]}
for w in ws:
 row={'world':w.get_path_name(),'time':unreal.GameplayStatics.get_time_seconds(w)}
 p=next((s for s in unreal.ObjectIterator(unreal.GuLiPerformanceSubsystem) if s.get_outer()==w),None)
 if p:row['profile']=json.loads(p.get_capture_json())
 fx=next((s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if s.get_outer()==w),None)
 if fx:
  c=fx.get_counters()
  row['effects']={k:c.get_editor_property(k) for k in ['impact_active','impact_components','impact_batch_published','impact_batch_fallbacks','wingman_flight_active','wingman_flight_components','wingman_flight_uploads','niagara_array_uploads','client_flight_data_active','client_flight_actor_active','last_update_milliseconds']}
  states=fx.get_effect_states();row['flight_by_definition']={}
  for state in states:
   key=str(state.projectile_definition)
   row['flight_by_definition'][key]=row['flight_by_definition'].get(key,0)+1
 fixture=next((s for s in unreal.ObjectIterator(unreal.GuLiFlightAcceptanceSubsystem) if s.get_outer()==w),None)
 if fixture:row['load']=json.loads(fixture.get_load_stats_json())
 row['niagara']={}
 for c in unreal.ObjectIterator(unreal.NiagaraComponent):
  if c.get_world()==w and c.is_active() and c.get_asset():
   key=c.get_asset().get_path_name();row['niagara'][key]=row['niagara'].get(key,0)+1
 report['worlds'].append(row)
unreal.MCPythonHelper.submit_result(json.dumps(report))
'''
