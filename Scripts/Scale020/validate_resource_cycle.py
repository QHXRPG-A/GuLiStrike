"""Reuse the existing bounded mining acceptance with explicit 0.2x fixture expectations.

Only disposable PIE actors/orders are changed. No asset, map or settings save.
The original acceptance script and its historical reports remain untouched.
"""
import hashlib,json
from pathlib import Path
import unreal
root=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
source_path=root/'Scripts/validate_resource_world_pie.py'
source=source_path.read_text(encoding='utf-8')

def _peer_factory(world, authority):
    # Actor names are not a cross-client identity; static team/anchor pairs are unique here.
    matches=[f for f in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.GuLiResourceFactoryActor)
             if f.get_team()==authority.get_team()
             and (f.get_actor_location()-authority.get_actor_location()).length()<2.0]
    assert len(matches)==1,[(f.get_name(),str(f.get_actor_location())) for f in matches]
    return matches[0]

changes=[
 # This isolated editor intentionally disables live MCP; retain result logging locally.
 ('unreal.MCPythonHelper.submit_result', 'unreal.log'),
 ('TestResults/WORK-20260913-003/ResourcePIE/dual-client.json','TestResults/Scale020/ResourcePIE/dual-client.json'),
 ('2714f7bb7668d56638f0d7a4d752e194a9657996','5710cc3994205e33848d7d4051a80c2d6cd3b458'),
 # Current production adds a gifted factory at each initially owned territory.
 # Keep the exact count check, updating the obsolete pre-gift fixture from 2 to 4.
 ('row["factory_count"] == 2','row["factory_count"] == 4'),
 # Gameplay timers use server-world seconds, not shader/renderer wall-clock stalls.
 # The overall 600-second wall timeout is still enforced unchanged.
 ('self.ready_at = time.monotonic()', 'self.ready_at = unreal.GameplayStatics.get_time_seconds(_server_world())'),
 ('elapsed = time.monotonic() - self.ready_at',
  'elapsed = unreal.GameplayStatics.get_time_seconds(_server_world()) - self.ready_at'),
 # A team can now own multiple factories: observe the miner's actual assignment.
 ('self.factory = _find_by_team(_actors(self.server, FACTORY), self.miner.get_team())',
  'self.factory = unreal.GuLiScaleMigrationLibrary.read_mining_factory(self.miner)'),
 ('remote_factory=_find_by_team(_actors(world,FACTORY),self.miner.get_team())',
  'remote_factory=_peer_factory(world,self.factory)'),
 ('row.update(state=_enum(miner.get_task_state()),cargo=',
  'row.update(mode=_enum(miner.get_control_mode()),state=_enum(miner.get_task_state()),cargo='),
 ('factories=[_find_by_team(_actors(w,FACTORY),unreal.GuLiTeam.RED) for w in _worlds()]',
  'factories=[_peer_factory(w,self.factory) for w in _worlds()]'),
 ('self.parent.report["mining_cycle"] = {"cluster_id":self.cluster_id,',
  'self.parent.report["success"] = False\n        self.parent.report["mining_cycle"] = {"factory":self.factory.get_name(),"cluster_id":self.cluster_id,'),
 ('"door":float(factory.get_door_alpha()),"door_target":bool(factory.should_door_be_open()),"beams":[]}',
  '"door":float(factory.get_door_alpha()),"door_target":bool(factory.should_door_be_open()),"beams":[],\n'
  '            "door_collision":str(next(c for child in factory.get_components_by_class(unreal.ChildActorComponent)\n'
  '                if child.child_actor for c in child.child_actor.get_components_by_class(unreal.SkeletalMeshComponent)\n'
  '                if c.get_name()=="Door").get_collision_enabled())}'),
 ('laser_width_five_times','laser_width_scaled_020'),('outside_speed_tripled','outside_speed_scaled_020'),
 ('inside_speed_tripled','inside_speed_scaled_020'),('miner_range_is_5400_cm','miner_range_is_1080_cm'),
 ('["laser_asset_width_cm"]-25.0','["laser_asset_width_cm"]-5.0'),
 ('max_walk_speed - 4500','max_walk_speed - 900'),
 ('"factory_maneuver_speed_centimeters_per_second") - 1500','"factory_maneuver_speed_centimeters_per_second") - 300'),
 ('unreal.Vector(6500,600,0)','unreal.Vector(1300,120,0)'),
 ('get_mining_range() - 5400','get_mining_range() - 1080'),
 ('lo,hi = 0.0,10000.0','lo,hi = 0.0,2000.0'),('span(mid)<5400','span(mid)<1080'),
 ('unreal.Vector(6000,-6500,4000)','unreal.Vector(1200,-1300,800)'),
 ('unreal.Vector(-800,0,700)','unreal.Vector(-160,0,140)'),
 ('unreal.Vector(8000,2500,1900)','unreal.Vector(1600,500,380)'),
 ('"outputs/unit-data-mining"','"TestResults/Scale020/ResourcePIE"'),
 ('["scale"][0]*591.6596-1800','["scale"][0]*591.6596-360'),
 ('b["length"]<=5400.1','b["length"]<=1080.1'),
 ('row["local"][0]+900<1100 and abs(row["local"][1])+625<1800',
  'row["local"][0]+180<220 and abs(row["local"][1])+125<360'),
 ('unreal.Vector(6500,-600,0)','unreal.Vector(1300,-120,0)'),
 ('abs(row["local"][1]+600)<5','abs(row["local"][1]+120)<1'),
 ('row["local"][0]>3990','row["local"][0]>798'),
 ('self.end_target)<1600','self.end_target)<320'),
 # Arrival must belong to the deferred player move, not an automatic route that later crosses the target.
 ('self.checks["latest_command_destination"] = _length(self.miner.get_actor_location()-self.end_target)<320',
  'self.checks["latest_command_destination"] = _length(self.miner.get_actor_location()-self.end_target)<320 and (state=="PLAYER_MOVING" or _enum(self.miner.get_control_mode())=="GRACE")\n'
  '                self.parent.report["mining_cycle"]["deferred_destination"] = {"time":now,"state":state,"mode":_enum(self.miner.get_control_mode()),"distance_cm":_length(self.miner.get_actor_location()-self.end_target)}'),
 ('unreal.Vector(4000,-600,650)','unreal.Vector(800,-120,130)'),
 ('self.multi[1].get_actor_location())<20','self.multi[1].get_actor_location())<4'),
]
manifest={'version':1,'source':str(source_path),'sha256':hashlib.sha256(source.encode()).hexdigest(),'changes':[]}
for old,new in changes:
    assert old in source,old
    count=source.count(old);source=source.replace(old,new)
    manifest['changes'].append({'before':old,'target':new,'occurrences':count})
