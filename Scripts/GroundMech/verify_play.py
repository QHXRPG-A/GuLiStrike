"""PIE acceptance capture using UE's Enhanced Input injection and existing QA helpers.
Run only in a dedicated, unattended standalone PIE session. This fixture deliberately
relocates the pawn between cases; never run it while the user is controlling the pawn.
Temporary collision fixtures exist only in PIE; this script never saves assets.
"""
import unreal,json,traceback
from pathlib import Path

class GroundMechPlayCheck:
    def __init__(self):
        self.world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        self.pc=unreal.GameplayStatics.get_player_controller(self.world,0)
        self.pawn=self.pc.get_controlled_pawn()
        assert isinstance(self.pawn,unreal.GuLiGroundMechCharacter)
        self.sub=next(s for s in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem) if s.get_outer().get_class()==unreal.LocalPlayer.static_class() and s.get_outer().get_world()==self.world)
        self.actions={name:unreal.load_asset('/Game/GuLiStrike/GroundMech/Input/IA_'+name) for name in ['Ground_Move','Ground_Sprint','Ground_Zoom','Battle_Build','Battle_Cancel','Battle_Slot2']}
        self.original=self.pawn.get_actor_location()
        self.output=Path(unreal.Paths.project_dir())/'TestResults/GroundMech/play.json'
        self.report={'success':False,'initial_role':str(self.pc.player_state.get_battle_role()),'ready':self.pc.player_state.is_battle_ready(),'spawn':list(self.original.to_tuple()),'phases':{}}
        self.platform=unreal.GuLiTeleportQALibrary.create_blocker(self.world,unreal.Vector(16000,-10000,3000),unreal.Vector(3200,3200,30))
        self.blocker=None
        self.phases=[('idle',1,{}),('walk',2,{'Ground_Move':(0,1,0)}),('run',2,{'Ground_Move':(0,1,0),'Ground_Sprint':(1,0,0)}),('diagonal',2,{'Ground_Move':(1,1,0),'Ground_Sprint':(1,0,0)}),('turn',1.5,{'Ground_Move':(1,0,0)}),('cancel',.7,{}),('ui_locked',1,{'Ground_Move':(0,1,0),'Ground_Sprint':(1,0,0)}),('ui_restored',1,{'Ground_Move':(0,1,0)}),('obstacle',2,{'Ground_Move':(0,1,0),'Ground_Sprint':(1,0,0)}),('zoom_min',.8,{'Ground_Zoom':(3,0,0)}),('zoom_max',.8,{'Ground_Zoom':(-3,0,0)}),('terrain',1.3,{'Ground_Move':(0,-1,0)}),('build',.8,{'Battle_Build':(1,0,0)}),('build_slot',.8,{'Battle_Slot2':(1,0,0)}),('build_cancel',.6,{'Battle_Cancel':(1,0,0)})]
        self.phases[6:8]=[('ui_locked',1,{'Ground_Move':(0,1,0),'Ground_Sprint':(1,0,0)}),('ui_restored',1,{'Ground_Move':(0,1,0)}),('keyboard_run',1.5,{}),('focus_flush',.7,{}),('unpossessed',.3,{}),('repossessed',1,{'Ground_Move':(0,1,0)})]
        self.phases[8:8]=[('keyboard_'+key,1,{}) for key in ('W','A','S','D','diagonal')]
        self.index=-1;self.elapsed=0;self.sample_time=0;self.first=True
        self.advance()
        self.handle=unreal.register_slate_post_tick_callback(self.tick)
    def snap(self):
        p=self.pawn;m=p.mesh;a=m.get_anim_instance()
        return {'location':list(p.get_actor_location().to_tuple()),'velocity':list(p.get_velocity().to_tuple()),'speed':p.get_velocity().length(),'anim_speed':a.get_editor_property('speed'),'turn_rate':a.get_editor_property('turn_rate'),'leg_yaw':p.get_actor_rotation().yaw,'aim_yaw':self.pc.get_control_rotation().yaw,'upper_yaw':p.get_editor_property('upper_body_pivot').get_world_rotation().yaw,'foot_r':list(m.get_socket_transform('Foot_R',unreal.RelativeTransformSpace.RTS_COMPONENT).translation.to_tuple()),'actor_scale':list(p.get_actor_scale3d().to_tuple()),'mesh_scale':list(m.get_world_transform().scale3d.to_tuple()),'zoom':p.get_editor_property('camera_boom').target_arm_length,'build_active':self.pc.get_component_by_class(unreal.GuLiBuildingPlacementComponent).is_build_mode_active(),'building_type':str(self.pc.get_component_by_class(unreal.GuLiBuildingPlacementComponent).get_selected_building_type())}
    def advance(self):
        self.index+=1;self.elapsed=0;self.sample_time=0;self.first=True
        if self.index==len(self.phases):
            self.finish();return
        name,duration,actions=self.phases[self.index]
        for key in ('W','A','S','D','LeftShift'):unreal.SystemLibrary.execute_console_command(self.world,'Input.-key '+key,self.pc)
        if name in ('idle','walk','run','diagonal','turn','obstacle') or name.startswith('keyboard_'):
            self.pawn.character_movement.stop_movement_immediately()
            self.pawn.set_actor_location(unreal.Vector(14000,-10000,3410),False,True)
            self.pawn.set_actor_rotation(unreal.Rotator(yaw=0),True)
        if name=='obstacle':self.blocker=unreal.GuLiTeleportQALibrary.create_blocker(self.world,unreal.Vector(15000,-10000,3600),unreal.Vector(50,1000,600))
        if name=='ui_locked':
            unreal.GuLiComponentSkillQALibrary.set_gm_panel_open(self.pc,True)
            self.report['gm_open_move_ignored']=self.pc.is_move_input_ignored()
        if name=='ui_restored':
            unreal.GuLiComponentSkillQALibrary.set_gm_panel_open(self.pc,False)
            self.report['gm_closed_move_ignored']=self.pc.is_move_input_ignored()
        if name.startswith('keyboard_'):
            keys={'run':('W','LeftShift'),'diagonal':('W','D','LeftShift')}.get(name[9:],(name[9:],))
            for key in keys:unreal.SystemLibrary.execute_console_command(self.world,'Input.+key '+key,self.pc)
        if name=='focus_flush':unreal.GuLiComponentSkillQALibrary.flush_player_input(self.pc)
        if name=='unpossessed':
            self.pc.un_possess()
            self.report['unpossessed_mapping']=str(self.sub.has_mapping_context(self.pawn.get_editor_property('mapping_context')))
        if name=='repossessed':
            self.pc.possess(self.pawn)
            self.report['repossessed_mapping']=str(self.sub.has_mapping_context(self.pawn.get_editor_property('mapping_context')))
        if name=='terrain':
            self.pawn.character_movement.stop_movement_immediately()
            self.pawn.set_actor_location(self.original,False,True)
        self.pc.set_mouse_location(510,190)
        self.report['phases'][name]={'start':self.snap(),'samples':[]}
    def tick(self,delta):
        try:
            name,duration,actions=self.phases[self.index]
            for action,value in actions.items():
                if action.startswith('Battle_') and not self.first:continue
                self.sub.inject_input_vector_for_action(self.actions[action],unreal.Vector(*value),[],[])
            self.first=False;self.elapsed+=delta;self.sample_time+=delta
            if self.sample_time>=.1:
                self.report['phases'][name]['samples'].append(self.snap());self.sample_time=0
            if self.elapsed>=duration:
                self.report['phases'][name]['end']=self.snap()
                self.output.write_text(json.dumps(self.report,indent=2),encoding='utf-8')
                self.advance()
        except Exception:
            self.report['error']=traceback.format_exc();self.finish()
    def finish(self):
        unreal.unregister_slate_post_tick_callback(self.handle)
        self.pawn.character_movement.stop_movement_immediately()
        self.pawn.set_actor_location(self.original,False,True)
        self.platform.destroy_actor()
        if self.blocker:self.blocker.destroy_actor()
        self.pc.reset_ignore_move_input()
        self.report['success']='error' not in self.report
        self.output.write_text(json.dumps(self.report,indent=2),encoding='utf-8')

ground_play_check=GroundMechPlayCheck()
unreal.MCPythonHelper.submit_result(json.dumps({'running':True,'output':str(ground_play_check.output)}))
