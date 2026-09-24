import unreal,json,time,builtins,traceback
from pathlib import Path
r={'started':None,'step':0,'next_sample':0.,'events':[],'file':open('D:/UE5.7/test1/Artifacts/MassImmediate20260924/runtime-blue.jsonl','w',encoding='utf-8')}
builtins._mass_blue=r
schedule=[(0.,'same-type',960101),(.8,'stop',960102),(1.6,'move',960103),(5.5,'stop',960104),(6.3,'move',960105),(10.,'stop',960106),(10.8,'move',960107),(10.92,'move',960108),(15.,'stop',960109),(16.,'group',960110),(17.,'move',960111),(22.,'resync',960112),(25.,'stop',960113),(26.,'clear',960114)]
def tick(delta):
 import unreal,json,time,builtins,traceback
 from pathlib import Path
 r=builtins._mass_blue
 try:
  if r['started'] is None:
   ws=unreal.EditorLevelLibrary.get_pie_worlds(True)
   if len(ws)<3:return
   sw=next((w for w in ws if unreal.GameplayStatics.get_game_mode(w)),None)
   if not sw:return
   a=unreal.find_object(None,sw.get_path_name()+':GuLiBattleAuthoritySubsystem_0')
   if not a:return
   d=json.loads(a.get_move_response_diagnostics())
   if len(d.get('soldiers',[]))<500:return
   cw=None
   for w in ws:
    if w==sw:continue
    pcs=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiCommanderPlayerController)
    if pcs and pcs[0].player_state.get_team()==unreal.GuLiTeam.BLUE:cw=w;pc=pcs[0];break
   if not cw:return
   sync=pc.get_component_by_class(unreal.GuLiCommanderNetSyncComponent)
   if not json.loads(sync.get_move_response_diagnostics())['ready']:return
   units=[s for s in d['soldiers'] if s['team']==2 and s['can_act']]
   seed=min(units,key=lambda s:s['id']);r.update(authority=a,sync=sync,seed=seed['id'],seed_position=seed['position'],center=[sum(s['position'][i] for s in units[:100])/len(units[:100]) for i in range(3)],started=time.perf_counter())
  now=time.perf_counter()-r['started'];sync=r['sync']
  while r['step']<len(schedule) and now>=schedule[r['step']][0]:
   _,action,command=schedule[r['step']];d=json.loads(r['authority'].get_move_response_diagnostics());net=json.loads(sync.get_move_response_diagnostics());pos=list(r['center']);seed=r['seed']
   if action=='same-type':pos=next(s['position'] for s in d['soldiers'] if s['id']==seed)
   if action=='group':seed=min(net['selected'])
   if action=='move':
    pos[0]+=8000 if command in (960103,960105) else -8000 if command==960107 else 3000
    pos[1]+=0 if command in (960103,960105,960107) else 8000
   queued=sync.submit_move_response_diagnostic(action,command,seed,unreal.Vector(*pos))
   e={'t':now,'command':command,'action':action,'selected':net['selected'],'position':pos,'queued':queued};r['events'].append(e);r['file'].write(json.dumps({'event':e})+'\n');r['step']+=1
  if now>=r['next_sample']:
   r['next_sample']=now+.2;r['file'].write(json.dumps({'t':now,'authority':json.loads(r['authority'].get_move_response_diagnostics()),'client':json.loads(sync.get_move_response_diagnostics())},separators=(',',':'))+'\n')
  if now>=29:
   r['file'].close();unreal.unregister_slate_post_tick_callback(r['handle']);r['complete']=True
   Path('D:/UE5.7/test1/Artifacts/MassImmediate20260924/runtime-blue-events.json').write_text(json.dumps(r['events'],indent=2),encoding='utf-8')
 except Exception:
  err=traceback.format_exc();r['file'].close();unreal.unregister_slate_post_tick_callback(r['handle']);Path('D:/UE5.7/test1/Artifacts/MassImmediate20260924/blue-error.txt').write_text(err,encoding='utf-8')
r['handle']=unreal.register_slate_post_tick_callback(tick)
print({'started':unreal.GuLiComponentSkillQALibrary.start_pie(2,2)})
