"""Drive the remote Ground player in the project's existing two-client PIE session."""
import unreal,json,math,traceback
from pathlib import Path

class GroundMechNetworkCheck:
    def __init__(self):
        worlds=[w for w in unreal.ObjectIterator(unreal.World) if 'UEDPIE_' in w.get_path_name()]
        self.client=next(w for w in worlds if json.loads(unreal.GuLiTeleportQALibrary.snapshot(w))['net_mode']==3)
        self.server=next(w for w in worlds if json.loads(unreal.GuLiTeleportQALibrary.snapshot(w))['net_mode'] in (1,2))
        self.pc=unreal.GameplayStatics.get_player_controller(self.client,0)
        self.pawn=self.pc.get_controlled_pawn();assert isinstance(self.pawn,unreal.GuLiGroundMechCharacter)
        self.guid=self.pc.player_state.get_player_guid().export_text()
        self.remote=next(p for p in unreal.GameplayStatics.get_all_actors_of_class(self.server,unreal.GuLiGroundMechCharacter) if p.player_state.get_player_guid().export_text()==self.guid)
        self.sub=next(s for s in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem) if s.get_outer().get_class()==unreal.LocalPlayer.static_class() and s.get_outer().get_world()==self.client)
        self.move=unreal.load_asset('/Game/GuLiStrike/GroundMech/Input/IA_Ground_Move')
        self.sprint=unreal.load_asset('/Game/GuLiStrike/GroundMech/Input/IA_Ground_Sprint')
        self.report={'success':False,'guid':self.guid,'client_world':self.client.get_path_name(),'server_world':self.server.get_path_name(),'samples':[]}
        self.elapsed=0;self.sample_time=0;self.output=Path(unreal.Paths.project_dir())/'TestResults/GroundMech/network.json'
        self.started=unreal.GameplayStatics.get_time_seconds(self.client)
        self.last_time=self.started;self.last_position=list(self.pawn.get_actor_location().to_tuple());self.max_horizontal_speed=0
        self.handle=unreal.register_slate_post_tick_callback(self.tick)
    def snap(self,p):
        return {'location':list(p.get_actor_location().to_tuple()),'velocity':list(p.get_velocity().to_tuple()),'leg_yaw':p.get_actor_rotation().yaw,'upper_yaw':p.get_editor_property('upper_body_pivot').get_world_rotation().yaw,'scale':list(p.mesh.get_world_transform().scale3d.to_tuple()),'anim_speed':p.mesh.get_anim_instance().get_editor_property('speed')}
    def tick(self,delta):
        try:
            now=unreal.GameplayStatics.get_time_seconds(self.client)
            self.elapsed=now-self.started;self.sample_time+=now-self.last_time
            position=list(self.pawn.get_actor_location().to_tuple())
            if now>self.last_time:self.max_horizontal_speed=max(self.max_horizontal_speed,math.dist(position[:2],self.last_position[:2])/(now-self.last_time))
            self.last_time=now;self.last_position=position
            if self.elapsed<1.6:
                self.sub.inject_input_vector_for_action(self.move,unreal.Vector(0,-1,0),[],[])
                self.sub.inject_input_vector_for_action(self.sprint,unreal.Vector(1,0,0),[],[])
            # The second aim target is applied at rest to exercise ControlRotation transmission without translation.
            self.pc.set_mouse_location(530 if self.elapsed<2.5 else 100,180)
            if self.sample_time>.1:
                self.report['samples'].append({'time':self.elapsed,'client':self.snap(self.pawn),'server':self.snap(self.remote),'client_control_yaw':self.pc.get_control_rotation().yaw});self.sample_time=0
            if self.elapsed>4:
                unreal.unregister_slate_post_tick_callback(self.handle)
                rows=self.report['samples'];last=rows[-1]
                self.report['final_position_error_cm']=math.dist(last['client']['location'],last['server']['location'])
                self.report['final_upper_yaw_error_degrees']=abs((last['client']['upper_yaw']-last['server']['upper_yaw']+180)%360-180)
                self.report['travel_cm']=math.dist(rows[0]['client']['location'],last['client']['location'])
                self.report['max_frame_horizontal_speed_cm_s']=self.max_horizontal_speed
                self.report['success']=self.report['final_position_error_cm']<10 and self.report['final_upper_yaw_error_degrees']<2 and self.report['travel_cm']>100
                self.output.write_text(json.dumps(self.report,indent=2),encoding='utf-8')
        except Exception:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.report['error']=traceback.format_exc();self.output.write_text(json.dumps(self.report,indent=2),encoding='utf-8')

ground_network_check=GroundMechNetworkCheck()
unreal.MCPythonHelper.submit_result(json.dumps({'running':True}))
