"""Explicitly authorized PIE regressions. Never run inside a timing window."""
import json,time,sys,math,collections
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Scripts'))
from Performance import run_four_stage_review as review
OUT=ROOT/'outputs/performance/20261010-muzzle-batch'
BASE="ws=unreal.EditorLevelLibrary.get_pie_worlds(True)\nw=ws[1]\nfx=next(s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if s.get_outer()==w)\n"
report={'success':False,'checks':{},'evidence':{},'scope':'Client-local synthetic inputs plus real moving combat; no measurements in these windows.'}
def snap():
 return review.run(BASE+"c=fx.get_counters()\ns=json.loads(fx.get_muzzle_protocol_snapshot())\ns['effects']={k:c.get_editor_property(k) for k in ['muzzle_born','muzzle_batch_published','muzzle_batch_fallbacks','muzzle_expired','muzzle_offscreen_recycled','impact_batch_published','impact_batch_fallbacks']}\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'snapshot':s}))")['snapshot']
def emit(first,count=1,mode=2,velocity=(0,0,0)):
 return review.run(BASE+f"results=[fx.emit_review_muzzle_input(i,unreal.Vector(-10800+(i%10)*100,65000,1600),unreal.Rotator(0,(i%10)*35,0),i%2==0,{mode},unreal.Vector(*{velocity!r})) for i in range({first},{first+count})]\nunreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'accepted':sum(results)}}))")
def select(s,first,count=1):
 return [v for v in s['slots'] if v['shot'].upper().startswith('47534D5A') and first<=int(v['shot'].replace('-','')[8:16],16)<first+count]
def check(name,condition,evidence):
 report['checks'][name]=bool(condition);report['evidence'][name]=evidence
 if not condition:raise AssertionError(name+': '+str(evidence))
