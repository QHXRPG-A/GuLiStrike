import unreal,json,time,builtins,traceback
worlds=unreal.EditorLevelLibrary.get_pie_worlds(True)
server=next(w for w in worlds if unreal.GameplayStatics.get_game_mode(w))
authority=unreal.find_object(None,server.get_path_name()+':GuLiBattleAuthoritySubsystem_0')
clients=[]
for w in worlds:
 if w==server: continue
 pc=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiCommanderPlayerController)[0]
 clients.append((pc,pc.get_component_by_class(unreal.GuLiCommanderNetSyncComponent)))
clients.sort(key=lambda pair: int(pair[0].player_state.get_team().value))
initial=json.loads(authority.get_move_response_diagnostics())
red=[s for s in initial['soldiers'] if s['team']==1 and s['can_act']][:25]
center=[sum(s['position'][i] for s in red)/len(red) for i in range(3)]
run={'authority':authority,'clients':clients,'started':time.perf_counter(),'next_sample':0.,'step':0,'center':center,'events':[],'frames':0}
run['file']=open('D:/UE5.7/test1/Artifacts/MassResponse20260923/runtime-25.jsonl','w',encoding='utf-8')
builtins._mass_response_capture=run
schedule=[(0.,'radius',920001),(.7,'group',920002),(1.4,'stop',920003),(2.2,'move',920004),(7.,'move',920005),(7.15,'stop',920006),(9.5,'move',920007),(15.,'resync',920008),(19.,'clear',920009),(20.,'radius',920010),(21.,'group',920011),(22.,'stop',920012)]
run['schedule']=schedule

def capture_tick(delta):
 import unreal,json,time,builtins,traceback
 r=builtins._mass_response_capture
 try:
  now=time.perf_counter()-r['started'];sync=r['clients'][0][1]
  if r['step']<len(r['schedule']) and now>=r['schedule'][r['step']][0]:
   _,action,command=r['schedule'][r['step']];seed=0;pos=list(r['center'])
   net=json.loads(sync.get_move_response_diagnostics())
   if action=='group' and net['selected']: seed=min(net['selected'])
   if action=='move':
    pos[0]+=4500 if command==920004 else 10000 if command==920005 else 8000
    pos[1]-=4000 if command==920004 else 8000 if command==920005 else 2000
   if command==920010:
    a=json.loads(r['authority'].get_move_response_diagnostics());ids=set(r.get('ids',[]));members=[s for s in a['soldiers'] if s['id'] in ids and s['can_act']]
    if members: pos=[sum(s['position'][i] for s in members)/len(members) for i in range(3)]
   ok=sync.submit_move_response_diagnostic(action,command,seed,unreal.Vector(*pos))
   event={'t':now,'action':action,'command':command,'seed':seed,'position':pos,'queued':ok,'selected':net['selected']}
   if action=='move' and command==920004: r['ids']=list(net['selected'])
   r['events'].append(event);r['file'].write(json.dumps({'event':event})+'\n');r['step']+=1
  if now>=r['next_sample']:
   r['next_sample']=now+.1
   a=json.loads(r['authority'].get_move_response_diagnostics());nets=[json.loads(n.get_move_response_diagnostics()) for p,n in r['clients']]
   r['file'].write(json.dumps({'t':now,'authority':a,'clients':nets},separators=(',',':'))+'\n');r['frames']+=1
  if now>=25:
   r['file'].close();unreal.unregister_slate_post_tick_callback(r['handle']);r['complete']=True
   open('D:/UE5.7/test1/Artifacts/MassResponse20260923/runtime-25-events.json','w',encoding='utf-8').write(json.dumps({'events':r['events'],'frames':r['frames'],'duration':now},indent=2))
 except Exception:
  r['error']=traceback.format_exc();r['file'].close();unreal.unregister_slate_post_tick_callback(r['handle'])
  open('D:/UE5.7/test1/Artifacts/MassResponse20260923/runtime-25-error.txt','w').write(r['error'])
run['handle']=unreal.register_slate_post_tick_callback(capture_tick)
print(json.dumps({'capture_started':True,'center':center,'initial_soldiers':len(initial['soldiers'])}))
