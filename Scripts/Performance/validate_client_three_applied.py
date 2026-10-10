"""Functional checks of saved defaults; no FPS capture and no candidate installation.

Uses the explicitly requested client Worlds, catalog entry and real ground
Definition. Metadata/resource fallback cases are transient and restore their
original values. This is technical evidence, separate from player acceptance.
"""
import argparse,json,math,sys,time,uuid,traceback
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Scripts'))
from Performance import run_four_stage_review as review
from Performance.client_three_review_support import OUT,capture_controls,restore_controls
from Performance.validate_wingman_ground_runtime import SNAPSHOT as WINGMAN_SNAPSHOT

CONTEXT=r'''
_applied_ws=unreal.EditorLevelLibrary.get_pie_worlds(True)
_applied_clients=[]
for _applied_w in _applied_ws:
 _applied_pc=unreal.GameplayStatics.get_player_controller(_applied_w,0)
 if _applied_pc and _applied_pc.is_local_controller():_applied_clients.append((_applied_w,_applied_pc))
_applied_clients.sort(key=lambda v:v[0].get_path_name())
assert len(_applied_clients)==2
_applied_fx=[next(s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if s.get_outer()==w) for w,pc in _applied_clients]
_applied_cam=[next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.CameraActor) if a.get_actor_label()=='ClientThreeReview_Camera_Near') for w,pc in _applied_clients]
for (_applied_w,_applied_pc),_applied_c in zip(_applied_clients,_applied_cam):
 _applied_pc.set_editor_property('bAutoManageActiveCameraTarget',False);_applied_pc.set_view_target_with_blend(_applied_c,0)
def _applied_snapshot(owner):
 c=owner.get_counters();w=owner.get_outer()
 components=[]
 for comp in unreal.ObjectIterator(unreal.NiagaraComponent):
  if comp.get_world()!=w or not comp.is_active() or not comp.get_asset():continue
  name=comp.get_asset().get_name()
  if not name.startswith('NS_MachineGunImpact'):continue
  row={'asset':comp.get_asset().get_path_name()}
  if name.startswith('NS_MachineGunImpact_Batch'):row['data']=json.loads(unreal.GuLiCombatEffectAuthoringLibrary.inspect_impact_batch_component(comp))
  components.append(row)
 return {'world':w.get_path_name(),'active':c.impact_active,'components':c.impact_components,
  'published':c.impact_batch_published,'fallbacks':c.impact_batch_fallbacks,'systems':components}
'''

SCHEDULE=r'''
_applied_frames=[]
def _applied_frame_tick(dt):
 global _applied_emission
 try:
  index=len(_applied_frames)
  row={'tick':index,'worlds':[_applied_snapshot(fx) for fx in _applied_fx]}
  if index==0:
   _applied_emission={}
   exec(_applied_emit_code,globals())
   row['emission']=_applied_emission
  _applied_frames.append(row)
  if len(_applied_frames)>=_applied_frame_count:
   unreal.unregister_slate_post_tick_callback(_applied_frame_handle)
   _applied_frame_path.write_text(json.dumps({'success':True,'run_id':_applied_run_id,'frames':_applied_frames},indent=2),encoding='utf-8')
 except Exception:
  import traceback
  unreal.unregister_slate_post_tick_callback(_applied_frame_handle)
  _applied_frame_path.write_text(json.dumps({'success':False,'run_id':_applied_run_id,'error':traceback.format_exc()}),encoding='utf-8')
_applied_frame_handle=unreal.register_slate_post_tick_callback(_applied_frame_tick)
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'scheduled':_applied_frame_count}))
'''

def control(**values):
 code="_control_world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
 for key,value in values.items():code+=f'unreal.SystemLibrary.execute_console_command(_control_world,{f"{key} {value}"!r})\n'
 review.result(code)

def reset():
 review.result('for fx in _applied_fx:assert fx.reset_review_impacts()')
 time.sleep(.15)

def snapshot():
 return review.run("unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'worlds':[_applied_snapshot(fx) for fx in _applied_fx]}))")

