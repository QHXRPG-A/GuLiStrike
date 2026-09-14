"""Run bounded two-client (listen server + remote client) resource PIE acceptance."""

from __future__ import annotations

import json
import math
import time
import traceback
from pathlib import Path

import unreal


OUTPUT = (
    Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    / "TestResults/WORK-20260913-003/ResourcePIE/dual-client.json"
)
EXPECTED_LAYOUT_HASH = "2714f7bb7668d56638f0d7a4d752e194a9657996"
TIMEOUT_SECONDS = 600.0
KEEP_EDITOR_OPEN = globals().get("KEEP_RESOURCE_EDITOR_OPEN", False)


def _class(path: str):
    value = unreal.load_class(None, path)
    if value is None:
        raise RuntimeError(f"Missing reflected class: {path}")
    return value


RESOURCE_SUBSYSTEM = _class("/Script/GuLiStrike.GuLiResourceWorldSubsystem")
WORLD_STATE = _class("/Script/GuLiStrike.GuLiResourceWorldState")
ORE_FIELD = _class("/Script/GuLiStrike.GuLiOreFieldActor")
OBSTACLE = _class("/Script/GuLiStrike.GuLiOreClusterObstacleActor")
OUTPOST = _class("/Script/GuLiStrike.GuLiTerritoryOutpostActor")
FACTORY = _class("/Script/GuLiStrike.GuLiResourceFactoryActor")
MINER_MANAGER = _class("/Script/GuLiStrike.GuLiMiningVehicleManager")
MINER = _class("/Script/GuLiStrike.GuLiMiningVehiclePawn")
PLAYER_STATE = _class("/Script/GuLiStrike.GuLiBattlePlayerState")
MASS_AUTHORITY = _class("/Script/GuLiStrike.GuLiBattleAuthoritySubsystem")


def _actors(world, actor_class):
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, actor_class))


def _subsystem(world):
    return next(
        (
            value
            for value in unreal.ObjectIterator(RESOURCE_SUBSYSTEM)
            if value.get_outer() == world
        ),
        None,
    )


def _mass_authority(world):
    return next(
        (
            value
            for value in unreal.ObjectIterator(MASS_AUTHORITY)
            if value.get_outer() == world
        ),
        None,
    )


def _enum(value):
    return str(value).split(".")[-1].split(":", 1)[0].strip("<> ").upper()


def _amounts(value):
    return {
        "blue": int(value.blue),
        "red": int(value.red),
    }


def _worlds():
    return sorted(
        [
            world
            for world in unreal.ObjectIterator(unreal.World)
            if "UEDPIE_" in world.get_path_name()
            and "LVL_CommanderMassPrototype" in world.get_path_name()
        ],
        key=lambda world: world.get_path_name(),
    )


def _is_server(world):
    return len(_actors(world, OBSTACLE)) > 0


def _world_snapshot(world):
    subsystem = _subsystem(world)
    mass_authority = _mass_authority(world)
    states = _actors(world, WORLD_STATE)
    fields = _actors(world, ORE_FIELD)
    obstacles = _actors(world, OBSTACLE)
    outposts = _actors(world, OUTPOST)
    factories = _actors(world, FACTORY)
    miners = _actors(world, MINER)
    players = _actors(world, PLAYER_STATE)
    local_player_states = set()
    for controller in _actors(world, unreal.PlayerController):
        try:
            if controller.is_local_controller() and controller.player_state:
                local_player_states.add(controller.player_state.get_name())
        except Exception:
            pass
    territory_owners = []
    if states:
        territory_owners = [_enum(states[0].get_territory_owner(index)) for index in range(25)]
    field_rows = []
    for field in fields:
        components = field.get_components_by_class(
            unreal.HierarchicalInstancedStaticMeshComponent
        )
        field_rows.append(
            {
                "hism_components": len(components),
                "visible_nodes": int(field.get_visible_node_count()),
                "component_instances": [int(component.get_instance_count()) for component in components],
            }
        )
    return {
        "world": world.get_path_name(),
        "server": _is_server(world),
        "runtime_ready": bool(subsystem and subsystem.is_runtime_ready()),
        "resource_active": bool(subsystem and subsystem.is_resource_world_active()),
        "fatal_error": bool(subsystem and subsystem.has_fatal_initialization_error()),
        "initialization_error": subsystem.get_initialization_error() if subsystem else "missing subsystem",
        "world_state_count": len(states),
        "authority_ready": bool(states and states[0].is_authority_ready()),
        "mass_population_spawned": bool(
            mass_authority and mass_authority.has_spawned_authority_population()
        ),
        "mass_authoritative_members": int(
            mass_authority.get_authoritative_member_count() if mass_authority else 0
        ),
        "cluster_remaining": [subsystem.get_cluster_remaining_raw(i+1) for i in range(240)] if subsystem else [],
        "layout_hash": states[0].get_layout_hash() if states else "",
        "territory_owners": territory_owners,
        "ore_fields": field_rows,
        "obstacle_count": len(obstacles),
        "enabled_obstacles": sum(1 for actor in obstacles if actor.is_obstacle_enabled()),
        "outpost_count": len(outposts),
        "factory_count": len(factories),
        "factories": [
            {
                "team": _enum(actor.get_team()),
                "queue": int(actor.get_queued_raw_amount()),
                "owner": actor.get_owner().get_name() if actor.get_owner() else None,
            }
            for actor in factories
        ],
        "miner_count": len(miners),
        "miners": [
            {
                "team": _enum(actor.get_team()),
                "cargo": _amounts(actor.get_cargo()),
                "mode": _enum(actor.get_control_mode()),
                "task": _enum(actor.get_task_state()),
                "target_cluster": int(actor.get_target_cluster_id()),
                "grace": float(actor.get_grace_seconds_remaining()),
                "owner": actor.get_owner().get_name() if actor.get_owner() else None,
                "location": [
                    float(actor.get_actor_location().x),
                    float(actor.get_actor_location().y),
                    float(actor.get_actor_location().z),
                ],
            }
            for actor in miners
        ],
        "players": [
            {
                "name": actor.get_name(),
                "local": actor.get_name() in local_player_states,
                "team": _enum(actor.get_team()),
                "role": _enum(actor.get_battle_role()),
                "sync_ready": bool(actor.is_sync_ready()),
                "inventory": _amounts(actor.get_resource_inventory()),
            }
            for actor in players
        ],
    }


