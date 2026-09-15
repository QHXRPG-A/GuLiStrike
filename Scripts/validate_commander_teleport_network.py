"""Exercise a remote Red vehicle in Listen/Dedicated PIE, including a late joining observer."""
import json
import traceback
from pathlib import Path
import unreal


def worlds():
    return [w for w in unreal.ObjectIterator(unreal.World) if 'UEDPIE_' in w.get_path_name()]


def blue(actor):
    return any('M_TeleportBody' in str(c.get_material(i)) for c in actor.get_components_by_class(unreal.MeshComponent)
               for i in range(c.get_num_materials()))


def location(actor):
    p = actor.get_actor_location()
    return [p.x,p.y,p.z]


class NetworkRun:
    def __init__(self, vehicle='ship'):
        self.finished = False
        self.vehicle_class = unreal.GuLiStrikeShip if vehicle=='ship' else unreal.GuLiWarMachinePlaceholderPawn
        self.vehicle = vehicle
        self.perform_late_join = globals().get('TELEPORT_QA_LATE_JOIN', True)
        snapshots = [(w,json.loads(unreal.GuLiTeleportQALibrary.snapshot(w))) for w in worlds()]
        self.world,self.initial = next((w,s) for w,s in snapshots if s['net_mode'] in [1,2])
        self.mode = self.initial['net_mode']
        self.out = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))/globals().get('TELEPORT_QA_OUTPUT_ROOT','TestResults/CommanderTeleport')/((('dedicated' if self.mode==1 else 'listen')+'-'+vehicle)+'.json')
        self.out.parent.mkdir(parents=True, exist_ok=True)
        self.pc = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(self.world,unreal.PlayerController)
                       if p.player_state.is_commander() and p.player_state.get_team()==unreal.GuLiTeam.RED)
        self.input = self.pc.get_component_by_class(unreal.GuLiTeleportInputComponent)
        self.input.set_server_level(4)
        local = [p for w in worlds() for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.PlayerController)
                 if p.is_local_controller() and p.player_state and p.player_state.is_commander() and p.player_state.get_team()==unreal.GuLiTeam.RED]
        self.sender = local[0].get_component_by_class(unreal.GuLiTeleportInputComponent)
        unit = next(s for s in self.initial['units'] if s['team']==1 and s['type']==1 and s['health']>0)
        self.source = unreal.Vector(unit['x'],unit['y'],unit['z'])
        ships = [a for a in unreal.GameplayStatics.get_all_actors_of_class(self.world,self.vehicle_class)
                 if a.player_state and a.player_state.get_team()==unreal.GuLiTeam.RED]
        assert ships, 'A remote Red '+vehicle+' is required'
        self.ship = ships[0]
        self.player_slot = self.ship.player_state.get_battle_slot_index()
        self.group_before = next((g for g in self.initial.get('wingman_groups',[]) if g['player_slot']==self.player_slot),None) if vehicle=='ship' else None
        self.ship_id = self.group_before['ship'] if self.group_before else ''
        self.ship.set_actor_location(self.source+unreal.Vector(0,0,9000 if vehicle=='ship' else 200),False,True)
        self.report = {'mode':self.mode,'vehicle':vehicle,'passed':False,'checks':{},'stages':[],'errors':[],'source':location(self.ship)}
        self.original_pawn = self.ship.get_path_name()
        self.initial_worlds = {s['world'] for w,s in snapshots}
        self.start = unreal.GameplayStatics.get_time_seconds(self.world)
        self.stage = 0
        self.destination_offsets = [(-16000,-8000),(8000,0),(0,-8000),(0,8000),(-8000,0),(8000,-8000),(-8000,8000),(16000,0),(0,16000),(0,-16000)]
        self.destination_index = 0
        self.next_destination_attempt = 0
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def snapshot(self,label):
        data={'label':label,'worlds':[]}
        for w in worlds():
            s=json.loads(unreal.GuLiTeleportQALibrary.snapshot(w))
            fields=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiTeleportFieldActor)
            entries=[]
            for f in fields:
                state=f.get_cast_state()
                if True:
                    entries.append({'phase':str(state.phase),'count':state.participant_count,
                                    'cosmetics':len(f.get_components_by_class(unreal.DecalComponent))+len(f.get_components_by_class(unreal.StaticMeshComponent)),'message':state.message})
            vehicles=[]
            for a in unreal.GameplayStatics.get_all_actors_of_class(w,self.vehicle_class):
                if a.player_state and a.player_state.get_battle_slot_index()==self.player_slot:
                    vehicles.append({'name':a.get_name(),'location':location(a),'blue':blue(a),'hidden':a.get_editor_property('hidden'),'speed':a.get_velocity().length()})
            wingmen=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiWingmanPawn)
            cameras=[]
            for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.PlayerController):
                if p.is_local_controller() and p.player_state and isinstance(p.get_controlled_pawn(),self.vehicle_class) and p.player_state.get_battle_slot_index()==self.player_slot:
                    cameras.append({'same_target':p.get_view_target()==p.get_controlled_pawn(),'pawn':p.get_controlled_pawn().get_name()})
            data['worlds'].append({'world':s['world'],'mode':s['net_mode'],'phased_mass':sum(x['phased'] for x in s['units']),
                                   'fields':entries,'vehicles':vehicles,'wingmen':len(wingmen),'blue_wingmen':sum(blue(a) and not a.get_editor_property('hidden') for a in wingmen),'cameras':cameras,
                                   'group_pawns':[p for p in s.get('wingmen',[]) if p['ship']==self.ship_id],
                                   'group_state':next((g for g in s.get('wingman_groups',[]) if g['ship']==self.ship_id),None)})
        self.report['stages'].append(data)
        self.save()
        return data

    def tick(self,delta):
        try:
            elapsed=unreal.GameplayStatics.get_time_seconds(self.world)-self.start
            if self.stage==0 and elapsed>=2:
                self.snapshot('before')
                self.report['checks']['local_player_rpc_sent']=unreal.GuLiTeleportQALibrary.submit_intent(self.sender,unreal.GuLiTeleportCommand.SOURCE,unreal.Guid(),self.source)
                self.stage=1
            elif self.stage==1 and elapsed>=5.6:
                state=self.input.query_state()
                self.report['server_status']=str(self.input.get_status_text())
                self.report['sender_status']=str(self.sender.get_status_text())
                self.report['checks']['server_collected']=state.phase==unreal.GuLiTeleportPhase.AWAITING_DESTINATION and state.participant_count>=(26 if self.group_before else 2)
                phased=self.snapshot('phased')
                self.group_at_capture=next((w['group_state'] for w in phased['worlds'] if w['mode']!=3),None)
                if self.perform_late_join:
                    unreal.GuLiTeleportQALibrary.request_late_join()
                self.stage=2
            elif self.stage==2 and elapsed>=9:
                snapshot=self.snapshot('late_join')
                clients=[w for w in snapshot['worlds'] if w['mode']==3]
                joined=[w for w in clients if w['world'] not in self.initial_worlds]
                if self.perform_late_join:
                    self.report['checks']['late_join_reconstructs_field']=bool(joined) and all(any('AWAITING_DESTINATION' in f['phase'] for f in w['fields']) for w in joined)
                self.report['checks']['clients_show_blue_vehicle']=bool(clients) and all(w['vehicles'] and all(v['blue'] and not v['hidden'] for v in w['vehicles']) for w in clients)
                self.report['checks']['clients_show_phased_mass']=bool(clients) and all(w['phased_mass']>0 for w in clients)
                cameras=[c for w in snapshot['worlds'] for c in w['cameras']]
                self.report['checks']['camera_follows_while_blue']=bool(cameras) and all(c['same_target'] for c in cameras)
                if self.group_before:
                    owner_worlds=[w for w in clients if w['cameras']]
                    self.report['checks']['all_living_owned_wingmen_blue']=bool(owner_worlds) and all(len(w['group_pawns'])==self.group_before['living'] and all(p['phased'] for p in w['group_pawns']) for w in owner_worlds)
                    self.report['checks']['visible_remote_wingmen_blue']=all(p['phased'] for w in clients for p in w['group_pawns'] if p['visible'])
                if self.mode==1:
                    server_fields=[f for w in snapshot['worlds'] if w['mode']==1 for f in w['fields']]
                    self.report['checks']['dedicated_has_no_field_cosmetics']=bool(server_fields) and all(f['cosmetics']==0 for f in server_fields)
                self.stage=3
            elif self.stage==3:
                state=self.input.query_state()
                if state.phase==unreal.GuLiTeleportPhase.AWAITING_DESTINATION:
                    if elapsed>=self.next_destination_attempt and self.destination_index<len(self.destination_offsets):
                        offset=self.destination_offsets[self.destination_index]
                        self.report.setdefault('destination_attempts',[]).append({'offset':offset,'previous_rejection':str(state.message)})
                        self.destination_index+=1
                        self.next_destination_attempt=elapsed+.25
                        unreal.GuLiTeleportQALibrary.submit_intent(self.sender,unreal.GuLiTeleportCommand.DESTINATION,state.cast_id,self.source+unreal.Vector(*offset,0))
                    return
                assert state.phase!=unreal.GuLiTeleportPhase.RETURNING, 'No authored test destination could fit this group before timeout'
                if state.phase==unreal.GuLiTeleportPhase.RECOVERY:
                    return
                landed=self.snapshot('landed')
                if self.group_before:
                    group=next(w['group_state'] for w in landed['worlds'] if w['mode']!=3)
                    self.report['checks']['wingman_health_loadout_preserved']=all(group[k]==self.group_at_capture[k] for k in ['ship','living','health_hash','config_hash'])
                state=self.input.query_state()
                self.report['checks']['landing_finished']=state.phase in [unreal.GuLiTeleportPhase.FINISHED,unreal.GuLiTeleportPhase.IDLE]
                self.position=location(self.ship)
                self.landed_at=elapsed
                self.stage=4
            elif self.stage==4 and elapsed>=self.landed_at+2.7:
                result=self.snapshot('stable')
                self.report['checks']['no_server_pullback']=sum((x-y)**2 for x,y in zip(self.position,location(self.ship)))<100
                destination=self.ship.get_actor_location()
                self.report['checks']['displaced']=(destination.x-self.source.x)**2+(destination.y-self.source.y)**2>4000**2
                self.report['checks']['original_pawn_alive']=self.ship.get_path_name()==self.original_pawn and self.ship.player_state is not None
                self.report['checks']['camera_keeps_original_target']=all(c['same_target'] for w in result['worlds'] for c in w['cameras'])
                self.report['checks']['normal_appearance_restored']=all(not v['blue'] and not v['hidden'] for w in result['worlds'] for v in w['vehicles'])
                self.report['checks']['all_clients_at_destination']=all(abs(v['location'][0]-self.ship.get_actor_location().x)<100 for w in result['worlds'] for v in w['vehicles'])
                if self.group_before:
                    group=next(w['group_state'] for w in result['worlds'] if w['mode']!=3)
                    self.report['checks']['all_wingmen_migrated']=group['movement_writes']-self.group_before['movement_writes']==self.group_before['living']
                    self.report['checks']['wingman_actions_resumed']=not group['locked']
                    self.report['checks']['visible_wingmen_restored']=all(not p['phased'] for w in result['worlds'] for p in w['group_pawns'] if p['visible'])
                self.report['passed']=all(self.report['checks'].values())
                self.finish()
        except Exception:
            self.report['errors'].append(traceback.format_exc()); self.finish()

    def save(self):
        self.out.write_text(json.dumps(self.report,ensure_ascii=False,indent=2),encoding='utf-8')

    def finish(self):
        self.finished = True
        unreal.unregister_slate_post_tick_callback(self.handle)
        self.save()


teleport_network_run=NetworkRun(globals().get('TELEPORT_QA_VEHICLE','ship'))
print(json.dumps({'started':True,'output':str(teleport_network_run.out)}))
