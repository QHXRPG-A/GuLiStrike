"""Host acceptance: real two-client Worlds, previous-frame NDC consumption, no profiling window."""
import json,sys,time,uuid,math,argparse
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Scripts'))
from Performance import run_four_stage_review as review
from Performance.client_three_review_support import OUT,INSTALL,COUNTERS,capture_controls,restore_controls

FRAMES=r'''
ws=unreal.EditorLevelLibrary.get_pie_worlds(True)
client=ws[1]
fx=next(s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if s.get_outer()==client)
other_fx=next(s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if s.get_outer()==ws[2])
test_frames=[]
def snapshot_batch(owner=fx):
 c=owner.get_counters();owner_world=owner.get_outer()
 return {'published':c.impact_batch_published,'fallbacks':c.impact_batch_fallbacks,'active':c.impact_active,
  'component_count':c.impact_components,
  'components':[dict(active=x.is_active(),world=x.get_world().get_path_name(),asset=x.get_asset().get_path_name(),data=json.loads(unreal.GuLiCombatEffectAuthoringLibrary.inspect_impact_batch_component(x))) for x in unreal.ObjectIterator(unreal.NiagaraComponent) if x.get_world()==owner_world and x.is_active() and x.get_asset() and x.get_asset().get_name().startswith('NS_MachineGunImpact_Batch')]}
def impact_test_tick(dt):
 i=len(test_frames)
 row=snapshot_batch();row['tick']=i
 row['other_world']=snapshot_batch(other_fx)
 if i==0:
  row['accepted']=fx.emit_review_impacts(unreal.Vector(-14900,-19500,1500),test_count,731)
  row['other_accepted']=other_fx.emit_review_impacts(unreal.Vector(-13900,-19500,1500),3,119)
 test_frames.append(row)
 if len(test_frames)>=30:
  unreal.unregister_slate_post_tick_callback(impact_test_handle)
  from pathlib import Path
  p=Path(unreal.Paths.project_dir())/'outputs/performance/20261009-client-three-optimizations/impact-runtime-frames.json'
  p.write_text(json.dumps({'success':True,'run_id':run_id,'frames':test_frames},indent=2),encoding='utf-8')
impact_test_handle=unreal.register_slate_post_tick_callback(impact_test_tick)
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'scheduled':30}))
'''

def verify_frames(frames,count=8):
 def emitters(frame):
  return {e['emitter']:e for c in frame['components'] for e in c['data']['emitters']}
 assert frames[0]['accepted']==count and frames[0]['other_accepted']==3
 assert all(f['fallbacks']==0 and f['component_count']<=1 for f in frames), 'Batch fell back or allocated per-hit components'
 assert all(e['particles']==0 for e in emitters(frames[1]).values()), 'Consumed current-frame data'
 born=emitters(frames[2])
 for name in ('Glow','Flash','RibbonCore'):
  assert born[name]['particles']==count and born[name]['distinct_identities']==count, (name,'Birth count or identity',born[name]['particles'])
  other=emitters(frames[2]['other_world'])[name]
  assert other['particles']==3 and other['distinct_identities']==3,(name,'Client World isolation')
 for frame in frames[2:]:
  for emitter in emitters(frame).values():
   for row in emitter['rows']:
    assert 0<=row['slot']<count and row['generation']>0,(emitter['emitter'],'Missing hit identity',row)
    origin=(-14900+(row['slot']%8)*120,-19500+(row['slot']//8%8)*120,1500)
    assert math.dist(origin,row['position'])<1400,(emitter['emitter'],'Position detached from hit',row)
 tail=[e for f in frames[3:] for name,e in emitters(f).items() if name=='RIbbonTrailFollower' and e['particles']]
 assert tail,'Location-event follower produced no particles'
 for emitter in tail:
  for row in emitter['rows']:
   assert (row['ribbon_slot'],row['ribbon_generation'])==(row['slot'],row['generation']), ('Cross-hit ribbon identity',row)
 assert any(emitters(f)['RibbonCore']['particles'] for f in frames[4:10]), 'Core died on its first update'
 return {'birth_frame_delay':1,'accepted':count,'components':1,'tail_frames':len(tail),'identity_and_position':True,'world_isolation':True}

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--count',type=int,choices=[8,600],default=8);args=ap.parse_args()
 receipt={'success':False,'requested':args.count}
 initial=capture_controls(review)
 try:
  review.result("assert not unreal.EditorLevelLibrary.get_pie_worlds(True)\nw=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"+
   '\n'.join(f'unreal.SystemLibrary.execute_console_command(w,{cmd!r})' for cmd in ['gs.Impacts.BatchMode 2','gs.Effects.ThreeTierLOD 0','gs.Effects.BoundsCull 0','gs.Effects.OffscreenLifecycle 0']))
  # A provisional in-memory contract enables this acceptance run; no uncertified package is saved.
  review.result("for suffix in ['', '_Reduced', '_Minimal']:\n assert unreal.NiagaraService.set_parameter('/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunImpact_Batch'+suffix,'User.GuLiImpactInputVersion','1')")
  receipt['setup']=review.launch('nodes8','Nodes8')
  review.result("for w in unreal.EditorLevelLibrary.get_pie_worlds(True):\n for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiPerformanceReviewActor):a.stop_comparison()")
  receipt['install']=review.run(INSTALL)
  run_id=uuid.uuid4().hex
  compile(FRAMES,'impact_frame_callback','exec')
  receipt['schedule']=review.run(f'run_id={run_id!r}\ntest_count={args.count}\n'+FRAMES)
  path=OUT/'impact-runtime-frames.json';deadline=time.monotonic()+20
  while time.monotonic()<deadline:
   time.sleep(.1)
   try:receipt['frames']=json.loads(path.read_text())
   except (FileNotFoundError,json.JSONDecodeError):continue
   if receipt['frames'].get('run_id')==run_id:break
  else:raise AssertionError('Callback did not finish this run')
  assert receipt['frames'].get('run_id')==run_id,'Callback did not finish this run'
  receipt['semantic_checks']=verify_frames(receipt['frames']['frames'],args.count)
  receipt['counters']=review.run(COUNTERS)
  receipt['success']=True
  print(json.dumps({'success':True,'semantic_checks':receipt['semantic_checks']},ensure_ascii=False),flush=True)
 finally:
  (OUT/f'impact-runtime-{args.count}-receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf-8')
  review.result("if 'impact_test_handle' in globals():unreal.unregister_slate_post_tick_callback(impact_test_handle)")
  review.stop()
  review.result("for suffix in ['', '_Reduced', '_Minimal']:\n assert unreal.NiagaraService.set_parameter('/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunImpact_Batch'+suffix,'User.GuLiImpactInputVersion','0')")
  restore_controls(review,initial)

if __name__=='__main__':main()
