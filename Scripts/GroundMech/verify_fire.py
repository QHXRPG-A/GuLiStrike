"""Authorized fire-only PIE checks. Run in the isolated FireReview map, listen or dedicated.

Uses real shared mouse input, server upgrade API and replicated shot counters.
Transient test controls are restored; never saves gameplay assets.
"""
import unreal, json, math, traceback
from pathlib import Path

OUT=Path('D:/UE5.7/test1/TestResults/GroundMech/Fire')

class GroundMechFireCheck:
    def __init__(self):
        worlds=[w for w in unreal.ObjectIterator(unreal.World) if 'UEDPIE_' in w.get_path_name()]
        modes={w:json.loads(unreal.GuLiTeleportQALibrary.snapshot(w))['net_mode'] for w in worlds}
        self.server=next(w for w in worlds if modes[w] in (0,1,2))
        self.client=next((w for w in worlds if modes[w]==3),self.server)
        self.pc=unreal.GameplayStatics.get_player_controller(self.client,0)
        self.pawn=self.pc.get_controlled_pawn()
        assert isinstance(self.pawn,unreal.GuLiGroundMechCharacter)
        self.guid=self.pawn.player_state.get_player_guid().export_text()
        self.peers=[]
        for w in worlds:
            for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiGroundMechCharacter):
                if p.player_state and p.player_state.get_player_guid().export_text()==self.guid:
                    self.peers.append((w,p,p.get_editor_property('weapon')))
        self.remote=next(p for w,p,c in self.peers if w==self.server)
        self.weapon=self.pawn.get_editor_property('weapon')
        self.server_weapon=self.remote.get_editor_property('weapon')
        assert all(c.is_weapon_ready() for w,p,c in self.peers)
        self.mode='dedicated' if modes[self.server]==1 else 'listen' if modes[self.server]==2 else 'standalone'
        self.report={'success':False,'mode':self.mode,'phases':{},'samples':[],'checks':{},'guid':self.guid}
        self.primary=unreal.load_asset('/Game/GuLiStrike/GroundMech/Input/IA_Battle_Primary')
        self.sub=next(s for s in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem) if s.get_outer().get_class()==unreal.LocalPlayer.static_class() and s.get_outer().get_world()==self.client)
        self.build=unreal.load_asset('/Game/GuLiStrike/GroundMech/Input/IA_Battle_Build')
        self.cancel=unreal.load_asset('/Game/GuLiStrike/GroundMech/Input/IA_Battle_Cancel')
        self.teleport=unreal.load_asset('/Game/GuLiStrike/GroundMech/Input/IA_Battle_Teleport')
        self.server_pc=self.remote.get_controller()
        self.phases=[('settle',.5),('level1',2.),('release1',.5),('level2',2.),('release2',.5),('level3',2.),('release3',.5),('focus_start',.4),('focus_flush',.6),('ui_start',.4),('ui_lock',.8),('ui_release',.4),('build_mode',.8),('build_cancel',.4),('teleport_mode',.7),('teleport_cancel',.4),('unpossess_start',.4),('unpossessed',.7),('repossessed',.7),('final_fire',1.),('cleanup',5.5)]
        self.index=-1;self.handle=None;self.started=unreal.GameplayStatics.get_time_seconds(self.server)
        self.advance()
        self.handle=unreal.register_slate_post_tick_callback(self.tick)
    def now(self): return unreal.GameplayStatics.get_time_seconds(self.server)
    def key(self,down):
        unreal.SystemLibrary.execute_console_command(self.client,'Input.'+('+' if down else '-')+'key LeftMouseButton',self.pc)
    def control(self,action):
        unreal.SystemLibrary.execute_console_command(None,'gs.MechFire.QA.Control '+self.guid+' '+action)
    def snap(self):
        peers=[]
        for w,p,c in self.peers:
            mesh=p.get_editor_property('machinegun');anim=mesh.get_anim_instance()
            bone=mesh.get_socket_transform('Barrel_big',unreal.RelativeTransformSpace.RTS_WORLD)
            parent=mesh.get_socket_transform(mesh.get_parent_bone('Barrel_big'),unreal.RelativeTransformSpace.RTS_WORLD)
            local=unreal.MathLibrary.inverse_transform_location(parent,bone.translation)
            muzzle=mesh.get_socket_transform('Muzzle',unreal.RelativeTransformSpace.RTS_WORLD)
            peers.append({'world':w.get_path_name(),'shots':c.shots_fired,'cosmetic':c.cosmetic_shots,'rate':c.get_fire_rate(),'damage':c.get_shot_damage(),'held':c.fire_held,'alpha':anim.recoil_alpha,'barrel_z':local.z,'muzzle':list(muzzle.translation.to_tuple())})
        return {'time':self.now()-self.started,'peers':peers,'teleport_aiming':self.pc.get_editor_property('teleport_input').is_aiming()}
    def advance(self):
        self.index+=1
        if self.index==len(self.phases): self.finish(); return
        self.name,self.duration=self.phases[self.index]
        self.phase_start=self.now();self.last_shots=self.server_weapon.shots_fired
        self.pc.set_mouse_location(690,215)
        if self.name.startswith('level'):
            level=int(self.name[-1]); assert self.server_weapon.apply_upgrade_by_id('1.'+str(level))
            self.key(True)
        elif self.name=='focus_start':
            # Console Input.+key is a persistent synthetic source which re-injects a
            # fresh press after FlushPressedKeys. Test the held intent without that source.
            self.control('held')
        elif self.name in ('ui_start','unpossess_start','final_fire'):
            self.key(True)
        elif self.name=='focus_flush':
            self.control('flush')
        elif self.name=='ui_lock':
            unreal.GuLiComponentSkillQALibrary.set_gm_panel_open(self.pc,True)
        elif self.name=='ui_release':
            unreal.GuLiComponentSkillQALibrary.set_gm_panel_open(self.pc,False);self.key(False)
        elif self.name=='build_mode':
            self.key(False);self.sub.inject_input_vector_for_action(self.build,unreal.Vector(1,0,0),[],[])
        elif self.name=='build_cancel':
            self.key(False);self.sub.inject_input_vector_for_action(self.cancel,unreal.Vector(1,0,0),[],[])
        elif self.name=='teleport_mode':
            self.key(False);self.sub.inject_input_vector_for_action(self.teleport,unreal.Vector(1,0,0),[],[])
        elif self.name=='teleport_cancel':
            self.key(False);self.sub.inject_input_vector_for_action(self.cancel,unreal.Vector(1,0,0),[],[])
        elif self.name=='unpossessed':
            self.control('unpossess');self.key(False)
        elif self.name=='repossessed':
            self.control('repossess')
        else:
            self.key(False)
        self.report['phases'][self.name]={'start_shots':self.last_shots,'start':self.now(),'samples':[]}
    def tick(self,delta):
        try:
            self.pc.set_mouse_location(690,215)
            elapsed=self.now()-self.phase_start
            if self.name in ('build_mode','teleport_mode') and .15<elapsed<.25:
                self.key(True)
            row=self.snap()
            self.report['phases'][self.name]['samples'].append(row)
            if self.server_weapon.shots_fired!=self.last_shots:
                self.report['samples'].append(row); self.last_shots=self.server_weapon.shots_fired
            if elapsed>=self.duration:
                phase=self.report['phases'][self.name]
                phase['end_shots']=self.server_weapon.shots_fired
                phase['shots']=phase['end_shots']-phase['start_shots']
                phase['end']=self.now()
                if self.name in ('level3','cleanup'):
                    unreal.SystemLibrary.execute_console_command(None,'gs.MechFire.QA.Sample '+self.mode+'_'+self.name)
                    phase['native']=json.loads((OUT/'native-sample.json').read_text(encoding='utf-8'))
                self.advance()
        except Exception:
            self.report['error']=traceback.format_exc();self.finish()
    def finish(self):
        if self.handle: unreal.unregister_slate_post_tick_callback(self.handle)
        self.key(False);self.control('release')
        unreal.GuLiComponentSkillQALibrary.set_gm_panel_open(self.pc,False)
        checks=self.report['checks'];phases=self.report['phases']
        if 'cleanup' in phases and 'end_shots' in phases['cleanup']:
            for level in (1,2,3):
                phase=phases['level'+str(level)]
                checks['rate_'+str(level)]=abs(phase['shots']-level*4)<=1
            for name in ('release1','release2','release3','focus_flush','ui_lock','build_mode','unpossessed','cleanup'):
                rows=phases[name]['samples'];counts=[r['peers'][next(i for i,p in enumerate(r['peers']) if 'UEDPIE_0_' in p['world'])]['shots'] for r in rows if r['time']-rows[0]['time']>.25]
                checks[name+'_stops']=bool(counts) and max(counts)==min(counts)
            final=self.snap()['peers'];checks['replicated_shot_count']=len({p['shots'] for p in final})==1
            checks['recoil_restored']=all(abs(p['barrel_z']-188.101471)<.01 for p in final)
            checks['no_remaining_fire_fx']=all(not e['active'] and e['particles']==0 for w in phases['cleanup']['native'] for e in w['effects'])
            checks['dedicated_no_cosmetics']=all(not w['effects'] and all(p['cosmetic_shots']==0 for p in w['players']) for w in phases['cleanup']['native'] if w['net_mode']==1)
            checks['invalid_upgrade_rejected']=not self.server_weapon.apply_upgrade_by_id('1.9')
            checks['held_before_focus_flush']=phases['focus_start']['shots']>0
            checks['fire_after_repossess']=phases['final_fire']['shots']>0
            # The existing teleport cast UI is Commander-only. Ground may be a
            # teleport participant, but T does not arm a cast on this role.
            checks['teleport_existing_ground_role']=not any(r['teleport_aiming'] for r in phases['teleport_mode']['samples']) and phases['teleport_mode']['shots']>0
            effects=[e for w in phases['level3']['native'] for e in w['effects']]
            bullets=[e for e in effects if e['asset']=='NS_GroundMech_Bullet']
            flashes=[e for e in effects if e['asset']=='NS_GroundMech_Muzzle' and e['active']]
            checks['one_particle_per_bullet']=bool(bullets) and all(e['particles']<=1 for e in bullets)
            checks['muzzle_axis_alignment']=bool(flashes) and all(e.get('muzzle_alignment',0)>.999 for e in flashes)
        self.report['success']='error' not in self.report and bool(checks) and all(checks.values())
        (OUT/('pie-'+self.mode+'.json')).write_text(json.dumps(self.report,indent=2),encoding='utf-8')
        self.peers=[]
        self.pawn=self.remote=self.pc=self.server_pc=self.server=self.client=self.server_weapon=self.weapon=self.sub=None