def _snapshot_all():
    return [_world_snapshot(world) for world in _worlds()]


def _server_world():
    return next((world for world in _worlds() if _is_server(world)), None)


def _mine_result(value):
    if isinstance(value, tuple):
        return bool(value[0])
    return bool(value)


def _find_by_team(actors, team):
    expected = _enum(team)
    return next((actor for actor in actors if _enum(actor.get_team()) == expected), None)


def _vec(value):
    return [float(value.x), float(value.y), float(value.z)]


def _length(value):
    return math.sqrt(value.x * value.x + value.y * value.y + value.z * value.z)


class MiningCycleAcceptance:
    """The approved end-to-end addition, using the real vehicle and factory APIs."""
    def __init__(self, parent):
        self.parent = parent
        self.server = _server_world()
        self.miner = parent.red_miner
        self.resources = _subsystem(self.server)
        self.factory = _find_by_team(_actors(self.server, FACTORY), self.miner.get_team())
        self.definition = self.resources.get_map_definition()
        self.nodes = list(self.definition.get_editor_property("nodes"))
        self.state = self.resources.get_resource_world_state()
        self.stage = "cycle"
        self.last_sample = -1.0
        self.samples = []
        self.transitions = []
        self.node_changes = []
        self.previous_amounts = self.node_amounts()
        self.previous_cargo = sum(_amounts(self.miner.get_cargo()).values())
        self.previous_node = 0
        self.previous_state = ""
        self.dock_started = None
        self.uploaded_at = None
        self.pending_checked = False
        self.exit_at = None
        self.shots = set()
        self.checks = {}
        self.request_id = 2000
        self.max_cargo = 0
        self.matching_laser_since = None
        self.manager = next(v for v in unreal.ObjectIterator(MINER_MANAGER) if v.get_outer() == self.server)
        self.checks["laser_width_five_times"] = abs(parent.report["laser_asset_width_cm"]-25.0)<0.001
        self.checks["miner_uses_soldiers_row"] = self.miner.get_unit_type_id() == 3
        self.checks["outside_speed_tripled"] = abs(self.miner.character_movement.max_walk_speed - 4500) < 0.1
        self.checks["inside_speed_tripled"] = abs(self.resources.get_economy_config().get_editor_property("factory_maneuver_speed_centimeters_per_second") - 1500) < 0.1
        self.end_target = self.factory.get_actor_transform().transform_location(unreal.Vector(6500,600,0))
        clusters = [c for c in self.definition.get_editor_property("clusters")
            if self.resources.can_team_mine_at(self.miner.get_team(), c.get_editor_property("cluster_id"))
            and self.resources.get_cluster_remaining_raw(c.get_editor_property("cluster_id")) > 0]
        chosen = min(clusters, key=lambda c: _length(c.center - self.factory.get_actor_location()))
        self.cluster_id = int(chosen.get_editor_property("cluster_id"))
        accepted = self.miner.issue_player_command_by_value(self.request_id, unreal.GuLiMiningOrderType.MINE_CLUSTER,
            chosen.center, self.cluster_id, 1, self.miner.get_team())
        assert accepted, "Mining command rejected"
        self.checks["miner_range_is_5400_cm"] = abs(self.miner.get_mining_range() - 5400) < 0.1
        self.checks["owned_miner_keeps_door_open_while_away"] = self.factory.should_door_be_open() and self.factory.get_door_alpha() > 0.999
        self.parent.report["mining_cycle"] = {"cluster_id":self.cluster_id,"samples":self.samples,
            "transitions":self.transitions,"node_changes":self.node_changes,"checks":self.checks}
        self.parent.save()

    def node_amounts(self):
        return [self.resources.get_node_remaining_raw(i+1) for i in range(len(self.nodes))]

    def presentation(self, miner):
        return miner.get_components_by_class(unreal.ChildActorComponent)[0].child_actor

    def sample(self, miner, factory, server=False):
        child = self.presentation(miner)
        row = {"speed":_length(miner.get_velocity()),"unit_type":miner.get_unit_type_id(),"location":_vec(miner.get_actor_location()),
            "local":_vec(factory.get_actor_transform().inverse_transform_location(miner.get_actor_location())),
            "yaw":float(miner.get_actor_rotation().yaw),"laser":bool(miner.is_mining_laser_active()),
            "node":int(miner.get_mining_node_id()),"target":_vec(miner.get_mining_target()),
            "door":float(factory.get_door_alpha()),"door_target":bool(factory.should_door_be_open()),"beams":[]}
        if server:
            row.update(state=_enum(miner.get_task_state()),cargo=sum(_amounts(miner.get_cargo()).values()),
                queue=int(factory.get_queued_raw_amount()))
        if child:
            row["visual_class"] = child.get_class().get_path_name()
            row["scale"] = _vec(child.get_actor_scale3d())
            for beam in child.get_components_by_class(unreal.NiagaraComponent):
                end, found = beam.get_variable_position("User.Beam End")
                row["beams"].append({"active":bool(beam.is_active()),"visible":bool(beam.is_visible()),
                    "length":_length(end-beam.get_world_location()) if found else 0.0,
                    "end":_vec(end),"found":bool(found)})
        return row

    def check_range_boundary(self):
        # Measure the two muzzle rigs from the actual presentation and probe the server query at +/-1 cm.
        child = self.presentation(self.miner)
        components = {c.get_name():c for c in child.get_components_by_class(unreal.SceneComponent)}
        pose = self.miner.get_actor_transform()
        target = self.miner.get_mining_target()
        away = self.miner.get_actor_location() - target
        away.z = 0
        away = away / _length(away)
        pivots = [components["CollectorPivot_"+side].get_world_location() for side in ["L","R"]]
        lengths = [_length(components["LaserMuzzle_"+side].get_world_location()-pivots[i]) for i,side in enumerate(["L","R"])]
        def span(offset):
            return max(_length(target-(pivot+away*offset))-lengths[i] for i,pivot in enumerate(pivots))
        lo,hi = 0.0,10000.0
        for _ in range(30):
            mid=(lo+hi)*0.5
            if span(mid)<5400:lo=mid
            else:hi=mid
        origin=pose.translation
        pose.translation=origin+away*(lo-1)
        inside=self.miner.can_mine_target_from(target,pose)
        pose.translation=origin+away*(hi+1)
        outside=self.miner.can_mine_target_from(target,pose)
        self.parent.report["mining_cycle"]["range_boundary"]={"inside_span":span(lo-1),"outside_span":span(hi+1),"inside":inside,"outside":outside}
        self.checks["laser_range_boundary"] = bool(inside) and not bool(outside)

    def capture(self, phase):
        if phase.endswith("mining"):
            focus=self.miner.get_actor_location()+(self.miner.get_mining_target()-self.miner.get_actor_location())*0.3
            location=focus+unreal.Vector(6000,-6500,4000)
        else:
            focus=self.factory.get_actor_transform().transform_location(unreal.Vector(-800,0,700))
            location=self.factory.get_actor_transform().transform_location(unreal.Vector(8000,2500,1900))
        for world in _worlds():
            camera=next(a for a in _actors(world,unreal.CameraActor) if a.get_actor_label()=="MiningAcceptanceCamera")
            camera.set_actor_location(location,False,True)
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location,focus),True)
            camera.camera_component.set_field_of_view(55)
            for controller in _actors(world,unreal.PlayerController):
                if controller.is_local_controller():controller.set_view_target_with_blend(camera,0)
        path=Path(unreal.Paths.project_dir()).resolve()/"outputs/unit-data-mining"/(phase+".png")
        unreal.SystemLibrary.execute_console_command(self.server,'HighResShot 1600x1000 filename="'+path.as_posix()+'"')
        self.shots.add(phase)

    def tick(self):
        now=unreal.GameplayStatics.get_time_seconds(self.server)
        if self.stage=="door_close":
            factories=[_find_by_team(_actors(w,FACTORY),unreal.GuLiTeam.RED) for w in _worlds()]
            if all(f.get_door_alpha()<0.001 and not f.should_door_be_open() for f in factories):
                self.checks["no_assigned_miner_closes_door_on_both_clients"] = True
                self.complete()
            return
        if self.stage in ("multi_mining", "multi_factory", "one_remaining"):
            self.tick_multi(now)
            return
        row=self.sample(self.miner,self.factory,True)
        row["time"]=now
        state=row["state"]
        if state!=self.previous_state:
            self.transitions.append({"time":now,"state":state,"local":row["local"],"cargo":row["cargo"]})
            self.previous_state=state
            self.parent.save()
        amounts=self.node_amounts()
        changed=[i+1 for i,(a,b) in enumerate(zip(self.previous_amounts,amounts)) if a!=b and self.nodes[i].get_editor_property("cluster_id")==self.cluster_id]
        if row["cargo"]>self.previous_cargo:
            self.node_changes.append({"nodes":changed,"previous_laser_node":self.previous_node,"cargo_delta":row["cargo"]-self.previous_cargo,
                "ore_delta":sum(self.previous_amounts[i-1]-amounts[i-1] for i in changed)})
        self.previous_amounts=amounts
        self.previous_cargo=row["cargo"]
        self.previous_node=row["node"]
        self.max_cargo=max(self.max_cargo,row["cargo"])
        if row["laser"]:
            if "laser_range_boundary" not in self.checks:self.check_range_boundary()
            self.checks["preserved_vehicle_presentation"] = "BP_MiningVehicle_TransporterLvl2" in row.get("visual_class","") and abs(row["scale"][0]*591.6596-1800)<0.5
            self.checks["dual_green_beams_reach_actual_target"] = len(row["beams"])==2 and all(b["active"] and b["visible"] and b["found"] and b["length"]<=5400.1
                and _length(unreal.Vector(*b["end"])-unreal.Vector(*row["target"]))<1 for b in row["beams"])
            for world in _worlds():
                if world==self.server:continue
                remote=_find_by_team(_actors(world,MINER),self.miner.get_team())
                remote_factory=_find_by_team(_actors(world,FACTORY),self.miner.get_team())
                peer=self.sample(remote,remote_factory)
                if peer["laser"] and peer["node"]==row["node"] and len(peer["beams"])==2 and all(b["active"] for b in peer["beams"]):
                    self.checks["remote_client_laser_matches_target"] = True
                    if self.matching_laser_since is None:self.matching_laser_since=now
                    if now-self.matching_laser_since>0.5 and self.resources.get_node_remaining_raw(row["node"])>=2 and "mining" not in self.shots:self.capture("mining")
                else:self.matching_laser_since=None
        if state=="DOCKING" and self.dock_started is None:
            self.dock_started=now
            self.checks["whole_vehicle_inside_at_unload"] = row["local"][0]+900<1100 and abs(row["local"][1])+625<1800 and not row["laser"]
            self.checks["cargo_retained_until_inside"] = row["cargo"]==10
            if "inside" not in self.shots:self.capture("inside")
            for request,target in [(2001,self.factory.get_actor_transform().transform_location(unreal.Vector(6500,-600,0))),(2002,self.end_target)]:
                assert self.miner.issue_player_command_by_value(request,unreal.GuLiMiningOrderType.MOVE,target,0,1,self.miner.get_team())
            self.pending_checked=True
            self.checks["new_commands_deferred_while_inside"] = _enum(self.miner.get_task_state())=="DOCKING"
        if state=="EXITING_FACTORY" and self.uploaded_at is None:
            self.uploaded_at=now
            self.checks["factory_turn_is_instant"] = abs(row["local"][1]+600)<5 and abs(unreal.MathLibrary.normalized_delta_rotator(self.miner.get_actor_rotation(),self.factory.get_actor_rotation()).yaw)<1 and all(s["state"]!="TURNING_IN_FACTORY" for s in self.transitions)
            self.capture("turned")
        if now-self.last_sample>=0.2:
            self.samples.append(row)
            self.last_sample=now
            if len(self.samples)%20==0:self.parent.save()
        if self.pending_checked and not self.miner.is_factory_maneuver_active():
            if self.exit_at is None:
                self.exit_at=now
                self.checks["front_facing_exit"] = row["local"][0]>3990 and abs(row["local"][1]+600)<5 and abs(unreal.MathLibrary.normalized_delta_rotator(self.miner.get_actor_rotation(),self.factory.get_actor_rotation()).yaw)<1
                self.checks["unload_waits_one_second"] = self.uploaded_at is not None and 0.98<=self.uploaded_at-self.dock_started<=1.15
                self.checks["latest_command_runs_after_exit"] = state=="PLAYER_MOVING"
                self.checks["exact_cargo_uploaded"] = self.max_cargo==10 and row["cargo"]==0
                self.checks["laser_consumes_its_target_node"] = bool(self.node_changes) and all(c["previous_laser_node"] in c["nodes"] and c["cargo_delta"]==c["ore_delta"] for c in self.node_changes)
                self.capture("exited")
            if _length(self.miner.get_actor_location()-self.end_target)<1600 and now-self.exit_at>1.0:
                self.checks["latest_command_destination"] = _length(self.miner.get_actor_location()-self.end_target)<1600
                self.start_multi()
                self.parent.save()

    def start_multi(self):
        self.extra = self.manager.spawn_mining_vehicle(self.factory, self.miner.get_actor_transform())
        assert self.extra, "Manager did not spawn from Soldiers"
        self.multi = [self.miner, self.extra]
        self.multi_held = set()
        self.multi_rows = []
        self.multi_docked = {}
        self.multi_uploaded = {}
        self.multi_exited = set()
        self.checks["multi_node_assignments_unique"] = True
        clusters = [c for c in self.definition.get_editor_property("clusters")
            if self.resources.can_team_mine_at(self.miner.get_team(), c.get_editor_property("cluster_id"))
            and self.resources.get_cluster_remaining_raw(c.get_editor_property("cluster_id"))>=20]
        chosen=min(clusters,key=lambda c:_length(c.center-self.miner.get_actor_location()))
        self.multi_cluster=int(chosen.get_editor_property("cluster_id"))
        for miner in self.multi:
            assert miner.issue_player_command_by_value(3000,unreal.GuLiMiningOrderType.MINE_CLUSTER,chosen.center,self.multi_cluster,1,miner.get_team())
        self.stage="multi_mining"
        self.parent.report["multi_mining"]={"cluster":self.multi_cluster,"samples":self.multi_rows}

    def tick_multi(self, now):
        if self.stage=="one_remaining":
            if now-self.one_remaining_since>3.2:
                factories=[_find_by_team(_actors(w,FACTORY),unreal.GuLiTeam.RED) for w in _worlds()]
                self.checks["remaining_miner_keeps_door_open"] = all(f.should_door_be_open() and f.get_door_alpha()>0.999 for f in factories)
                self.extra.destroy_actor()
                self.stage="door_close"
            return
        rows=[self.sample(m,self.factory,True) for m in self.multi]
        for i,row in enumerate(rows):
            row["assigned_node"]=self.manager.get_assigned_node(self.multi[i])
            row["name"]=self.multi[i].get_name()
        if now-self.last_sample>0.2:
            self.multi_rows.append({"time":now,"stage":self.stage,"miners":rows})
            self.last_sample=now
            if len(self.multi_rows)%20==0:self.parent.save()
        nodes=[r["assigned_node"] for r in rows if r["assigned_node"]]
        self.checks["multi_node_assignments_unique"] &= len(nodes)==len(set(nodes))
        if all(r["laser"] for r in rows):
            if "multi-mining" not in self.shots and all(self.resources.get_node_remaining_raw(r["node"])>=2 for r in rows):self.capture("multi-mining")
            self.checks["two_miners_mine_same_cluster"]=all(m.get_target_cluster_id()==self.multi_cluster for m in self.multi)
            for world in _worlds():
                if world==self.server:continue
                peers=[m for m in _actors(world,MINER) if _enum(m.get_team())=="RED"]
                if len(peers)==2 and all(p.get_unit_type_id()==3 and p.is_mining_laser_active() for p in peers):
                    self.checks["remote_multi_miners_synced"]=len(set(p.get_mining_node_id() for p in peers))==2
        if self.stage=="multi_mining":
            for i,row in enumerate(rows):
                if row["cargo"]==10 and i not in self.multi_held:
                    miner=self.multi[i]
                    assert miner.issue_player_command_by_value(3001,unreal.GuLiMiningOrderType.CANCEL,unreal.Vector(),0,1,miner.get_team())
                    miner.set_actor_tick_enabled(False)
                    self.multi_held.add(i)
            if len(self.multi_held)==2:
                self.multi_balance=sum(_amounts(self.resources.get_team_inventory(self.miner.get_team())).values())+self.factory.get_queued_raw_amount()
                self.stage="multi_factory"
                entry=self.factory.get_actor_transform().transform_location(unreal.Vector(4000,-600,650))
                for miner in self.multi:
                    miner.set_actor_location(entry,False,True)
                    miner.set_actor_rotation(unreal.Rotator(yaw=self.factory.get_actor_rotation().yaw+180),True)
                    assert miner.issue_player_command_by_value(3002,unreal.GuLiMiningOrderType.RETURN_TO_FACTORY,unreal.Vector(),0,1,miner.get_team())
                    miner.set_actor_tick_enabled(True)
        else:
            if all(r["state"]=="ENTERING_FACTORY" for r in rows):
                self.checks["simultaneous_factory_entry"]=True
                self.checks["miners_overlap_without_collision"]=_length(self.multi[0].get_actor_location()-self.multi[1].get_actor_location())<20
            for i,row in enumerate(rows):
                if row["state"]=="DOCKING" and i not in self.multi_docked:
                    self.multi_docked[i]=now
                if row["state"]=="EXITING_FACTORY" and i not in self.multi_uploaded:
                    self.multi_uploaded[i]=now
                if i in self.multi_uploaded and not self.multi[i].is_factory_maneuver_active():
                    self.multi_exited.add(i)
            if len(self.multi_exited)==2:
                self.checks["per_vehicle_one_second_upload"]=all(0.98<=self.multi_uploaded[i]-self.multi_docked[i]<=1.15 for i in range(2))
                balance=sum(_amounts(self.resources.get_team_inventory(self.miner.get_team())).values())+self.factory.get_queued_raw_amount()
                self.checks["two_cargos_uploaded_once"]=balance-self.multi_balance==20 and all(r["cargo"]==0 for r in rows)
                self.capture("multi-exit")
                self.miner.destroy_actor()
                self.stage="one_remaining"
                self.one_remaining_since=now
                self.parent.save()

    def complete(self):
        self.parent.report["checks"].update(self.checks)
        required=["miner_range_is_5400_cm","laser_range_boundary","preserved_vehicle_presentation","dual_green_beams_reach_actual_target",
            "remote_client_laser_matches_target","whole_vehicle_inside_at_unload","cargo_retained_until_inside","unload_waits_one_second",
            "front_facing_exit","new_commands_deferred_while_inside","latest_command_runs_after_exit","latest_command_destination",
            "miner_uses_soldiers_row","outside_speed_tripled","inside_speed_tripled","factory_turn_is_instant",
            "multi_node_assignments_unique","two_miners_mine_same_cluster","miners_overlap_without_collision",
            "simultaneous_factory_entry","per_vehicle_one_second_upload","two_cargos_uploaded_once",
            "remaining_miner_keeps_door_open","remote_multi_miners_synced","laser_width_five_times",
            "exact_cargo_uploaded","laser_consumes_its_target_node","owned_miner_keeps_door_open_while_away","no_assigned_miner_closes_door_on_both_clients"]
        for name in required:self.parent.report["checks"].setdefault(name,False)
        self.parent.report["failed_checks"]=[k for k,v in self.parent.report["checks"].items() if not v]
        self.parent.report["success"]=not self.parent.report["failed_checks"]
        self.parent.finish()


