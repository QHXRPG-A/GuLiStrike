"""Focused production-data dedicated PIE check. No data/asset/config saves or QA skill overrides."""
import json,os,time,traceback
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'TestResults/Scale020/runtime-q-dedicated.json'
QA=unreal.GuLiComponentSkillQALibrary
def prop(obj,name): return obj.get_editor_property(name)
def vector(v): return [v.x,v.y,v.z]
def world_snapshots():
    return [(w,json.loads(unreal.GuLiTeleportQALibrary.snapshot(w))) for w in
            unreal.ObjectIterator(unreal.World) if 'UEDPIE_' in w.get_path_name()]
def subsystem(cls,world): return next((s for s in unreal.ObjectIterator(cls) if s.get_outer()==world),None)
def state_data(s):
    return {'id':s.effect_id.to_string(),'radius':s.radius,'speed':s.motion.speed,
            'launch':vector(s.launch_location),'target':vector(s.last_target_location),
            'seed':s.random_seed,'start':s.start_time,'phase':str(s.phase),'fixed':s.fixed_point}
def reply_data(c):
    r=c.get_last_reply()
    return {'id':r.request_id.to_string(),'code':str(r.code),'error':r.error,
            'units':[{'id':prop(x.soldier_id,'value'),'code':str(x.code),
                      'effect':x.execution.effect_id.to_string(),'ready_at':x.ready_at_server_seconds}
                     for x in r.units]}
