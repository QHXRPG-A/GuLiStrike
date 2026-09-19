"""Short continuous run, with no transform writes, to check pose-loop continuity."""
import unreal,json,math
from pathlib import Path
rm_world=next(w for w in unreal.ObjectIterator(unreal.World) if 'UEDPIE_1_' in w.get_path_name())
rm_pc=unreal.GameplayStatics.get_player_controller(rm_world,0)
rm_pawn=rm_pc.get_controlled_pawn()
rm_sub=next(s for s in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem) if s.get_outer().get_class()==unreal.LocalPlayer.static_class() and s.get_outer().get_world()==rm_world)
rm_move=unreal.load_asset('/Game/GuLiStrike/GroundMech/Input/IA_Ground_Move')
rm_sprint=unreal.load_asset('/Game/GuLiStrike/GroundMech/Input/IA_Ground_Sprint')
rm_start=unreal.GameplayStatics.get_time_seconds(rm_world)
rm_rows=[]
def root_motion_tick(delta):
    t=unreal.GameplayStatics.get_time_seconds(rm_world)-rm_start
    if t<3.2:
        rm_sub.inject_input_vector_for_action(rm_move,unreal.Vector(0,1,0),[],[])
        rm_sub.inject_input_vector_for_action(rm_sprint,unreal.Vector(1,0,0),[],[])
    root=rm_pawn.mesh.get_socket_transform('HIPS',unreal.RelativeTransformSpace.RTS_COMPONENT)
    rm_rows.append({'t':t,'actor':list(rm_pawn.get_actor_location().to_tuple()),'root':list(root.translation.to_tuple()),'foot':list(rm_pawn.mesh.get_socket_transform('Foot_R',unreal.RelativeTransformSpace.RTS_COMPONENT).translation.to_tuple()),'speed':rm_pawn.get_velocity().length()})
    if t>4:
        unreal.unregister_slate_post_tick_callback(rm_handle)
        maximum=max(math.dist(r['root'],[0,0,0]) for r in rm_rows)
        result={'success':maximum<.01,'mode':str(rm_pawn.mesh.get_anim_instance().get_editor_property('root_motion_mode')),'root_max_offset_cm':maximum,'max_speed_cm_s':max(r['speed'] for r in rm_rows),'transform_writes':0,'samples':rm_rows}
        (Path(unreal.Paths.project_dir())/'TestResults/GroundMech/root-motion.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
rm_handle=unreal.register_slate_post_tick_callback(root_motion_tick)
unreal.MCPythonHelper.submit_result(json.dumps({'running':True,'transform_writes':0}))
