import os,unreal,json,math,time
from pathlib import Path
assert os.getpid()==39244
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert 'UEDPIE_0' in world.get_path_name()
source=json.loads(Path('D:/UE5.7/test1/TestResults/AccessRampCrash-20260916/passive-soldier-scene.json').read_text())['result']['corner']
mesh=unreal.find_object(None,source['component']); point=mesh.get_instance_transform(source['index'],True).translation
original=unreal.Vector(*source['position'])
assert math.hypot(point.x-original.x,point.y-original.y)<1,'Selected soldier is not stationary'
def ground(x,y):
 h=unreal.SystemLibrary.line_trace_single_for_objects(world,unreal.Vector(x,y,50000),unreal.Vector(x,y,-50000),[unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1],False,[],unreal.DrawDebugTrace.NONE,True)
 assert h,'No ground for avoidance replay'
 return h.to_tuple()[5]
start=ground(point.x-18000,point.y)
goal=ground(point.x+18000,point.y)
cls=unreal.load_class(None,'/Script/GuLiStrike.GuLiConstructionVehiclePawn')
existing={a.get_path_name() for a in unreal.GameplayStatics.get_all_actors_of_class(world,cls)}
unreal.SystemLibrary.execute_console_command(world,f'guli.builder.Spawn blue {start.x} {start.y} {start.z}')
spawned=[a for a in unreal.GameplayStatics.get_all_actors_of_class(world,cls) if a.get_path_name() not in existing]
assert len(spawned)==1
builder=spawned[0]
assert builder.issue_move(goal),'MoveTo rejected'
following=builder.get_controller().get_component_by_class(unreal.load_class(None,'/Script/GuLiStrike.GuLiEngineeringCrowdFollowingComponent'))
manager=next(m for m in unreal.ObjectIterator(unreal.load_class(None,'/Script/GuLiStrike.GuLiGroundCrowdManager')) if 'UEDPIE_0' in m.get_path_name())
state={'success':True,'pid':os.getpid(),'builder':builder.get_path_name(),'obstacle':source,'start':[start.x,start.y,start.z],'goal':[goal.x,goal.y,goal.z],'samples':[]}
guli_detour_observation={'state':state,'started':time.perf_counter(),'last':0,'builder':builder,'mesh':mesh,'manager':manager,'following':following,'goal':goal,'point':original,'index':source['index'],'arrived_since':None}
def guli_observe_passive_detour(delta):
 d=guli_detour_observation; now=time.perf_counter()-d['started']
 if now-d['last']<0.05:return
 d['last']=now
 b=d['builder'];loc=b.get_actor_location();v=b.get_velocity();p=d['mesh'].get_instance_transform(d['index'],True).translation
 row={'seconds':now,'position':[loc.x,loc.y,loc.z],'speed':math.hypot(v.x,v.y),'distance_to_soldier':math.hypot(loc.x-p.x,loc.y-p.y),'soldier_drift':math.hypot(p.x-d['point'].x,p.y-d['point'].y),'remaining':math.hypot(loc.x-d['goal'].x,loc.y-d['goal'].y),'debug':d['following'].get_avoidance_debug(),'ground_obstacles':d['manager'].get_ground_obstacle_count()}
 d['state']['samples'].append(row)
 if row['remaining']<=500 and row['speed']<1:
  if d['arrived_since'] is None:d['arrived_since']=now
 if now>45 or (d['arrived_since'] is not None and now-d['arrived_since']>1):
  unreal.unregister_slate_post_tick_callback(d['handle'])
  d['state']['arrived']=d['arrived_since'] is not None
  Path('D:/UE5.7/test1/TestResults/AccessRampCrash-20260916/passive-mass-avoidance-after.json').write_text(json.dumps(d['state'],indent=2),encoding='utf-8')
guli_detour_observation['handle']=unreal.register_slate_post_tick_callback(guli_observe_passive_detour)
unreal.MCPythonHelper.submit_result(json.dumps({k:v for k,v in state.items() if k!='samples'}))