class ResourcePIEAcceptance:
    def __init__(self):
        self.started = time.monotonic()
        self.ready_at = None
        self.stage = "waiting"
        self.red_miner = None
        self.cluster_id = 0
        self.obstacle = None
        self.report = {
            "schema": "guli.resource-world.pie-acceptance.v1",
            "success": False,
            "expected_layout_hash": EXPECTED_LAYOUT_HASH,
            "play_settings": {},
            "checks": {},
            "errors": [],
        }
        self.handle = unreal.register_slate_post_tick_callback(self.tick)
        self.shutdown_handle = None

    def save(self):
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        OUTPUT.write_text(
            json.dumps(self.report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    def fail(self):
        self.report["success"] = False
        self.finish()

    def finish(self):
        if self.handle is not None:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None
        self.save()
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
        if not KEEP_EDITOR_OPEN:
            self.shutdown_handle = unreal.register_slate_post_tick_callback(self.quit_after_pie_has_ended)

    def quit_after_pie_has_ended(self, _delta_seconds):
        level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if level.is_in_play_in_editor():
            return
        unreal.unregister_slate_post_tick_callback(self.shutdown_handle)
        self.shutdown_handle = None
        unreal.SystemLibrary.quit_editor()

    def begin_acceptance(self):
        worlds = _worlds()
        if len(worlds) < 2:
            return False
        initial = _snapshot_all()
        if not all(
            row["runtime_ready"]
            and row["authority_ready"]
            and not row["fatal_error"]
            and row["layout_hash"] == EXPECTED_LAYOUT_HASH
            and len(row["ore_fields"]) == 1
            and row["ore_fields"][0]["hism_components"] == 24
            and row["ore_fields"][0]["visible_nodes"] == 6240
            for row in initial
        ):
            return False
        if not any(
            row["server"]
            and row["mass_population_spawned"]
            and row["mass_authoritative_members"] == 500
            for row in initial
        ):
            return False
        server = _server_world()
        subsystem = _subsystem(server) if server else None
        miners = _actors(server, MINER) if server else []
        if not subsystem or len(miners) != 2:
            raise RuntimeError("Dedicated server resource actors are incomplete")
        self.red_miner = next(
            (actor for actor in miners if "RED" in _enum(actor.get_team())), None
        )
        blue_miner = next(
            (actor for actor in miners if "BLUE" in _enum(actor.get_team())), None
        )
        if not self.red_miner or not blue_miner:
            raise RuntimeError("Red/Blue miners were not both spawned")

        cancel_type = unreal.GuLiMiningOrderType.CANCEL
        red_cancel = self.red_miner.issue_player_command_by_value(
            1001, cancel_type, unreal.Vector(), 0, 1, self.red_miner.get_team()
        )
        blue_cancel = blue_miner.issue_player_command_by_value(
            1001, cancel_type, unreal.Vector(), 0, 1, blue_miner.get_team()
        )
        if not red_cancel or not blue_cancel:
            raise RuntimeError("Could not put both miners into bounded Grace for setup")

        for candidate in range(1, 241):
            if subsystem.can_team_mine_at(self.red_miner.get_team(), candidate):
                self.cluster_id = candidate
                break
        if not self.cluster_id:
            raise RuntimeError("No initially mineable red-home cluster was found")
        self.obstacle = next(
            (
                actor
                for actor in _actors(server, OBSTACLE)
                if actor.get_cluster_id() == self.cluster_id
            ),
            None,
        )
        if not self.obstacle:
            raise RuntimeError("Selected cluster has no server obstacle")

        self.report["initial"] = initial
        self.report["exercise"] = {
            "cluster_id": self.cluster_id,
            "cancel_accepted": {"red": bool(red_cancel), "blue": bool(blue_cancel)},
            "before_depletion": {
                "remaining": int(subsystem.get_cluster_remaining_raw(self.cluster_id)),
                "obstacle_enabled": bool(self.obstacle.is_obstacle_enabled()),
            },
        }
        successful_mines = 0
        for _ in range(39):
            successful_mines += int(
                _mine_result(subsystem.mine_one_raw(self.red_miner.get_team(), self.cluster_id))
            )
        field = _actors(server, ORE_FIELD)[0]
        self.report["exercise"]["after_39"] = {
            "successful_mines": successful_mines,
            "remaining": int(subsystem.get_cluster_remaining_raw(self.cluster_id)),
            "obstacle_enabled": bool(self.obstacle.is_obstacle_enabled()),
            "visible_nodes": int(field.get_visible_node_count()),
        }
        final_mine = _mine_result(
            subsystem.mine_one_raw(self.red_miner.get_team(), self.cluster_id)
        )
        self.report["exercise"]["after_40"] = {
            "mine_accepted": final_mine,
            "remaining": int(subsystem.get_cluster_remaining_raw(self.cluster_id)),
            "obstacle_enabled": bool(self.obstacle.is_obstacle_enabled()),
            "visible_nodes": int(field.get_visible_node_count()),
            "enabled_obstacles": sum(
                1 for actor in _actors(server, OBSTACLE) if actor.is_obstacle_enabled()
            ),
        }

        red_factory = _find_by_team(_actors(server, FACTORY), self.red_miner.get_team())
        self.report["exercise"]["factory_enqueue_10"] = bool(
            red_factory and red_factory.enqueue_raw(unreal.GuLiResourceType.BLUE, 10)
        )
        self.ready_at = time.monotonic()
        self.stage = "invalid"
        return True

    def evaluate(self):
        final = _snapshot_all()
        self.report["final"] = final
        checks = self.report["checks"]
        initial = self.report["initial"]
        exercise = self.report["exercise"]
        checks["two_client_worlds"] = len(final) == 2
        checks["listen_server_plus_remote_client"] = (
            sum(1 for row in final if row["server"]) == 1
            and sum(1 for row in final if not row["server"]) == 1
        )
        checks["all_worlds_ready_same_layout"] = all(
            row["runtime_ready"]
            and row["authority_ready"]
            and not row["fatal_error"]
            and row["layout_hash"] == EXPECTED_LAYOUT_HASH
            for row in final
        )
        checks["mass_spawns_after_resource_navigation_ready"] = any(
            row["server"]
            and row["mass_population_spawned"]
            and row["mass_authoritative_members"] == 500
            for row in initial
        )
        checks["initial_actor_counts"] = all(
            row["world_state_count"] == 1
            and row["outpost_count"] == 25
            and row["factory_count"] == 2
            and row["miner_count"] == 2
            and len(row["ore_fields"]) == 1
            and row["ore_fields"][0]["hism_components"] == 24
            and (row["obstacle_count"] == 240 if row["server"] else row["obstacle_count"] == 0)
            for row in initial
        )
        owners = [row["territory_owners"] for row in final]
        checks["public_territory_owners_match"] = bool(owners) and all(
            value == owners[0] for value in owners[1:]
        )
        checks["one_red_one_blue_23_neutral"] = bool(owners) and (
            sum("RED" in value for value in owners[0]) == 1
            and sum("BLUE" in value for value in owners[0]) == 1
            and sum("UNASSIGNED" in value or "NEUTRAL" in value for value in owners[0]) == 23
        )
        checks["39_raw_keep_obstacle"] = (
            exercise["after_39"]["successful_mines"] == 39
            and exercise["after_39"]["remaining"] == 1
            and exercise["after_39"]["obstacle_enabled"]
            and exercise["after_39"]["visible_nodes"] == 6215
        )
        checks["last_node_opens_cluster"] = (
            exercise["after_40"]["mine_accepted"]
            and exercise["after_40"]["remaining"] == 0
            and not exercise["after_40"]["obstacle_enabled"]
            and exercise["after_40"]["visible_nodes"] == 6214
            and exercise["after_40"]["enabled_obstacles"] == 239
        )
        checks["ore_delta_reaches_both_clients"] = all(
            row["cluster_remaining"][exercise["cluster_id"]-1] == 0 for row in final
        )
        checks["invalid_order_does_not_extend_grace"] = (
            not exercise["invalid_order"]["accepted"]
            and exercise["invalid_order"]["grace_after"]
            <= exercise["invalid_order"]["grace_before"]
        )
        checks["new_valid_order_overrides_and_resets_grace"] = (
            exercise["valid_override"]["accepted"]
            and exercise["valid_override"]["grace_after"]
            > exercise["valid_override"]["grace_before"] + 0.4
        )
        server_row = next(row for row in final if row["server"])
        red_server_miner = next(row for row in server_row["miners"] if "RED" in row["team"])
        checks["three_seconds_returns_to_auto"] = red_server_miner["mode"] == "AUTO"
        checks["factory_fifo_processed_10"] = (
            exercise["factory_enqueue_10"]
            and next(row for row in server_row["factories"] if "RED" in row["team"])["queue"] == 0
            and next(row for row in server_row["players"] if "RED" in row["team"])["inventory"]["blue"] >= 50
        )
        client_rows = [row for row in final if not row["server"]]
        privacy_ok = True
        for row in client_rows:
            locals_ = [player for player in row["players"] if player["local"]]
            remotes = [player for player in row["players"] if not player["local"]]
            privacy_ok &= len(locals_) == 1 and locals_[0]["role"] == "COMMANDER"
            privacy_ok &= locals_[0]["inventory"]["blue"] >= 40
            privacy_ok &= all(
                player["inventory"]["blue"] == 0 and player["inventory"]["red"] == 0
                for player in remotes
            )
        checks["owner_only_inventory"] = bool(client_rows) and privacy_ok
        checks["mid_owner_only_factory_queue"] = self._check_mid_queue_privacy()
        self.report["failed_checks"] = [name for name, passed in checks.items() if not passed]
        self.report["success"] = not self.report["failed_checks"]

    def _check_mid_queue_privacy(self):
        client_rows = [row for row in self.report.get("mid", []) if not row["server"]]
        if len(client_rows) != 1:
            return False
        remote_client = client_rows[0]
        server_row = next(
            (row for row in self.report.get("mid", []) if row["server"]), None
        )
        if not server_row:
            return False
        server_visible = next(
            row for row in server_row["factories"] if "RED" in row["team"]
        )["queue"]
        remote_hidden = next(
            row for row in remote_client["factories"] if "RED" in row["team"]
        )["queue"]
        return server_visible > 0 and remote_hidden == 0

    def tick(self, _delta):
        try:
            if time.monotonic() - self.started > TIMEOUT_SECONDS:
                raise TimeoutError(f"Resource PIE did not finish within {TIMEOUT_SECONDS} seconds")
            if self.stage == "cycle":
                self.cycle.tick()
                return
            if self.stage == "waiting":
                self.begin_acceptance()
                return
            elapsed = time.monotonic() - self.ready_at
            if self.stage == "invalid" and elapsed >= 0.35:
                grace_before = self.red_miner.get_grace_seconds_remaining()
                accepted = self.red_miner.issue_player_command_by_value(
                    1002,
                    unreal.GuLiMiningOrderType.MOVE,
                    unreal.Vector(400001.0, 0.0, 0.0),
                    0,
                    1,
                    self.red_miner.get_team(),
                )
                self.report["exercise"]["invalid_order"] = {
                    "accepted": bool(accepted),
                    "grace_before": float(grace_before),
                    "grace_after": float(self.red_miner.get_grace_seconds_remaining()),
                }
                self.stage = "override"
            if self.stage == "override" and elapsed >= 0.9:
                grace_before = self.red_miner.get_grace_seconds_remaining()
                accepted = self.red_miner.issue_player_command_by_value(
                    1003,
                    unreal.GuLiMiningOrderType.CANCEL,
                    unreal.Vector(),
                    0,
                    1,
                    self.red_miner.get_team(),
                )
                self.report["exercise"]["valid_override"] = {
                    "accepted": bool(accepted),
                    "grace_before": float(grace_before),
                    "grace_after": float(self.red_miner.get_grace_seconds_remaining()),
                }
                self.stage = "mid"
            if self.stage == "mid" and elapsed >= 1.8:
                self.report["mid"] = _snapshot_all()
                self.stage = "final"
            if self.stage == "final" and elapsed >= 8.0:
                self.evaluate()
                if not self.report["success"]:
                    self.finish()
                    return
                self.stage="cycle"
                self.cycle=MiningCycleAcceptance(self)
        except Exception:
            self.report["errors"].append(traceback.format_exc())
            self.fail()


def _configure_and_start():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Maps/LVL_CommanderMassPrototype")
    configured = unreal.GuLiResourceAuthoringLibrary.configure_listen_server_pie(2)
    if not configured:
        raise RuntimeError("Could not configure listen-server PIE")
    applied = {
        "PlayNumberOfClients": "2",
        "RunUnderOneProcess": "True",
        "bLaunchSeparateServer": "False",
        "PlayNetMode": "PIE_ListenServer",
    }
    performance = unreal.get_default_object(
        unreal.load_class(None, "/Script/UnrealEd.EditorPerformanceSettings")
    )
    performance.set_editor_property("bThrottleCPUWhenNotForeground", False)
    editor_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    for command in ("t.MaxFPS 0", "t.IdleWhenNotForeground 0", "Slate.bAllowThrottling 0"):
        unreal.SystemLibrary.execute_console_command(editor_world, command)
    camera=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.CameraActor,unreal.Vector(0,0,1000),unreal.Rotator())
    camera.set_actor_label("MiningAcceptanceCamera")
    runner = ResourcePIEAcceptance()
    runner.report["play_settings"] = applied
    settings=unreal.NiagaraService.get_all_editable_settings("/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Green")
    runner.report["laser_asset_width_cm"]=next(float(p.current_value) for p in settings.rapid_iteration_parameters if p.setting_path=="Constants.Beam.BeamWidth.Beam Width")
    runner.save()
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
    return runner


try:
    resource_pie_acceptance = _configure_and_start()
    unreal.MCPythonHelper.submit_result(
        json.dumps({"success": True, "started": True, "report": str(OUTPUT)})
    )
except Exception:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(
        json.dumps(
            {
                "schema": "guli.resource-world.pie-acceptance.v1",
                "success": False,
                "checks": {},
                "errors": [traceback.format_exc()],
            },
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )
    unreal.MCPythonHelper.submit_result(
        json.dumps({"success": False, "started": False, "report": str(OUTPUT)})
    )
    unreal.SystemLibrary.quit_editor()
