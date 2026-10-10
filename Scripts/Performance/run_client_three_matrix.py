"""Three paired rounds, isolated rollback controls, fixed source build and unperturbed 10/30 s windows."""
from __future__ import annotations
import argparse,json,sys,time,traceback
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Scripts'))
from Performance import run_four_stage_review as review
from Performance.client_three_review_support import OUT,INSTALL,COUNTERS

BASE_LAUNCH=review.launch
KEYS=['gs.Effects.OffscreenLifecycle','gs.Units.Offscreen5Hz','gs.Flights.OffscreenPresentation',
      'gs.Effects.ThreeTierLOD','gs.Impacts.BatchMode','gs.WingmanFlight.Presentation','gs.WingmanFlight.Warnings',
      'gs.Effects.BoundsCull','gs.Flights.DataPool','gs.SceneUI.ProjectionCache','gs.Avoidance.QueryOptimizations',
      'guli.stronghold.TeamUnitCap','guli.stronghold.CaptureSeconds','t.IdleWhenNotForeground','Slate.bAllowThrottling',
      'r.VSync','t.MaxFPS','sg.ViewDistanceQuality','sg.AntiAliasingQuality','sg.ShadowQuality','sg.PostProcessQuality',
      'sg.TextureQuality','sg.EffectsQuality','sg.FoliageQuality','sg.ShadingQuality']

CAMERA=r'''
ws=unreal.EditorLevelLibrary.get_pie_worlds(True)
marker=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(ws[0],unreal.Actor) if unreal.Name('FlightEventsQAOrigin') in a.tags)
o=marker.get_actor_location()
if family=='007':
 target=unreal.Vector(o.x,o.y+1200,o.z+4500);location=unreal.Vector(o.x-6000,o.y-12000,o.z+13000)
elif family=='whole':
 target=unreal.Vector(o.x+15000,o.y,o.z+4500);location=unreal.Vector(o.x+15000,o.y-10000,o.z+22000)
elif view=='visible':
 target=unreal.Vector(-14900,-19500,1500);location=unreal.Vector(-14900,-24500,6000)
elif view=='mixed':
 target=unreal.Vector(-9800,-19500,1500);location=unreal.Vector(-9800,-29500,8000)
else:
 target=unreal.Vector(50000,50000,1500);location=unreal.Vector(50000,45000,6000)
for w in ws[1:]:
 cam=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.CameraActor) if a.get_actor_label()=='PerfReview_Camera')
 cam.set_actor_location(location,False,True);cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location,target),True)
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'location':list(location.to_tuple()),'target':list(target.to_tuple())}))
'''

FIXTURE=r'''
ws=unreal.EditorLevelLibrary.get_pie_worlds(True)
marker=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(ws[0],unreal.Actor) if unreal.Name('FlightEventsQAOrigin') in a.tags)
o=marker.get_actor_location()
rows=[]
for w in ws[1:]:
 for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiPerformanceReviewActor):a.stop_comparison()
 if family=='007':continue
 for role,label,effect,offset in [('Mining','PerfReview_Mining_Opaque',36,-2000),('Construction','PerfReview_Construction_Opaque',45,2000),('Muzzle','PerfReview_Flash_Source',52,0),('Impact','PerfReview_Flash_Impact',5,4000)]:
  if family=='008' and role!='Impact':continue
  a=next(x for x in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiPerformanceReviewActor) if x.get_actor_label()==label)
  row=unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(effect)
  path=row.resource_path
  if effect==36:path=unreal.load_asset('/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Optimized_GPU_All_ThreeTier')
  if effect==45:path=unreal.load_asset('/Game/GuLiStrike/Buildings/Construction/NS_ConstructionLaser_Optimized_GPU_All_ThreeTier')
  a.set_editor_property('System',path);a.set_editor_property('BaseScale',row.scale)
  a.set_editor_property('EffectCount',16);a.set_editor_property('bCatalogImpacts',role=='Impact')
  a.set_editor_property('ImpactEventsPerSecond',200)
  # Whole view retains the old fixture geometry; individual effect view is near the saved review area.
  position=unreal.Vector(o.x+15000+offset,o.y,o.z+3000) if family=='whole' else unreal.Vector(-14900+offset,-19500,1500)
  if family=='008':position=unreal.Vector(-14900,-19500,1500)
  a.set_actor_location(position,False,True)
  a.set_editor_property('PolicyEffectId',effect if role!='Impact' else 0)
  a.start_comparison()
  rows.append({'world':w.get_path_name(),'role':role,'effect':effect,'count':16,'catalog_events_per_second':200 if role=='Impact' else 0,'position':list(position.to_tuple())})
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'fixtures':rows}))
'''

