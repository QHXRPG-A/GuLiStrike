"""Moving native combat, same build, mode 0/1/2. Inventory and screenshots only outside captures."""
from __future__ import annotations
import argparse,json,time,sys,traceback
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Scripts'))
from Performance import run_four_stage_review as review
from Performance.run_whole_optimization_review import FIXTURE
from Performance.capture_live_pie import SNAPSHOT
OUT=ROOT/'outputs/performance/20261010-muzzle-batch'
FIELDS=['received_shots','bursts_played','dropped_shots','muzzle_active','muzzle_components','muzzle_accepted','muzzle_born','muzzle_duplicates','muzzle_expired','muzzle_offscreen_recycled','muzzle_batch_published','muzzle_batch_fallbacks','muzzle_pose_queries','muzzle_life_uploads','muzzle_pose_uploads','impact_active','impact_components','impact_batch_published','impact_batch_fallbacks','component_count','last_update_milliseconds','client_flight_data_active','client_flight_actor_active','pose_cache_hits','pose_cache_misses']
COUNTERS=f'''
rows=[]
for w in unreal.EditorLevelLibrary.get_pie_worlds(True):
 row={{'world':w.get_path_name(),'time':unreal.GameplayStatics.get_time_seconds(w),'paused':unreal.GameplayStatics.is_game_paused(w)}}
 p=next((s for s in unreal.ObjectIterator(unreal.GuLiPerformanceSubsystem) if s.get_outer()==w),None)
 if p:row['profile']=json.loads(p.get_capture_json())
 fx=next((s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if s.get_outer()==w),None)
 if fx:
  c=fx.get_counters();row['effects']={{k:c.get_editor_property(k) for k in {FIELDS!r}}}
 f=next((s for s in unreal.ObjectIterator(unreal.GuLiFlightAcceptanceSubsystem) if s.get_outer()==w),None)
 if f:row['load']=json.loads(f.get_load_stats_json())
 row['ui']=[json.loads(s.get_frame_stats_json()) for s in unreal.ObjectIterator(unreal.GuLiSceneUIWidget) if s.get_world()==w]
 rep=next(iter(unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiSoldierStateReplicator)),None)
 if rep:
  states=rep.get_all_soldier_states();row['alive_roster']=sum(s.get_editor_property('Health')>0 for s in states)
 row['server_clock']=unreal.GameplayStatics.get_game_state(w).get_server_world_time_seconds()
 row['flight_network']=[s.get_flight_diagnostics() for s in unreal.ObjectIterator(unreal.GuLiCombatEffectReplicationComponent) if s.get_world()==w]
 if '/UEDPIE_0_' in w.get_path_name():row['native_population']=json.loads(unreal.GuLiComponentSkillQALibrary.performance_population_snapshot(w))
 rows.append(row)
unreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'worlds':rows}}))
'''
OPTIMIZED={'gs.Flights.DataPool':1,'gs.Effects.ThreeTierLOD':1,'gs.Effects.BoundsCull':1,'gs.Effects.OffscreenLifecycle':1,'gs.Units.Offscreen5Hz':1,'gs.Flights.OffscreenPresentation':1,'gs.Impacts.BatchMode':2,'gs.WingmanFlight.Presentation':1,'gs.WingmanFlight.Warnings':1,'gs.SceneUI.ProjectionCache':1,'guli.Commander.MoveLatencyDiagnostics':0}
KEYS=['gs.Muzzles.BatchMode','guli.stronghold.TeamUnitCap','guli.stronghold.CaptureSeconds','r.GPUCsvStatsEnabled','t.IdleWhenNotForeground','Slate.bAllowThrottling','r.VSync','t.MaxFPS',*OPTIMIZED]

