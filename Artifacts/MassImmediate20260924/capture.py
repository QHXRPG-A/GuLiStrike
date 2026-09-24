import unreal,json,time,builtins,traceback
from pathlib import Path
worlds=unreal.EditorLevelLibrary.get_pie_worlds(True)
server=next(w for w in worlds if unreal.GameplayStatics.get_game_mode(w))
authority=unreal.find_object(None,server.get_path_name()+':GuLiBattleAuthoritySubsystem_0')
clients=[]
for w in worlds:
 if w==server:continue
 pcs=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiCommanderPlayerController)
 if pcs:
  pc=pcs[0];clients.append((int(pc.player_state.get_team().value),pc,pc.get_component_by_class(unreal.GuLiCommanderNetSyncComponent)))
clients.sort(key=lambda v:v[0]);assert len(clients)==2
initial=json.loads(authority.get_move_response_diagnostics());assert initial['movement_pipeline']=='immediate-shared-navmesh'
centers={team:[sum(s['position'][i] for s in initial['soldiers'] if s['team']==team)/len([s for s in initial['soldiers'] if s['team']==team]) for i in range(3)] for team in (1,2)}
schedule=[(0.,0,'radius',940001),(.7,0,'same-type',940002),(1.4,0,'stop',940003),(2.2,0,'move',940004),(7.,0,'move',940005),(10.,0,'move',940006),(10.08,0,'move',940007),(14.,0,'stop',940008),(15.,0,'radius',940009),(16.,0,'group',940010),(16.7,0,'stop',940011),(17.5,0,'move',940012),(22.,0,'resync',940013),(24.,0,'stop',940014),(25.,0,'radius',940015),(25.,1,'radius',950001),(26.,0,'same-type',940016),(26.,1,'same-type',950002),(27.,0,'stop',940017),(27.,1,'stop',950003),(28.,0,'move',940018),(28.,1,'move',950004),(36.,0,'stop',940019),(36.,1,'stop',950005),(38.,0,'clear',940020),(38.,1,'clear',950006)]
r={'authority':authority,'clients':clients,'centers':centers,'schedule':schedule,'step':0,'started':time.perf_counter(),'next_sample':0,'events':[],'frames':0,'ids':[]}
r['file']=open('D:/UE5.7/test1/Artifacts/MassImmediate20260924/runtime-main.jsonl','w',encoding='utf-8');builtins._mass_immediate=r
def tick(delta):
 import unreal,json,time,builtins,traceback
 from pathlib import Path
 r=builtins._mass_immediate
 try:
  now=time.perf_counter()-r['started']
  while r['step']<len(r['schedule']) and now>=r['schedule'][r['step']][0]:
   _,ci,action,command=r['schedule'][r['step']];team,pc,sync=r['clients'][ci]
   net=json.loads(sync.get_move_response_diagnostics());a=json.loads(r['authority'].get_move_response_diagnostics())
   owned=[s for s in a['soldiers'] if s['team']==team and s['can_act']]
   members=[s for s in owned if s['id'] in set(net['selected'])]
   center=[sum(s['position'][i] for s in (members or owned))/len(members or owned) for i in range(3)]
   pos=list(r['centers'][team]);seed=min(net['selected']) if net['selected'] else owned[0]['id']
   if action=='radius':pos=center
   if action=='same-type':pos=center
   if action=='move':
    if command in (940004,940005):pos=[r['centers'][team][0]+5000,r['centers'][team][1]-4000,r['centers'][team][2]]
    elif command==940006:pos=[center[0]+10000,center[1],center[2]]
    elif command==940007:pos=[center[0]-6000,center[1]-6000,center[2]]
    else:pos=[center[0]+5000,center[1]+(4000 if team==2 else -4000),center[2]]
   ok=sync.submit_move_response_diagnostic(action,command,seed,unreal.Vector(*pos))
   event={'t':now,'action':action,'command':command,'team':team,'position':pos,'queued':ok,'selected':net['selected'],'authority_clock':a['clock']}
   r['events'].append(event);r['file'].write(json.dumps({'event':event})+'\n');r['step']+=1
  if now>=r['next_sample']:
   r['next_sample']=now+.2
   a=json.loads(r['authority'].get_move_response_diagnostics());nets=[json.loads(sync.get_move_response_diagnostics()) for _,_,sync in r['clients']]
   r['file'].write(json.dumps({'t':now,'authority':a,'clients':nets},separators=(',',':'))+'\n');r['frames']+=1
  if now>=41:
   r['file'].close();unreal.unregister_slate_post_tick_callback(r['handle']);r['complete']=True
   Path('D:/UE5.7/test1/Artifacts/MassImmediate20260924/runtime-main-events.json').write_text(json.dumps({'events':r['events'],'samples':r['frames'],'duration':now},indent=2),encoding='utf-8')
 except Exception:
  error=traceback.format_exc();r['file'].close();unreal.unregister_slate_post_tick_callback(r['handle'])
  Path('D:/UE5.7/test1/Artifacts/MassImmediate20260924/runtime-error.txt').write_text(error,encoding='utf-8')
r['handle']=unreal.register_slate_post_tick_callback(tick)
print(json.dumps({'started':True,'soldiers':len(initial['soldiers']),'centers':centers}))