def commands(family,variant):
    screen=lod=unit=flight=wing=warn=0;mode=0
    if family=='006':
        screen=unit=flight=int(variant in ('offscreen','combined'));lod=int(variant in ('lod','combined'))
    elif family=='008':screen=unit=flight=lod=1;mode=int(variant[-1])
    elif family=='007':screen=unit=flight=lod=1;mode=2;wing=int(variant!='old');warn=int(variant=='new_warning')
    elif variant=='all':screen=unit=flight=lod=wing=warn=1;mode=2
    values={'gs.Effects.OffscreenLifecycle':screen,'gs.Units.Offscreen5Hz':unit,'gs.Flights.OffscreenPresentation':flight,
       'gs.Effects.ThreeTierLOD':lod,'gs.Impacts.BatchMode':mode,'gs.WingmanFlight.Presentation':wing,'gs.WingmanFlight.Warnings':warn,
       'r.VSync':0,'t.MaxFPS':0}
    return [f'{k} {v}' for k,v in values.items()]+[f'{k} 3' for k in KEYS if k.startswith('sg.')]

def launch_case(family,view,count,variant):
    review.result("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"+'\n'.join(
        f'unreal.SystemLibrary.execute_console_command(w,{x!r})' for x in commands(family,variant)))
    setup=BASE_LAUNCH('runtime' if family=='whole' else 'flight' if family=='007' else 'nodes8',
                      'optimized_controls' if family=='whole' else 'data_pool' if family=='007' else 'Nodes8',start_flight_load=False)
    setup['review_candidates']=review.run(INSTALL)
    setup['camera']=review.run(f'family={family!r}\nview={view!r}\n'+CAMERA)
    setup['fixture']=review.run(f'family={family!r}\n'+FIXTURE)
    if family=='whole':
        setup['load']=review.run("w=unreal.EditorLevelLibrary.get_pie_worlds(True)[0]\nf=next(s for s in unreal.ObjectIterator(unreal.GuLiFlightAcceptanceSubsystem) if s.get_outer()==w)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':f.start_load(500,60)}))")
    elif family=='007':
        style='/Game/GuLiStrike/FX/GroundWarning/DA_GroundWarning_Red'
        setup['load']=review.run("w=unreal.EditorLevelLibrary.get_pie_worlds(True)[0]\nf=next(s for s in unreal.ObjectIterator(unreal.GuLiFlightAcceptanceSubsystem) if s.get_outer()==w)\n"+
            f"unreal.MCPythonHelper.submit_result(json.dumps({{'success':f.start_wingman_ground_load({count},60,unreal.load_asset({style!r}))}}))")
    setup.update({'family':family,'view':view,'ground_missile_count':count,'controls':commands(family,variant),
        'fixture_scope':'Native production catalog hit entry at 200/s/client, 16 tool beams and 16 muzzle previews per role/client. Four previous optimization controls stay enabled.'})
    return setup

def matrix(families):
    for family in families:
        views=['visible','mixed','offscreen'] if family=='006' else ['visible']
        counts=[125,250,500] if family=='007' else [0]
        variants={'006':['current','offscreen','lod','combined'],'008':['mode0','mode1','mode2'],
                  '007':['old','new','new_warning'],'whole':['current','all']}[family]
        for view in views:
            for count in counts:
                for pair in range(1,4):
                    for variant in variants if pair%2 else list(reversed(variants)):
                        yield family,view,count,pair,variant

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--families',nargs='+',default=['006','008','007','whole']);ap.add_argument('--resume',action='store_true');args=ap.parse_args()
    initial=review.run(f"assert not unreal.EditorLevelLibrary.get_pie_worlds(True)\nunreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'values':{{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in {KEYS!r}}}}}))")['values']
    review.OUT=OUT;review.COUNTERS=COUNTERS;records=[]
    try:
        for family,view,count,pair,variant in matrix(args.families):
            label=f'{family}-{view}'+(f'-{count}' if count else '')+'-'+variant
            done=OUT/'paired'/f'{"runtime" if family=="whole" else "flight"}-p{pair}-{label}'/'result.json'
            if args.resume and done.exists() and json.loads(done.read_text()).get('adoption_eligible'):continue
            review.launch=lambda _case,_variant,f=family,v=view,n=count,label_variant=variant:launch_case(f,v,n,label_variant)
            record=review.capture('runtime' if family=='whole' else 'flight',pair,label)
            record.update({'family':family,'view':view,'ground_missile_count':count,
              'scope':'Current source build with three new local rollback controls versus candidates. Previous four optimizations remain enabled; no old source restored. Same-process dedicated server and two 1280x720 clients.'})
            paths=sorted((OUT/'paired').glob(f'{"runtime" if family=="whole" else "flight"}-p{pair}-{label}*/result.json'),key=lambda p:p.stat().st_mtime)
            paths[-1].write_text(json.dumps(record,indent=2),encoding='utf-8');records.append(record)
            (OUT/'matrix-progress.json').write_text(json.dumps({'success':True,'finished':len(records),'last':str(paths[-1]),'matrix_complete':False},indent=2),encoding='utf-8')
        (OUT/'matrix-progress.json').write_text(json.dumps({'success':True,'finished_this_run':len(records),'families':args.families,'matrix_complete':True},indent=2),encoding='utf-8')
    finally:
        review.stop()
        review.result("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"+'\n'.join(f'unreal.SystemLibrary.execute_console_command(w,{f"{k} {v:g}"!r})' for k,v in initial.items()))

if __name__=='__main__':main()