def frames(label,code,n=12):
 run_id=uuid.uuid4().hex;path=OUT/f'applied-{label}-frames.json'
 review.run(f"from pathlib import Path\n_applied_frame_path=Path({str(path)!r})\n_applied_run_id={run_id!r}\n_applied_frame_count={n}\n_applied_emit_code={code!r}\n"+SCHEDULE)
 deadline=time.monotonic()+20
 while time.monotonic()<deadline:
  time.sleep(.1)
  try:result=json.loads(path.read_text(encoding='utf-8'))
  except (FileNotFoundError,json.JSONDecodeError):continue
  if result.get('run_id')==run_id:
   assert result['success'],result
   return result['frames']
 raise AssertionError('Frame callback did not complete: '+label)

def particle_rows(world,emitter=None):
 return [row for comp in world['systems'] for entry in comp.get('data',{}).get('emitters',[])
         if emitter is None or entry['emitter']==emitter for row in entry['rows']]

def particles(world,emitter='Glow'):
 return sum(entry['particles'] for comp in world['systems'] for entry in comp.get('data',{}).get('emitters',[]) if entry['emitter']==emitter)

def wait_clients():
 deadline=time.monotonic()+40
 while time.monotonic()<deadline:
  ready=review.run("ws=unreal.EditorLevelLibrary.get_pie_worlds(True)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'clients':sum(bool((pc:=unreal.GameplayStatics.get_player_controller(w,0)) and pc.is_local_controller()) for w in ws)}))")
  if ready['clients']==2:return
  time.sleep(.4)
 raise AssertionError('Two local clients not ready')

def metadata_only():
 """Resume the remaining case without repeating seven already-passed checks."""
 receipt=json.loads((OUT/'applied-functional-receipt.json').read_text(encoding='utf-8'))
 assert len(receipt['checks'])==7 and all(v=='passed' for v in receipt['checks'].values()),receipt['checks']
 controls=capture_controls(review);started=False
 try:
  review.result("_metadata_original={}\nfor suffix in ('','_Reduced','_Minimal'):\n p='/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunImpact_Batch'+suffix\n _metadata_original[p]=str(unreal.NiagaraService.get_parameter(p,'User.GuLiImpactInputVersion').current_value)\n assert unreal.NiagaraService.set_parameter(p,'User.GuLiImpactInputVersion','0')")
  review.run((ROOT/'Scripts/Performance/client_three_player_review.py').read_text(encoding='utf-8'))
  review.result('guli_client_three_review_start()');started=True
  wait_clients();review.result(CONTEXT)
  control(**{'gs.Effects.ThreeTierLOD':0,'gs.Effects.BoundsCull':0,'gs.Effects.OffscreenLifecycle':0,'gs.Impacts.BatchMode':2})
  reset()
  fallback=frames('metadata-fallback',"for fx in _applied_fx:_applied_emission[fx.get_outer().get_path_name()]=fx.emit_review_impacts(unreal.Vector(-14900,-19500,1500),2,5001)")
  for world in fallback[2]['worlds']:
   assert world['active']==2 and world['components']==2 and not any('Batch' in s['asset'] for s in world['systems']),world
  assert all(w['components']==0 for w in fallback[1]['worlds']),'Fallback lost F+1 delay'
  receipt['checks']['uncertified_metadata_same_tier_single_f_plus_one']='passed'
  receipt['success']=True;receipt.pop('error',None)
 finally:
  if started:review.stop()
  review.result("for p,value in _metadata_original.items():assert unreal.NiagaraService.set_parameter(p,'User.GuLiImpactInputVersion',value)")
  restore_controls(review,controls)
  (OUT/'applied-functional-receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf-8')
 print(json.dumps({'success':receipt['success'],'checks':receipt['checks'],'performance':'deferred_by_user'}))

