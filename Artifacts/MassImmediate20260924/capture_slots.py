"""One-off authorized PIE observation through the existing player command endpoints."""
import unreal,json,time,builtins,traceback
from pathlib import Path
root=Path('D:/UE5.7/test1/Artifacts/MassImmediate20260924')
r={'started':None,'step':0,'next_sample':0.,'events':[],'file':open(root/'runtime-slots.jsonl','w',encoding='utf-8')}
builtins._mass_slots=r
schedule=[(0.,'same-type',970101),(.8,'remove-group',970102),(1.6,'stop',970103),(2.4,'move',970104),
 (64.,'stop',970105),(65.,'radius',970106),(66.,'stop',970107),(67.,'move',970108),
 (125.,'move',970109),(125.18,'move',970110),(128.,'resync',970111),(131.,'stop',970112)]
def tick(delta):
 import unreal,json,time,builtins,traceback
 from pathlib import Path
 r=builtins._mass_slots
 try:
  if r['started'] is None:
   ws=unreal.EditorLevelLibrary.get_pie_worlds(True)
   sw=next((w for w in ws if unreal.GameplayStatics.get_game_mode(w)),None)
   if not sw:return
   a=unreal.find_object(None,sw.get_path_name()+':GuLiBattleAuthoritySubsystem_0')
   if not a:return
   d=json.loads(a.get_move_response_diagnostics())
   if len(d.get('soldiers',[]))<500:return
   pc=None
   for w in ws:
    if w==sw:continue
    pcs=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiCommanderPlayerController)
    if pcs and pcs[0].player_state.get_team()==unreal.GuLiTeam.BLUE:pc=pcs[0];break
   if not pc:return
   sync=pc.get_component_by_class(unreal.GuLiCommanderNetSyncComponent)
   if not json.loads(sync.get_move_response_diagnostics())['ready']:return
   if 'ready_at' not in r:r['ready_at']=time.perf_counter();return
   if time.perf_counter()-r['ready_at']<4.:return
   units=[s for s in d['soldiers'] if s['team']==2 and s['can_act']]
   seed=min(units,key=lambda s:s['id'])
   r.update(authority=a,sync=sync,seed=seed['id'],started=time.perf_counter(),target=None)
  now=time.perf_counter()-r['started'];sync=r['sync']
  if r['step']<len(schedule) and now>=schedule[r['step']][0]:
   _,action,command=schedule[r['step']];d=json.loads(r['authority'].get_move_response_diagnostics());net=json.loads(sync.get_move_response_diagnostics())
   units=[s for s in d['soldiers'] if s['team']==2 and s['can_act']]
   selected=[s for s in units if s['id'] in net['selected']]
   seed=r['seed'];pos=[0.,0.,0.]
   if action=='same-type':pos=next(s['position'] for s in units if s['id']==seed)
   elif action=='remove-group':seed=min(net['selected'])
   elif action=='radius':
    # Same radius selection a player can make; choose a region containing both visible unit types.
    candidates=[s['position'] for s in units[::10]]
    def score(p):
     nearby=[s for s in units if sum((s['position'][i]-p[i])**2 for i in (0,1))<=9000**2]
     return (len(set(s['unit_type'] for s in nearby)),len(nearby))
    pos=max(candidates,key=score)
   elif action=='move':
    if command in (970104,970108):
     pos=[sum(s['position'][i] for s in selected)/len(selected) for i in range(3)]
     pos[1]+=6000.;r['target']=pos
    else:
     pos=list(r['target']);pos[0]+=6000 if command==970109 else -6000
   queued=sync.submit_move_response_diagnostic(action,command,seed,unreal.Vector(*pos))
   e={'t':now,'command':command,'action':action,'selected':net['selected'],'selected_types':sorted(set(s['unit_type'] for s in selected)),'position':pos,'queued':queued}
   r['events'].append(e);r['file'].write(json.dumps({'event':e})+'\n');r['file'].flush();r['step']+=1
  if now>=r['next_sample']:
   recent=now-(r['events'][-1]['t'] if r['events'] else 0.)<3.
   r['next_sample']=now+(.15 if recent else 1.)
   d=json.loads(r['authority'].get_move_response_diagnostics());net=json.loads(sync.get_move_response_diagnostics())
   d['soldiers']=[s for s in d['soldiers'] if s['team']==2]
   r['file'].write(json.dumps({'t':now,'authority':d,'client':net},separators=(',',':'))+'\n');r['file'].flush()
  if now>=134:
   r['file'].close();unreal.unregister_slate_post_tick_callback(r['handle']);r['complete']=True
   Path('D:/UE5.7/test1/Artifacts/MassImmediate20260924/runtime-slots-events.json').write_text(json.dumps(r['events'],indent=2),encoding='utf-8')
 except Exception:
  err=traceback.format_exc();r['file'].close();unreal.unregister_slate_post_tick_callback(r['handle'])
  Path('D:/UE5.7/test1/Artifacts/MassImmediate20260924/slots-error.txt').write_text(err,encoding='utf-8')
r['handle']=unreal.register_slate_post_tick_callback(tick)
print({'started':unreal.GuLiComponentSkillQALibrary.start_pie(2,2)})