def write(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')

def commands(values,editor=False):
 target="unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()" if editor else "next(w for w in unreal.EditorLevelLibrary.get_pie_worlds(True) if '/UEDPIE_1_' in w.get_path_name())"
 return review.result(f'w={target}\n'+'\n'.join(f'unreal.SystemLibrary.execute_console_command(w,{v!r})' for v in values))

def launch(scene,mode,*,fixed_viewports=False,install_review_assets=True,extra_previews=True,load_seconds=65):
 commands([f'gs.Muzzles.BatchMode {mode}','t.IdleWhenNotForeground 0','Slate.bAllowThrottling 0','r.VSync 0','t.MaxFPS 0']+[f'{k} {v}' for k,v in OPTIMIZED.items()]+[f'{k} 3' for k in ['sg.ViewDistanceQuality','sg.AntiAliasingQuality','sg.ShadowQuality','sg.PostProcessQuality','sg.TextureQuality','sg.EffectsQuality','sg.FoliageQuality','sg.ShadingQuality']],True)
 review.run(f"assert not unreal.EditorLevelLibrary.get_pie_worlds(True)\nunreal.MCPythonHelper.submit_result(json.dumps({{'success':unreal.GuLiComponentSkillQALibrary.start_performance_pie({scene=='stress'},1280,720)}}))")
 deadline=time.monotonic()+120
 while time.monotonic()<deadline:
  ready=review.run("ws=unreal.EditorLevelLibrary.get_pie_worlds(True)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'ready':len(ws)==3 and all(unreal.GameplayStatics.get_player_controller(w,0) for w in ws)}))")
  if ready['ready']:break
  time.sleep(.5)
 else:raise RuntimeError('PIE not ready')
 bandwidth=None
 if scene=='stress':
  bandwidth=review.run("rows=[]\nfor w in unreal.EditorLevelLibrary.get_pie_worlds(True):\n r=json.loads(unreal.GuLiComponentSkillQALibrary.set_performance_bandwidth(w,250000));assert r['success'];rows.append(r)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'bytes_per_second':250000,'worlds':rows,'scope':'Production 250000-byte/s link and unchanged flight packet contract. Zero accepted muzzle feedback is reported as upstream starvation, not as a batching gain.'}))")
 if install_review_assets:
  review.run("dest='/Game/GuLiStrike/FX/CommanderWeapons'\nfor w in unreal.EditorLevelLibrary.get_pie_worlds(True)[1:]:\n reg=next(s for s in unreal.ObjectIterator(unreal.GuLiVfxRegistrySubsystem) if s.get_outer()==w)\n row=unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(52)\n for field,suffix in [('BatchResourcePath',''),('ReducedBatchResourcePath','_Reduced'),('MinimalBatchResourcePath','_Minimal')]:row.set_editor_property(field,unreal.load_asset(dest+'/NS_MachineGunMuzzle_Batch'+suffix))\n assert reg.apply_review_definition(row)\n fx=next(s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if s.get_outer()==w)\n assert fx.set_review_muzzle_channel(unreal.load_asset(dest+'/NDC_CommanderMuzzles'))\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'candidate_install':'World-local; no saved references altered'}))")
 count=600 if scene=='stress' else 200
 center=[15000,65000,0] if scene=='stress' else [-10800,65000,0]
 deadline=time.monotonic()+120
 while time.monotonic()<deadline:
  pop=review.run(f"w=unreal.EditorLevelLibrary.get_pie_worlds(True)[0]\nunreal.MCPythonHelper.submit_result(unreal.GuLiComponentSkillQALibrary.prepare_performance_population(w,{count},True,unreal.Vector(*{center!r})))")
  if pop.get('requested')==count and pop.get('alive')==count and pop.get('pending_plans')==0 and pop.get('moving',0)>0:break
  time.sleep(.5)
 else:raise RuntimeError('Population failed: '+str(pop))
 sizes=[[1280,720],[1280,720]] if scene=='stress' or fixed_viewports else [[1505,1115],[640,484]]
 # Keep real ground muzzles inside the existing 20,000 cm effect range while
 # retaining the three flight domains and fixed pressure previews in the view.
 camera=[15000,58000,16000] if scene=='stress' else [-10800,61000,15500]
 target=[15000,65000,5401.779] if scene=='stress' else [-10800,65000,1000]
 setup=review.run(f"ws=unreal.EditorLevelLibrary.get_pie_worlds(True)\nunreal.SystemLibrary.execute_console_command(ws[0],'guli.stronghold.CaptureSeconds 1000000000')\nfor i,w in enumerate(ws[1:]):\n pc=unreal.GameplayStatics.get_player_controller(w,0)\n assert unreal.GuLiComponentSkillQALibrary.set_performance_viewport_size(pc,*{sizes!r}[i])\n cam=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.CameraActor) if a.get_actor_label()=='PerfReview_Camera')\n cam.set_actor_location(unreal.Vector(*{camera!r}),False,True)\n cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(cam.get_actor_location(),unreal.Vector(*{target!r})),True)\n pc.set_editor_property('bAutoManageActiveCameraTarget',False);pc.set_view_target_with_blend(cam,0)\n for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiPerformanceReviewActor):a.stop_comparison()\nunreal.MCPythonHelper.submit_result(json.dumps({{'success':True}}))")
 if scene=='stress':
  if extra_previews:
   setup['extra_previews']=review.run(FIXTURE)
   review.result("for w in unreal.EditorLevelLibrary.get_pie_worlds(True)[1:]:\n a=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiPerformanceReviewActor) if a.get_actor_label()=='PerfReview_Flash_Source')\n a.stop_comparison();a.set_editor_property('System',unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunMuzzle_PressurePreview'));a.start_comparison()")
  ship=review.run("w=unreal.EditorLevelLibrary.get_pie_worlds(True)[0]\nship=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiStrikeShip)[0]\nship.set_actor_location(unreal.Vector(0,65000,10001.779),False,True)\nif not ship.get_hangar_capability():\n assert unreal.GuLiComponentSkillQALibrary.commit_ship_choice(ship.player_state.get_ship_build(),'08') is not None\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True}))")
  deadline=time.monotonic()+30
  while time.monotonic()<deadline:
   started=review.run("w=unreal.EditorLevelLibrary.get_pie_worlds(True)[0]\nf=next(s for s in unreal.ObjectIterator(unreal.GuLiFlightAcceptanceSubsystem) if s.get_outer()==w)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'started':f.start_load(500,LOAD_SECONDS)}))".replace('LOAD_SECONDS',str(load_seconds)))
   if started['started']:break
   time.sleep(.5)
  else:raise RuntimeError('Three source flights not ready')
 setup.update(population=pop,sizes=sizes,camera=camera,target=target,health=500000,bandwidth=bandwidth,motion='Normal authority spawn and accepted native move planning. Natural combat stops/arrivals retained, no unit tick or avoidance disabled.')
 return setup

def capture(scene,round_number,mode,gpu=True):
 directory=OUT/'paired'/f'{scene}-r{round_number}-mode{mode}'
 assert not directory.exists(),str(directory);directory.mkdir(parents=True)
 print(json.dumps({'stage':'setup','scene':scene,'round':round_number,'mode':mode}),flush=True)
 setup=launch(scene,mode);time.sleep(10)
 review.result(review.LOCK_VIEW);time.sleep(.2)
 before=review.run(COUNTERS);context=review.run(SNAPSHOT)
 assert all(not w['paused'] for w in before['worlds'])
 write(directory/'counters-before.json',before);write(directory/'context-before.json',context)
 commands(['r.GPUCsvStatsEnabled 1'])
 profiles="ws=list(unreal.EditorLevelLibrary.get_pie_worlds(True))\nfor s in unreal.ObjectIterator(unreal.GuLiPerformanceSubsystem):\n if s.get_outer() in ws:s.begin_capture()"
 review.result(profiles)
 csvpath=directory/'frames.csv';tracepath=directory/'session.utrace'
 commands([f'Trace.File {tracepath.as_posix()} cpu,gpu,frame,bookmark',f'CsvProfile STARTFILE=../../../{csvpath.relative_to(ROOT).as_posix()}','CsvProfile START','Trace.Bookmark MuzzlePairStart'])
 begin=time.monotonic();print(json.dumps({'stage':'capture','scene':scene,'round':round_number,'mode':mode,'seconds':30}),flush=True);time.sleep(30);end=time.monotonic()
 commands(['Trace.Bookmark MuzzlePairEnd','CsvProfile STOP','Trace.Stop'])
 review.result(profiles.replace('begin_capture','end_capture'))
 after=review.run(COUNTERS);write(directory/'counters-after.json',after);write(directory/'context-after.json',review.run(SNAPSHOT))
 write(directory/'clock.json',{'window_monotonic_seconds':[begin,end],'clock_basis':'Windows QPC seconds'})
 deadline=time.monotonic()+15
 while time.monotonic()<deadline:
  if csvpath.is_file() and '[hasheaderrowatend]' in csvpath.read_text(encoding='utf-8',errors='replace')[-65536:].lower():break
  time.sleep(.25)
 stats=review.summarize_csv(csvpath)
 native_before=next(w['native_population'] for w in before['worlds'] if 'native_population' in w)
 native_after=next(w['native_population'] for w in after['worlds'] if 'native_population' in w)
 coordinates={p['id']:p['position'] for p in native_before.get('positions',[])}
 displaced=sum(sum((v-coordinates[p['id']][i])**2 for i,v in enumerate(p['position']))>100 for p in native_after.get('positions',[]) if p['id'] in coordinates)
 throughput=[]
 for b,a in zip(before['worlds'][1:],after['worlds'][1:]):
  throughput.append({k:a['effects'][k]-b['effects'][k] for k in ['received_shots','muzzle_accepted','muzzle_born','muzzle_batch_published','muzzle_batch_fallbacks']})
 views=[c for w in context['worlds'] for c in w['controllers'] if c['local']]
 expected=600 if scene=='stress' else 200
 expiry_delta=[a['effects']['muzzle_expired']-b['effects']['muzzle_expired'] for b,a in zip(before['worlds'][1:],after['worlds'][1:])]
 eligible=native_before['alive']==native_after['alive']==expected and displaced>0 and all(t['muzzle_born']>0 for t in throughput) and all(expired<=max(4,t['muzzle_accepted']*.03) for expired,t in zip(expiry_delta,throughput)) and [c['viewport'] for c in views]==setup['sizes'] and all(c['view_target']=='PerfReview_Camera' and not c['auto_manage_camera'] for c in views)
 if scene=='stress':eligible=eligible and all(w.get('load',{}).get('domains')==[167,167,166] for w in [before['worlds'][0],after['worlds'][0]])
 receipt={'scene':scene,'round':round_number,'mode':mode,'case':scene,'pair':round_number,'variant':f'mode{mode}','adoption_eligible':eligible,'movement_units_displaced':displaced,'shot_throughput':throughput,'warmup_seconds':10,'capture_seconds':end-begin,'setup':setup,'csv':stats,'trace':tracepath.as_posix(),'scope':'Same source build, dedicated server plus two clients and editor. Mode1 vs2 isolates batching. Existing optimizations remain on. Real combat feedback and extra stress previews use distinct Niagara asset names and are reported separately.'}
 write(directory/'result.json',receipt)
 if gpu and round_number==1:
  review.run(f"w=next(w for w in unreal.EditorLevelLibrary.get_pie_worlds(True) if '/UEDPIE_1_' in w.get_path_name())\np=unreal.GameplayStatics.get_player_controller(w,0)\nunreal.MCPythonHelper.submit_result(json.dumps({{'success':unreal.GuLiComponentSkillQALibrary.capture_performance_viewport(p,{(directory/'client1.png').as_posix()!r})}}))")
  log=ROOT/'Saved/Logs/GuLiStrike.log';offset=log.stat().st_size
  commands(['r.ProfileGPU.ShowUI 0','r.ProfileGPU.Sort 0','r.ProfileGPU.ThresholdPercent 0.2','r.ProfileGPU.UnicodeOutput 0','ProfileGPU']);time.sleep(2)
  with log.open('rb') as stream:stream.seek(offset);(directory/'profile-gpu.txt').write_bytes(stream.read())
 print(json.dumps({'stage':'done','scene':scene,'round':round_number,'mode':mode,'csv':stats}),flush=True)
 review.stop();return receipt

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--scenes',nargs='+',default=['dense200','stress']);ap.add_argument('--rounds',type=int,default=3);ap.add_argument('--modes',nargs='+',type=int,choices=[0,1,2],default=[0,1,2]);ap.add_argument('--resume',action='store_true');ap.add_argument('--smoke',action='store_true');args=ap.parse_args()
 original=review.run(f"assert not unreal.EditorLevelLibrary.get_pie_worlds(True)\nunreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'values':{{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in {KEYS!r}}}}}))")['values'];write(OUT/'capture-original-controls.json',original)
 review.run("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\nn=unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(w,True)\nr=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()\nunreal.MCPythonHelper.submit_result(json.dumps({'success':r.success and n.success,'navigation':n.message,'resources':r.message}))",timeout=180)
 try:
  if args.smoke:
   launch('dense200',2);time.sleep(2);write(OUT/'smoke-counters.json',review.run(COUNTERS));print('smoke ready',flush=True);return
  for scene in args.scenes:
   for r in range(1,args.rounds+1):
    for mode in ([0,1,2] if r==1 else [2,1,0] if r==2 else [1,0,2]):
     if mode not in args.modes:continue
     if args.resume and (OUT/'paired'/f'{scene}-r{r}-mode{mode}'/'result.json').exists():continue
     capture(scene,r,mode)
 finally:
  if not args.smoke:review.stop()
  if not args.smoke:commands([f'{k} {v:g}' for k,v in original.items()],True)

if __name__=='__main__':main()
