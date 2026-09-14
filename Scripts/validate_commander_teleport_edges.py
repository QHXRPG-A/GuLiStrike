"""PIE authority fixtures for tier/height boundaries and abnormal lifetime cleanup."""
import json
import traceback
import re
from pathlib import Path
import unreal


class TeleportEdgeRun:
    def __init__(self):
        self.world=next(w for w in unreal.ObjectIterator(unreal.World) if 'UEDPIE_' in w.get_path_name()
                        and json.loads(unreal.GuLiTeleportQALibrary.snapshot(w))['net_mode']!=3)
        self.out=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))/'TestResults/CommanderTeleport'/globals().get('TELEPORT_EDGE_REPORT','edges.json')
        self.cases=globals().get('TELEPORT_EDGE_CASES',['tier_1','tier_2','tier_3','height_exact','height_above','freeze_duplicate','empty','participant_destroy','role_loss','disconnect'])
        self.report={'passed':False,'cases':[],'errors':[]}
        self.index=-1; self.finished=False
        self.handle=unreal.register_slate_post_tick_callback(self.tick)
        self.next_case()

    def snapshot(self):
        return json.loads(unreal.GuLiTeleportQALibrary.snapshot(self.world))

    def now(self):
        return unreal.GameplayStatics.get_time_seconds(self.world)

    def next_case(self):
        self.index+=1
        if self.index==len(self.cases):
            self.report['passed']=all(c['passed'] for c in self.report['cases']); self.finish(); return
        self.name=self.cases[self.index]
        team=unreal.GuLiTeam.BLUE if self.name=='disconnect' else unreal.GuLiTeam.RED
        self.pc=next(p for p in unreal.GameplayStatics.get_all_actors_of_class(self.world,unreal.PlayerController)
                     if p.player_state and p.player_state.is_commander() and p.player_state.get_team()==team)
        self.input=self.pc.get_component_by_class(unreal.GuLiTeleportInputComponent)
        self.level=int(self.name[-1]) if self.name.startswith('tier_') else (1 if self.name in ['freeze_duplicate','empty','role_loss','disconnect','match_end'] else 4)
        self.input.set_server_level(self.level)
        self.before=self.snapshot()
        units=[u for u in self.before['units'] if u['health']>0 and u['type']==1 and u['team']==(2 if team==unreal.GuLiTeam.BLUE else 1)]
        unit=units[0]
        if self.name=='empty':
            unit=next(u for u in self.before['units'] if u['health']>0 and u['team']==2)
        self.source=unreal.Vector(unit['x'],unit['y'],unit['z'])
        self.ship=next((a for a in unreal.GameplayStatics.get_all_actors_of_class(self.world,unreal.GuLiStrikeShip)
                        if a.player_state and a.player_state.get_team()==team),None)
        self.case={'name':self.name,'checks':{},'passed':False}
        self.report['cases'].append(self.case)
        if self.name.startswith('tier_') or self.name.startswith('height_') or self.name=='participant_destroy':
            assert self.ship, 'Ship fixture is required'
            hit=unreal.SystemLibrary.line_trace_single_for_objects(self.world,self.source+unreal.Vector(0,0,20000),self.source-unreal.Vector(0,0,30000),
                  [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1],False,unreal.GameplayStatics.get_all_actors_of_class(self.world,unreal.Pawn),unreal.DrawDebugTrace.NONE)
            assert hit, 'Physical terrain is required'
            clearance=10000.1 if self.name=='height_above' else (10000 if self.name=='height_exact' else 9000)
            xyz=re.search(r'ImpactPoint=\(X=([^,]+),Y=([^,]+),Z=([^\)]+)\)',hit.export_text()).groups()
            self.ground=unreal.Vector(*[float(v) for v in xyz])
            self.ship.set_actor_location(self.ground+unreal.Vector(0,0,clearance),False,True)
            self.case['requested_clearance']=clearance
        self.started=self.now(); self.stage=0; self.cast=None
        self.save()

    def state(self):
        fields=unreal.GameplayStatics.get_all_actors_of_class(self.world,unreal.GuLiTeleportFieldActor)
        return next((f.get_cast_state() for f in fields if self.cast and f.get_cast_state().cast_id.to_string()==self.cast.to_string()),unreal.GuLiTeleportCastState())

    def tick(self,delta):
        try:
            elapsed=self.now()-self.started
            if self.stage==0 and elapsed>=1:
                self.input.server_submit(unreal.GuLiTeleportCommand.SOURCE,unreal.Guid(),self.source)
                s=self.input.query_state(); self.cast=s.cast_id
                assert s.phase==unreal.GuLiTeleportPhase.WINDUP, str(self.input.get_status_text())
                self.case['checks']['source_accepted']=True
                self.source_time=self.now(); self.stage=1
                if self.name=='freeze_duplicate': self.input.set_server_level(4)
            elif self.stage==1 and self.now()-self.source_time>=.2:
                if self.name=='freeze_duplicate':
                    self.input.server_submit(unreal.GuLiTeleportCommand.SOURCE,unreal.Guid(),self.source)
                    s=self.input.query_state()
                    self.case['checks']['duplicate_keeps_cast']=s.cast_id.to_string()==self.cast.to_string()
                    self.case['checks']['configuration_frozen']=s.config.level==1 and s.config.radius_centimeters==1000
                self.stage=2
            elif self.stage==2 and self.now()-self.source_time>=3.2:
                s=self.state(); snapshot=self.snapshot()
                self.captured=[u for u in snapshot['units'] if u['phased']]
                self.case['collected']=s.participant_count
                if self.name=='empty':
                    self.case['checks']['empty_ends']=s.phase in [unreal.GuLiTeleportPhase.FINISHED,unreal.GuLiTeleportPhase.IDLE] and not self.captured
                else:
                    self.case['checks']['mass_collected']=bool(self.captured) and s.phase==unreal.GuLiTeleportPhase.AWAITING_DESTINATION
                    if self.name.startswith('tier_') or self.name.startswith('height_'):
                        included=not self.ship.get_actor_enable_collision()
                        self.case['checks']['vehicle_eligibility']=included==(self.name=='height_exact')
                        self.case['actual_clearance']=self.ship.get_actor_location().z-self.ground.z
                    if self.name=='participant_destroy':
                        self.case['checks']['vehicle_collected_before_destroy']=not self.ship.get_actor_enable_collision()
                        self.ship.destroy_actor()
                    if self.name=='role_loss':
                        self.case['checks']['role_revoked']=unreal.GuLiTeleportQALibrary.revoke_commander_role(self.pc)
                    elif self.name=='disconnect':
                        self.pc.destroy_actor()
                    elif self.name=='match_end':
                        unreal.GameplayStatics.get_game_mode(self.world).end_match()
                    else:
                        self.input.server_submit(unreal.GuLiTeleportCommand.CANCEL,self.cast,unreal.Vector())
                self.stage=3
            elif self.stage==3 and self.now()-self.source_time>=5:
                s=self.state()
                if s.phase==unreal.GuLiTeleportPhase.IDLE:
                    after={u['id']:u for u in self.snapshot()['units']}
                    self.case['checks']['all_original_mass_restored']=all(u['id'] in after and after[u['id']]['health']==u['health'] and not after[u['id']]['locked'] and not after[u['id']]['phased'] for u in self.captured)
                    self.case['checks']['returned_near_source']=all((after[u['id']]['x']-self.source.x)**2+(after[u['id']]['y']-self.source.y)**2<=(self.level*4000)**2 for u in self.captured)
                    self.case['checks']['field_cleaned_up']=True
                    self.case['passed']=all(self.case['checks'].values()); self.save(); self.next_case()
            if elapsed>25: raise RuntimeError('Cleanup did not complete within fixture limit: '+self.name)
        except Exception:
            self.report['errors'].append(traceback.format_exc()); self.finish()

    def save(self):
        self.out.write_text(json.dumps(self.report,ensure_ascii=False,indent=2),encoding='utf-8')

    def finish(self):
        self.finished=True
        unreal.unregister_slate_post_tick_callback(self.handle)
        self.save()


teleport_edge_run=TeleportEdgeRun()
print(json.dumps({'started':True,'output':str(teleport_edge_run.out)}))
