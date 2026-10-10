"""Actual ground Definition/source/frozen field, inspect live client arrays outside timing windows."""
import json,sys,time,argparse,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Scripts'))
from Performance import run_four_stage_review as review
from Performance.client_three_review_support import OUT,INSTALL,COUNTERS,capture_controls,restore_controls

CAMERA=r'''
ws=unreal.EditorLevelLibrary.get_pie_worlds(True)
marker=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(ws[0],unreal.Actor) if unreal.Name('FlightEventsQAOrigin') in a.tags)
o=marker.get_actor_location()
for w in ws[1:]:
 cam=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.CameraActor) if a.get_actor_label()=='PerfReview_Camera')
 cam.set_actor_location(unreal.Vector(o.x-6000,o.y-12000,o.z+13000),False,True)
 cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(cam.get_actor_location(),unreal.Vector(o.x,o.y+1200,o.z+4500)),True)
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'origin':list(o.to_tuple())}))
'''

SNAPSHOT=r'''
report={'success':True,'worlds':[]}
for w in unreal.EditorLevelLibrary.get_pie_worlds(True)[1:]:
 fx=next(s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if s.get_outer()==w)
 warning=next(s for s in unreal.ObjectIterator(unreal.GuLiGroundWarningSubsystem) if s.get_outer()==w)
 c=fx.get_counters();row={'world':w.get_path_name(),'logical':c.wingman_flight_active,'components':c.wingman_flight_components,'warnings':warning.get_active_warning_count(),'circles':warning.get_active_circle_count(),'batches':[]}
 for comp in unreal.ObjectIterator(unreal.NiagaraComponent):
  if comp.get_world()!=w or not comp.is_active() or not comp.get_asset() or comp.get_asset().get_name()!='NS_WingmanGroundFlight_SphereTrail':continue
  arrays=unreal.NiagaraDataInterfaceArrayFunctionLibrary
  positions=arrays.get_niagara_array_position(comp,'User.WingmanPositions')
  scales=arrays.get_niagara_array_vector(comp,'User.WingmanScales')
  colors=arrays.get_niagara_array_color(comp,'User.WingmanColors')
  head=[i for i in range(0,len(scales),9) if scales[i].x>0]
  tail=[i for i in range(len(scales)) if i%9 and scales[i].x>0 and scales[i].z>0]
  row['batches'].append({'component':comp.get_path_name(),'heads':len(head),'segments':len(tail),
   'diameters_cm':[scales[i].x*100 for i in head],
   'trail_lengths_cm':sum(scales[i].z*100 for i in tail),
   'per_slot_trail_lengths_cm':[sum(scales[j].z*100 for j in range(i+1,i+9) if scales[j].x>0 and scales[j].z>0) for i in range(0,len(scales),9)],
   'per_slot_segments':[sum(1 for j in range(i+1,i+9) if scales[j].x>0 and scales[j].z>0) for i in range(0,len(scales),9)],
   'first_head':list(positions[head[0]].to_tuple()) if head else None,
   'first_scale':list(scales[head[0]].to_tuple()) if head else None,
   'first_color':list(colors[head[0]].to_tuple()) if head else None})
 report['worlds'].append(row)
unreal.MCPythonHelper.submit_result(json.dumps(report))
'''

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--count',type=int,choices=[125,250,500],default=125);args=ap.parse_args()
 receipt={'success':False,'requested':args.count}
 initial=capture_controls(review)
 try:
  review.result("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"+
   '\n'.join(f'unreal.SystemLibrary.execute_console_command(w,{cmd!r})' for cmd in ['gs.WingmanFlight.Presentation 1','gs.WingmanFlight.Warnings 1','gs.Effects.ThreeTierLOD 1','gs.Effects.OffscreenLifecycle 1','gs.Flights.OffscreenPresentation 1','gs.Units.Offscreen5Hz 1']))
  receipt['setup']=review.launch('flight','data_pool',start_flight_load=False)
  receipt['install']=review.run(INSTALL)
  receipt['camera']=review.run(CAMERA)
  receipt['start']=review.run("w=unreal.EditorLevelLibrary.get_pie_worlds(True)[0]\nf=next(s for s in unreal.ObjectIterator(unreal.GuLiFlightAcceptanceSubsystem) if s.get_outer()==w)\nstyle=unreal.load_asset('/Game/GuLiStrike/FX/GroundWarning/DA_GroundWarning_Red')\n"+
    f"unreal.MCPythonHelper.submit_result(json.dumps({{'success':f.start_wingman_ground_load({args.count},45,style)}}))")
  deadline=time.monotonic()+12
  while time.monotonic()<deadline:
   time.sleep(.5);receipt['visible']=review.run(SNAPSHOT)
   if all(w['logical']==args.count and w['warnings']==args.count for w in receipt['visible']['worlds']):break
  else:raise AssertionError('Both client Worlds did not reach requested real ground load')
  review.result("for w in unreal.EditorLevelLibrary.get_pie_worlds(True)[1:]:\n cam=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.CameraActor) if a.get_actor_label()=='PerfReview_Camera')\n cam.set_actor_location(unreal.Vector(1000000,1000000,1000000),False,True)\n cam.set_actor_rotation(unreal.Rotator(0,45,0),True)")
  time.sleep(.5)
  receipt['offscreen']=review.run(SNAPSHOT)
  review.run(CAMERA)
  receipt['reentry_first']=review.run(SNAPSHOT)
  time.sleep(.15)
  receipt['reentry_growth']=review.run(SNAPSHOT)
  receipt['counters']=review.run(COUNTERS)
  assert all(w['components']>0 and w['batches'] for w in receipt['visible']['worlds']),receipt['visible']
  assert all(w['logical']>0 and w['components']==0 for w in receipt['offscreen']['worlds']),receipt['offscreen']
  assert all(w['components']>0 for w in receipt['reentry_growth']['worlds']),receipt['reentry_growth']
  w0=65.6000009775
  for snap in ('visible','reentry_growth'):
   for w in receipt[snap]['worlds']:
    assert w['components']<=math.ceil(args.count/32),('Batch component count',w)
    for b in w['batches']:
     assert all(1.5*w0-.1<=d<=2.5*w0+.1 for d in b['diameters_cm']),('Pulse diameter',b)
     assert all(0<=length<=5000.1 for length in b['per_slot_trail_lengths_cm']),('Trail length',b)
     assert all(n<=8 for n in b['per_slot_segments']),('Trail segments',b)
  receipt['success']=True
  print(json.dumps({'success':True,'requested':args.count,'worlds':[{k:w[k] for k in ['world','logical','components','warnings','circles']} for w in receipt['visible']['worlds']]},ensure_ascii=False),flush=True)
 finally:
  (OUT/f'wingman-runtime-{args.count}-receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf-8')
  review.stop()
  restore_controls(review,initial)

if __name__=='__main__':main()
