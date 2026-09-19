"""Passive sample of the current player. Does not inject input or change transforms."""
import unreal,json,time,math
from pathlib import Path
observe_world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
observe_pc=unreal.GameplayStatics.get_player_controller(observe_world,0)
observe_pawn=observe_pc.get_controlled_pawn()
observe_started=time.monotonic()
observe_rows=[]
def observe_tick(delta):
    t=unreal.GameplayStatics.get_time_seconds(observe_world)
    if not observe_rows or t>observe_rows[-1]['time']:
        observe_rows.append({'time':t,'position':list(observe_pawn.get_actor_location().to_tuple()),'velocity':list(observe_pawn.get_velocity().to_tuple())})
    if time.monotonic()-observe_started>=15:
        unreal.unregister_slate_post_tick_callback(observe_handle)
        excess=[max(0,math.dist(b['position'][:2],a['position'][:2])-1440*(b['time']-a['time'])) for a,b in zip(observe_rows,observe_rows[1:])]
        out={'passive':True,'samples':observe_rows,'max_horizontal_excess_cm':max(excess,default=0),'net_mode':json.loads(unreal.GuLiTeleportQALibrary.snapshot(observe_world))['net_mode']}
        (Path(unreal.Paths.project_dir())/'TestResults/GroundMech/passive-movement.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
observe_handle=unreal.register_slate_post_tick_callback(observe_tick)
unreal.MCPythonHelper.submit_result(json.dumps({'observing_seconds':15,'input_injection':False}))