def main():
 receipt={'success':False,'performance':'deferred_by_user','resource_overrides_for_normal_path':False,'checks':{}}
 controls=capture_controls(review);started=False
 try:
  receipt['saved_defaults']=review.run("definition=unreal.load_asset('/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundMissile')\ncatalog=unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_CommanderCombatEffects')\nassert definition.get_editor_property('FlightPresentationProfile') and definition.get_editor_property('GroundWarningStyle') and catalog.get_editor_property('ImpactChannel')\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'profile':str(definition.get_editor_property('FlightPresentationProfile')),'channel':str(catalog.get_editor_property('ImpactChannel'))}))")
  review.run((ROOT/'Scripts/Performance/client_three_player_review.py').read_text(encoding='utf-8'))
  review.result('guli_client_three_review_start()');started=True
  deadline=time.monotonic()+40
  while time.monotonic()<deadline:
   ready=review.run("ws=unreal.EditorLevelLibrary.get_pie_worlds(True)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'clients':sum(bool((pc:=unreal.GameplayStatics.get_player_controller(w,0)) and pc.is_local_controller()) for w in ws)}))")
   if ready['clients']==2:break
   time.sleep(.4)
  else:raise AssertionError('Two local clients not ready')
  review.result(CONTEXT)
  control(**{'gs.Effects.ThreeTierLOD':0,'gs.Effects.BoundsCull':0,'gs.Effects.OffscreenLifecycle':0,'gs.Impacts.BatchMode':2})
  time.sleep(.25);reset()
  input_code='''
for fx in _applied_fx:
 values=[]
 for i in range(3):
  values.append(fx.emit_review_impact_input(101+i,unreal.Vector(-14900+i*300,-19500,1500),unreal.Rotator(15*i,37*i,5*i),unreal.Vector(1+i*.4,.8+i*.2,1.3),unreal.LinearColor(.9,.2+i*.1,.5,1),731+i,3,11,1))
 duplicate=fx.emit_review_impact_input(101,unreal.Vector(-14900,-19500,1500),unreal.Rotator(),unreal.Vector(1,.8,1.3),unreal.LinearColor(.9,.2,.5,1),731,3,11,1)
 _applied_emission[fx.get_outer().get_path_name()]={'accepted':values,'duplicate':duplicate}
'''
  capture=frames('input-contract',input_code)
  assert all(all(v['accepted']) and not v['duplicate'] for v in capture[0]['emission'].values())
  assert all(particles(w)==0 for w in capture[1]['worlds']),'Consumed current-frame rows'
  for world in capture[2]['worlds']:
   assert particles(world)==3 and world['components']==1,world
   # Glow consumes its input fields at Spawn and the compiler removes them.
   # RibbonCore retains all fields for its update/event payload contract.
   rows=particle_rows(world,'RibbonCore');assert len(rows)==3
   for row in rows:
    i=row['slot'];assert i in (0,1,2)
    expected={'input_position':[-14900+i*300,-19500,1500],'input_scale':[1+i*.4,.8+i*.2,1.3],
              'input_tint':[.9,.2+i*.1,.5,1]}
    for key,value in expected.items():
     assert key in row and max(abs(a-b) for a,b in zip(row[key],value))<.02,(key,row)
    assert row['input_seed']==731+i and math.isclose(row['input_lifetime'],1.25,abs_tol=.01),row
    assert 'input_rotation' in row and math.isclose(sum(v*v for v in row['input_rotation']),1,abs_tol=.01),row
    if i:assert max(abs(v) for v in row['input_rotation'][:3])>.1,row
   for row in particle_rows(world,'RIbbonTrailFollower'):
    assert (row['ribbon_slot'],row['ribbon_generation'])==(row['slot'],row['generation'])
  receipt['checks']['nondefault_inputs_f_plus_one_dedup_world_isolation']='passed'
  # Retire hidden slots, return with no replay, then reuse a freed slot with a new generation.
  old_generations={r['slot']:r['generation'] for r in particle_rows(capture[2]['worlds'][0],'Glow')}
  control(**{'gs.Effects.BoundsCull':1,'gs.Effects.OffscreenLifecycle':1})
  review.result("for cam in _applied_cam:\n cam.set_actor_location(unreal.Vector(1000000,1000000,1000000),False,True)\n cam.set_actor_rotation(unreal.Rotator(0,45,0),True)")
  time.sleep(.5);receipt['hidden']=snapshot()
  assert all(w['active']==0 and w['components']==0 for w in receipt['hidden']['worlds'])
  review.result('guli_client_three_review_view("near")\nfor cam in _applied_cam:\n loc=unreal.Vector(-14800,-25400,5600);cam.set_actor_location(loc,False,True)\n cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(loc,unreal.Vector(-14800,-19200,1500)),True)')
  time.sleep(.2);assert all(w['active']==0 and w['components']==0 for w in snapshot()['worlds'])
  reuse=frames('reuse',"for fx in _applied_fx:_applied_emission[fx.get_outer().get_path_name()]=fx.emit_review_impacts(unreal.Vector(-14900,-19500,1500),1,881)")
  for world in reuse[2]['worlds']:
   assert particles(world)==1
   row=particle_rows(world,'Glow')[0]
   assert row['slot'] in old_generations and row['generation']>old_generations[row['slot']],row
  receipt['checks']['offscreen_retire_no_replay_slot_reuse']='passed'
  time.sleep(3.2);receipt['idle']=snapshot()
  assert all(w['active']==0 and w['components']==0 for w in receipt['idle']['worlds'])
  wake=frames('idle-wake',"for fx in _applied_fx:_applied_emission[fx.get_outer().get_path_name()]=fx.emit_review_impacts(unreal.Vector(-14900,-19500,1500),1,882)")
  assert all(particles(w)==1 for w in wake[2]['worlds'])
  receipt['checks']['empty_frames_idle_wake_no_stale_rows']='passed'
  reset()
  epoch=frames('epoch-reset',"for fx in _applied_fx:\n fx.emit_review_impacts(unreal.Vector(-14900,-19500,1500),8,999)\n assert fx.reset_review_impacts()\n _applied_emission[fx.get_outer().get_path_name()]=fx.emit_review_impacts(unreal.Vector(-14900,-19500,1500),1,1001)")
  assert all(particles(w)==1 for w in epoch[2]['worlds'])
  receipt['checks']['reset_before_publish_rejects_old_epoch']='passed'
  reset();control(**{'gs.Effects.ThreeTierLOD':1})
  review.result("for cam in _applied_cam:\n cam.set_actor_location(unreal.Vector(-14800,-32000,8000),False,True)\n cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(cam.get_actor_location(),unreal.Vector(-14800,-19000,1500)),True)")
  time.sleep(.4)
  # LOD distance is measured to the complete envelope, not its center. The
  # production scale 2 makes the authored 4096 cm half-extent 8192 cm.
  mixed=frames('mixed-lod',"for fx,cam in zip(_applied_fx,_applied_cam):\n accepted=[]\n for d in (4000,18000,26000):\n  loc=cam.get_actor_location()+cam.get_actor_forward_vector()*d\n  accepted.append(fx.emit_review_impacts(loc,1,2100+d))\n _applied_emission[fx.get_outer().get_path_name()]=accepted")
  assert all(v==[1,1,1] for v in mixed[0]['emission'].values()),mixed[0]['emission']
  for world in mixed[3]['worlds']:
   paths={s['asset'].split('.')[0] for s in world['systems']}
   expected={'/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunImpact_Batch'+suffix for suffix in ('','_Reduced','_Minimal')}
   assert paths==expected and world['components']==3 and particles(world)==3,(paths,world)
  receipt['checks']['three_lod_resources_world_once_max_three_components']='passed'
  reset();control(**{'gs.Effects.ThreeTierLOD':0})
  review.result("for cam in _applied_cam:\n loc=unreal.Vector(-14800,-25400,5600);cam.set_actor_location(loc,False,True)\n cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(loc,unreal.Vector(-14800,-19200,1500)),True)")
  time.sleep(.3)
  review.result("_original_rows=[]\nfor w,pc in _applied_clients:\n reg=next(s for s in unreal.ObjectIterator(unreal.GuLiVfxRegistrySubsystem) if s.get_outer()==w)\n row=unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(5)\n _original_rows.append((reg,unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(5)))\n for field in ('BatchResourcePath','ReducedBatchResourcePath','MinimalBatchResourcePath'):row.set_editor_property(field,None)\n assert reg.apply_review_definition(row)")
  try:
   missing=frames('missing-resource-fallback',"for fx in _applied_fx:_applied_emission[fx.get_outer().get_path_name()]=fx.emit_review_impacts(unreal.Vector(-14900,-19500,1500),2,5101)")
   assert all(w['components']==2 and not any('Batch' in s['asset'] for s in w['systems']) for w in missing[2]['worlds'])
   receipt['checks']['missing_batch_resource_single_fallback']='passed'
  finally:review.result('for reg,row in _original_rows:assert reg.apply_review_definition(row)')
  reset();restore_controls(review,controls)
  # Saved ground profile/warning, with real production Wingman and frozen target field.
  review.result('guli_client_three_review_ground(125)')
  deadline=time.monotonic()+25
  while time.monotonic()<deadline:
   time.sleep(.4);wingman=review.run(WINGMAN_SNAPSHOT)
   if all(w['logical']>=125 and w['components']>0 and w['warnings']>=125 for w in wingman['worlds']):break
  else:raise AssertionError('Saved ground profile/warnings did not activate')
  receipt['ground_visible']=wingman
  assert all(w['components']<=8 for w in wingman['worlds'])
  review.result('guli_client_three_review_stop_ground()')
  # StopLoad stops replenishment, preserving the real authority's 8 s flight
  # lifetime. Wait for its reliable endings and the additional 0.55 s tail fade.
  deadline=time.monotonic()+15
  terminal=[]
  receipt['ground_terminal']=terminal
  while time.monotonic()<deadline:
   time.sleep(.05);snap=review.run(WINGMAN_SNAPSHOT);terminal.append(snap)
   if all(w['logical']==0 and w['components']==0 and w['warnings']==0 for w in snap['worlds']):break
  else:raise AssertionError('Ground stop did not clean visuals/warnings')
  receipt['ground_terminal']=terminal
  receipt['checks']['saved_ground_profile_warning_trigger_stop_cleanup']='passed'
  # Niagara's asset authoring service is unavailable during PIE. Prepare a
  # metadata-missing session before Play, then restore version 1 after stopping.
  review.stop();started=False
  review.result("_metadata_original={}\nfor suffix in ('','_Reduced','_Minimal'):\n p='/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunImpact_Batch'+suffix\n _metadata_original[p]=str(unreal.NiagaraService.get_parameter(p,'User.GuLiImpactInputVersion').current_value)\n assert unreal.NiagaraService.set_parameter(p,'User.GuLiImpactInputVersion','0')")
  review.run((ROOT/'Scripts/Performance/client_three_player_review.py').read_text(encoding='utf-8'))
  review.result('guli_client_three_review_start()');started=True
  wait_clients();review.result(CONTEXT)
  control(**{'gs.Effects.ThreeTierLOD':0,'gs.Effects.BoundsCull':0,'gs.Effects.OffscreenLifecycle':0,'gs.Impacts.BatchMode':2})
  reset()
  fallback=frames('metadata-fallback',"for fx in _applied_fx:_applied_emission[fx.get_outer().get_path_name()]=fx.emit_review_impacts(unreal.Vector(-14900,-19500,1500),2,5001)")
  for world in fallback[2]['worlds']:
   assert world['active']==2 and world['components']==2 and not any('Batch' in s['asset'] for s in world['systems']),world
  assert all(w['components']==0 for w in fallback[1]['worlds']),'Fallback lost F+1 delay'
  receipt['checks']['uncertified_metadata_same_tier_single_f_plus_one']='passed'
  receipt['success']=True
 except Exception:
  receipt['error']=traceback.format_exc();raise
 finally:
  if started:
   review.result("if '_applied_frame_handle' in globals():unreal.unregister_slate_post_tick_callback(_applied_frame_handle)")
   review.stop()
  review.result("if '_metadata_original' in globals():\n for p,value in _metadata_original.items():assert unreal.NiagaraService.set_parameter(p,'User.GuLiImpactInputVersion',value)")
  restore_controls(review,controls)
  (OUT/'applied-functional-receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf-8')
 print(json.dumps({'success':True,'checks':receipt['checks'],'performance':'deferred_by_user'},ensure_ascii=False))

if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('--metadata-only',action='store_true');args=parser.parse_args()
 metadata_only() if args.metadata_only else main()