out=root/'TestResults/Scale020/ResourcePIE';out.mkdir(parents=True,exist_ok=True)
(out/'fixture-migration.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
exec(compile(source,str(source_path),'exec'),globals())

# Observe the presentation-only door policy without changing component settings.
_cycle_tick=MiningCycleAcceptance.tick
def _policy_tick(self):
    # The door is visual-only in production, on authority and remote clients.
    # Observe every phase (including final closed pose); do not change components here.
    door_rows=[]
    for world in _worlds():
        factory=_peer_factory(world,self.factory)
        door=next(c for child in factory.get_components_by_class(unreal.ChildActorComponent)
                  if child.child_actor for c in child.child_actor.get_components_by_class(unreal.SkeletalMeshComponent)
                  if c.get_name()=='Door')
        door_rows.append({'world':world.get_path_name(),'alpha':float(factory.get_door_alpha()),
                          'collision':str(door.get_collision_enabled()),
                          'overlap_events':bool(door.get_editor_property('generate_overlap_events'))})
    self.parent.report['mining_cycle']['door_policy_latest']=door_rows
    policy_key='visual_door_no_collision_or_overlaps_on_both_clients'
    self.checks[policy_key]=self.checks.get(policy_key,True) and len(door_rows)==2 and all(
        row['collision']==str(unreal.CollisionEnabled.NO_COLLISION) and not row['overlap_events'] for row in door_rows)
    _cycle_tick(self)
MiningCycleAcceptance.tick=_policy_tick