class Run:
    def __init__(self):
        self.stage=0;self.started=self.at=time.monotonic();self.done=False
        self.report={'pid':os.getpid(),'passed':False,'checks':{},'errors':[],
                     'scope':'Dedicated server plus two clients; unchanged production tables; no persistent writes',
                     'states':{},'decals':{}}
        self.ids=[];self.seen={};self.decal_seen=set()
        unreal.EditorPythonScripting.set_keep_python_script_alive(True)
        self.handle=unreal.register_slate_post_tick_callback(self.tick)
        assert QA.start_pie(2,2)
        self.save()
    def save(self):
        self.report['stage']=self.stage
        OUT.write_text(json.dumps(self.report,ensure_ascii=False,indent=2),encoding='utf-8')
    def check(self,key,ok):
        self.report['checks'][key]=bool(ok);self.save();assert ok,key
    def advance(self,s): self.stage=s;self.at=time.monotonic();self.save()
    def sample(self):
        for world,snapshot in world_snapshots():
            if snapshot['net_mode']==1:
                runtime=subsystem(unreal.GuLiCombatEffectRuntimeSubsystem,world)
                self.server_time=snapshot['time']
                for eid in self.ids:
                    state=runtime.query_effect(eid)
                    if state:
                        data=state_data(state);key=eid.to_string()
                        self.report['states'].setdefault(key,{})[world.get_path_name()]=data
                        self.seen.setdefault(key,{})[world.get_path_name()]=data
            elif snapshot['net_mode']==3:
                presentation=subsystem(unreal.GuLiCombatEffectPresentationSubsystem,world)
                if presentation:
                    for state in presentation.get_effect_states():
                        key=state.effect_id.to_string()
                        if key in [x.to_string() for x in self.ids]:
                            data=state_data(state)
                            self.report['states'].setdefault(key,{})[world.get_path_name()]=data
                            self.seen.setdefault(key,{})[world.get_path_name()]=data
                # Match circles to the frozen landing positions, not arbitrary scene decals.
                targets=[r['target'] for rows in self.report['states'].values() for r in rows.values()]
                for decal in unreal.ObjectIterator(unreal.DecalComponent):
                    if decal.get_world()!=world or not decal.is_visible(): continue
                    p=decal.get_world_location()
                    if not any((p.x-t[0])**2+(p.y-t[1])**2<4 for t in targets): continue
                    size=prop(decal,'decal_size')
                    row={'size':vector(size),'location':vector(p),'damage_edge_cm':size.y*.96}
                    self.report['decals'][world.get_path_name()]=row
                    self.decal_seen.add(world.get_path_name())
    def tick(self,dt):
        if self.done:return
        try:
            now=time.monotonic();wait=now-self.at
            assert now-self.started<180,'Production Q acceptance timed out'
            if self.stage==0:
                rows=world_snapshots()
                server=next(((w,s) for w,s in rows if s['net_mode']==1 and s['units']),None)
                if not server:return
                commanders=[p for w,s in rows if s['net_mode']==3 for p in
                    unreal.GameplayStatics.get_all_actors_of_class(w,unreal.PlayerController)
                    if p.is_local_controller() and p.player_state and p.player_state.is_commander()
                    and p.player_state.get_team()==unreal.GuLiTeam.RED]
                if not commanders:return
                self.pc=commanders[0]
                if not json.loads(QA.selection_snapshot(self.pc)).get('ready'):return
                machines=[u for u in server[1]['units'] if u['team']==1 and u['type']==2 and u['health']>0]
                if not machines:return
                self.world,self.initial=server;u=machines[0]
                self.report['initial_unit']=u
                self.center=unreal.Vector(u['x'],u['y'],u['z'])
                self.ground=self.center+unreal.Vector(2400,0,0)
                self.skills=self.pc.player_state.get_component_by_class(unreal.GuLiCommanderSkillComponent)
                self.check('selection_rpc',QA.select_radius(self.pc,self.center,820001,False))
                self.advance(1)
            elif self.stage==1 and wait>.3:
                self.selection=json.loads(QA.selection_snapshot(self.pc))
                if self.selection['pending'] or not self.selection['members']:return
                self.report['selection']=self.selection
                self.before=reply_data(self.skills)['id']
                QA.press_q(self.skills,True,self.ground)
                self.advance(2)
            elif self.stage==2:
                reply=reply_data(self.skills)
                if reply['id']==self.before:return
                self.report['reply']=reply
                self.check('production_q_succeeded',any('SUCCEEDED' in x['code'] for x in reply['units']))
                self.ids=[x.execution.effect_id for x in self.skills.get_last_reply().units if 'SUCCEEDED' in str(x.code)]
                self.sample()
                self.advance(3)
            elif self.stage==3:
                self.sample()
                if wait<.5:return
                shared=[rows for rows in self.seen.values() if len(rows)>=3]
                if not shared and wait<3:return
                self.check('same_effect_seen_on_server_and_two_clients',bool(shared))
                for index,rows in enumerate(shared):
                    values=list(rows.values());first=values[0]
                    self.check('radius_and_speed_'+str(index),all(abs(v['radius']-160)<.001 and abs(v['speed']-1200)<.001 for v in values))
                    # Effects retain FVector_NetQuantize's 1 cm codec (Mass is 20/2 cm).
                    # Server is unquantized; clients must agree exactly with each other.
                    quant_error=max(abs(v[k][axis]-first[k][axis]) for v in values for k in ('target','launch') for axis in range(3))
                    self.report['maximum_effect_quantization_error_cm']=max(self.report.get('maximum_effect_quantization_error_cm',0),quant_error)
                    clients=[v for w,v in rows.items() if 'UEDPIE_0_' not in w]
                    self.check('frozen_target_and_launch_'+str(index),quant_error<=.501 and
                               all(v['seed']==first['seed'] for v in values) and
                               all(v['launch']==clients[0]['launch'] and v['target']==clients[0]['target'] for v in clients))
                    self.check('spread_inside_4m_radius_'+str(index),(first['target'][0]-self.ground.x)**2+(first['target'][1]-self.ground.y)**2<=400.1**2)
                self.check('both_clients_have_landing_decal',len(self.decal_seen)==2)
                self.check('decal_damage_boundary_and_depth',all(abs(d['damage_edge_cm']-160)<.01 and abs(d['size'][0]-80)<.01 for d in self.report['decals'].values()))
                for row in self.report['reply']['units']:
                    if 'SUCCEEDED' not in row['code']:continue
                    starts=[v['start'] for v in self.seen[row['effect']].values()]
                    self.check('cooldown_6_seconds_'+str(row['id']),abs(row['ready_at']-starts[0]-6)<.05)
                self.before=reply_data(self.skills)['id']
                QA.press_q(self.skills,True,self.ground)
                self.advance(4)
            elif self.stage==4 and wait>.5:
                self.report['repeat_reply']=reply_data(self.skills)
                # Input can suppress a known cooldown locally; otherwise the server must reject it.
                self.check('repeat_does_not_launch',self.report['repeat_reply']['id']==self.before or
                           not any('SUCCEEDED' in u['code'] for u in self.report['repeat_reply']['units']))
                self.advance(5)
            elif self.stage==5 and wait>9:
                self.report['final_warning_counts']={s.get_outer().get_path_name():s.get_active_warning_count()
                    for s in unreal.ObjectIterator(unreal.GuLiGroundWarningSubsystem)
                    if 'UEDPIE_' in s.get_outer().get_path_name()}
                self.check('warnings_released',all(n==0 for n in self.report['final_warning_counts'].values()))
                self.report['passed']=True;self.finish()
        except Exception:
            self.report['errors'].append(traceback.format_exc());self.finish()
    def finish(self):
        self.done=True;unreal.unregister_slate_post_tick_callback(self.handle);self.save()
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
        self.exit_at=time.monotonic()
        def quit_later(dt):
            if time.monotonic()-self.exit_at>2:
                unreal.unregister_slate_post_tick_callback(self.exit_handle)
                unreal.SystemLibrary.quit_editor()
        self.exit_handle=unreal.register_slate_post_tick_callback(quit_later)
run=Run()