try:
 probe=json.loads((OUT/'contract-frame-readback.json').read_text())
 slots=[s for frame in probe['frames'] for world in frame for s in world['slots'] if s['born_frame']>=0]
 followers=[r for frame in probe['frames'] for world in frame for system in world['systems'] for e in system['emitters'] if e['emitter']=='RIbbonTrailFollower' for r in e['rows']]
 check('F_plus_1',bool(slots) and all(s['born_frame']==s['published_frame']+1 for s in slots),{'samples':len(slots)})
 check('heavy_scale_once',{s['scale'] for s in slots}=={1,2},{'scales':sorted({s['scale'] for s in slots})})
 check('follower_identity',bool(followers) and all(r.get('ribbon_slot')==r['slot'] and r.get('ribbon_generation')==r['generation'] for r in followers),{'samples':len(followers)})
 check('per_event_world_render',all('render_position' in r and r['render_position'][1]>60000 for r in followers),{'samples':len(followers)})
 review.result(BASE+'assert fx.reset_review_muzzles()')
 before=snap();accepted=emit(200001,600)['accepted'];time.sleep(.12);burst=snap();live=select(burst,200001,600)
 check('unbounded_pool_growth',accepted==600 and burst['capacity']>=768,{'accepted':accepted,'capacity':burst['capacity'],'observed_live':len(live)})
 other=review.run("w=unreal.EditorLevelLibrary.get_pie_worlds(True)[2]\nfx=next(s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if s.get_outer()==w)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'snapshot':json.loads(fx.get_muzzle_protocol_snapshot())}))")['snapshot']
 check('World_isolation',not select(other,200001,600),{'other_client_review_slots':len(select(other,200001,600))})
 handles={s['slot']:s['generation'] for s in live};time.sleep(1.3)
 emit(300001,600);time.sleep(.12);reused=select(snap(),300001,600)
 shared=[s for s in reused if s['slot'] in handles]
 check('slot_reuse',bool(shared) and all(s['generation']>handles[s['slot']] for s in shared),{'reused_sampled':len(shared)})
 emit(400001);time.sleep(.12);active=select(snap(),400001)
 review.result(BASE+'assert fx.remove_review_muzzle_source(400001)');time.sleep(.12)
 check('source_loss',bool(active) and not select(snap(),400001),{'before':active})
 # Fail closed at the same LOD, with the allowed F+1 single fallback.
 review.result(BASE+"reg=next(s for s in unreal.ObjectIterator(unreal.GuLiVfxRegistrySubsystem) if s.get_outer()==w)\nrow=unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(52)\nfor field,suffix in [('BatchResourcePath',''),('ReducedBatchResourcePath','_Reduced'),('MinimalBatchResourcePath','_Minimal')]:row.set_editor_property(field,unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunMuzzle_AllOptimizations'+suffix))\nassert reg.apply_review_definition(row)")
 b=snap();emit(400002);time.sleep(.12);fallback=snap();f=select(fallback,400002)
 check('same_tier_F1_fallback',bool(f) and all(s['mode']==1 and s['born_frame']==s['published_frame']+1 for s in f) and fallback['effects']['muzzle_batch_fallbacks']>b['effects']['muzzle_batch_fallbacks'],{'slots':f})
 review.result(BASE+"reg=next(s for s in unreal.ObjectIterator(unreal.GuLiVfxRegistrySubsystem) if s.get_outer()==w)\nrow=unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(52)\nfor field,suffix in [('BatchResourcePath',''),('ReducedBatchResourcePath','_Reduced'),('MinimalBatchResourcePath','_Minimal')]:row.set_editor_property(field,unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunMuzzle_Batch'+suffix))\nassert reg.apply_review_definition(row)")
 # Fully offscreen lifecycle; authoritative units continue moving and firing.
 review.result(BASE+"cam=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.CameraActor) if a.get_actor_label()=='PerfReview_Camera')\ncam.set_actor_location(unreal.Vector(1000000,1000000,15500),False,True)")
 time.sleep(1.2);idle=snap()
 check('offscreen_idle',idle['components']==0,{'components':idle['components'],'offscreen_recycled':idle['effects']['muzzle_offscreen_recycled']})
 review.result(BASE+"cam=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.CameraActor) if a.get_actor_label()=='PerfReview_Camera')\ncam.set_actor_location(unreal.Vector(-10800,61000,15500),False,True)")
 time.sleep(.12);emit(500001,velocity=(700,200,0));time.sleep(.12);awake=snap()
 check('idle_wake_no_replay',not select(awake,200001,600) and not select(awake,300001,600) and bool(select(awake,500001)),{'new_slot':select(awake,500001),'old_slots':len(select(awake,200001,600))+len(select(awake,300001,600))})
 first=select(awake,500001);time.sleep(.12);second=select(snap(),500001)
 check('moving_follow',bool(first) and bool(second) and second[0]['position'][0]>first[0]['position'][0],{'before':first,'after':second})
 review.result(BASE+"unreal.SystemLibrary.execute_console_command(w,'gs.Muzzles.BatchMode 1')")
 time.sleep(.12);after=snap();duplicate=emit(500001)['accepted']
 check('mode_switch_no_replay',not select(after,500001) and duplicate==0,{'old_slots':len(select(after,500001)),'duplicate_accepted':duplicate})
 # Use the same production BeginEpoch/reset code, restricted to the review client.
 emit(600001);time.sleep(.12);old=select(snap(),600001)
 review.result(BASE+'assert fx.advance_review_effect_epoch()')
 after=snap();again=emit(600001)['accepted'];time.sleep(.12);new=select(snap(),600001)
 check('epoch_reset',bool(old) and not select(after,600001) and again==1 and bool(new) and new[0]['epoch']!=old[0]['epoch'],{'old':old,'new':new})
 report['success']=True
finally:
 (OUT/'runtime-contract-validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
 print(json.dumps({'success':report['success'],'checks':report['checks']},ensure_ascii=False),flush=True)