class GroundMechFireEvidence:
    """Capture one real shot at 0.1x time scale, with actual bone/particle readback."""
    def __init__(self):
        self.world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        self.pc=unreal.GameplayStatics.get_player_controller(self.world,0)
        self.pawn=self.pc.get_controlled_pawn();self.weapon=self.pawn.get_editor_property('weapon')
        assert self.weapon.is_weapon_ready() and self.weapon.apply_upgrade_by_id('1.1')
        self.guid=self.pawn.player_state.get_player_guid().export_text()
        self.camera=self.pawn.get_editor_property('camera');self.frames=[];self.shot_time=None;self.next_capture=0;self.handle=None
        self.camera_relative=unreal.Transform(location=self.camera.relative_location,rotation=self.camera.relative_rotation,scale=self.camera.relative_scale3d)
        self.camera_fov=self.camera.field_of_view
        center=self.pawn.get_actor_location()+unreal.Vector(300,0,170)
        location=self.pawn.get_actor_location()+unreal.Vector(300,1300,650)
        transform=unreal.Transform(location=location,rotation=unreal.MathLibrary.find_look_at_rotation(location,center))
        self.camera.set_absolute(True,True,False)
        self.camera.set_world_transform(transform,False,True)
        self.camera.set_field_of_view(75)
        self.aim_point=self.pawn.get_actor_location()+unreal.Vector(1200,0,-350)
        self.mouse=(1,1)
        self.pc.set_mouse_location(*self.mouse)
        self.start=unreal.GameplayStatics.get_time_seconds(self.world)
        self.handle=unreal.register_slate_post_tick_callback(self.tick)
    def tick(self,delta):
        try:
            now=unreal.GameplayStatics.get_time_seconds(self.world)
            screen=self.pc.project_world_location_to_screen(self.aim_point,True)
            if screen:self.mouse=(int(screen.x),int(screen.y))
            self.pc.set_mouse_location(*self.mouse)
            if self.shot_time is None:
                if now-self.start<.7:return
                unreal.SystemLibrary.execute_console_command(None,'gs.MechFire.QA.Sample fire_idle')
                unreal.GameplayStatics.set_global_time_dilation(self.world,.1)
                self.before_shots=self.weapon.shots_fired
                unreal.SystemLibrary.execute_console_command(None,'gs.MechFire.QA.Control '+self.guid+' held')
                self.shot_time=now
                unreal.SystemLibrary.execute_console_command(None,'gs.MechFire.QA.Control '+self.guid+' release')
            age=now-self.shot_time
            if age>=self.next_capture:
                label='fire_frame_'+str(len(self.frames)).zfill(3)
                unreal.SystemLibrary.execute_console_command(None,'gs.MechFire.QA.Sample '+label)
                native=json.loads((OUT/'native-sample.json').read_text(encoding='utf-8'))
                self.frames.append({'age':age,'image':label+'-0.png','native':native})
                self.next_capture=age+.005
            if age>=.35:
                self.finish()
        except Exception:
            self.finish(traceback.format_exc())
    def finish(self,error=None):
        if self.handle:unreal.unregister_slate_post_tick_callback(self.handle)
        unreal.GameplayStatics.set_global_time_dilation(self.world,1)
        self.camera.set_absolute(False,False,False)
        self.camera.set_relative_transform(self.camera_relative,False,True)
        self.camera.set_field_of_view(self.camera_fov)
        fired=self.weapon.shots_fired-getattr(self,'before_shots',self.weapon.shots_fired)
        report={'success':not error and fired==1,'error':error,'time_scale':.1,'frames':self.frames,'shot_count':fired}
        (OUT/'visual-sequence.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        self.world=self.pc=self.pawn=self.weapon=self.camera=None

if globals().get('FIRE_CAPTURE',False):
    ground_fire_check=GroundMechFireEvidence()
    unreal.MCPythonHelper.submit_result(json.dumps({'running':True,'mode':'visual_sequence'}))
else:
    ground_fire_check=GroundMechFireCheck()
    unreal.MCPythonHelper.submit_result(json.dumps({'running':True,'mode':ground_fire_check.mode}))
