"""Capture the seven real runtime samples. No asset, level, or gameplay rule is saved."""
import json,time,traceback
from pathlib import Path
import unreal

def start_hover_review():
    out=Path('D:/UE5.7/test1/ArtSource/WarMachineHover_20260929/Previews');out.mkdir(exist_ok=True)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world();assert world
    actor=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor) if a.actor_has_tag('WarMachineHoverPreview20260929'))
    models=actor.get_component_by_class(unreal.InstancedStaticMeshComponent)
    driver=actor.get_component_by_class(unreal.GuLiWarMachineHoverPreviewComponent);driver.set_component_tick_enabled(True)
    pc=unreal.GameplayStatics.get_player_controller(world,0);pawn=pc.get_controlled_pawn()
    pawn.set_actor_tick_enabled(False);pawn.get_component_by_class(unreal.SpringArmComponent).set_component_tick_enabled(False)
    camera=pawn.get_component_by_class(unreal.CameraComponent);camera.set_absolute(True,True,True);camera.set_field_of_view(47)
    command=lambda s:unreal.SystemLibrary.execute_console_command(world,s)
    command('gs.Commander.HoverPreview 1');command('gs.Commander.HoverFX 1')
    cases=[(0,'idle-start'),(0,'idle-peak'),(0,'idle-other-phase'),(1,'idle-fire'),(2,'turn-in-place'),(3,'moving-720'),(4,'moving-1440'),(5,'start-stop'),(6,'lifecycle')]
    state={'case':0,'elapsed':0.,'captures':[],'done':False,'start':time.monotonic()}
    def finish(error=None):
        unreal.unregister_slate_post_tick_callback(state['handle']);state['done']=True
        r={'success':not error,'error':error,'scope':'Real native preview in standalone PIE; gameplay network lifecycle is separate','width_ratio':1.10,'core_peak_alpha':.90,'captures':state['captures']}
        (out/'review-runtime.json').write_text(json.dumps(r,indent=2),encoding='utf8')
    def tick(dt):
        try:
            if not world or time.monotonic()-state['start']>60:raise RuntimeError('PIE capture timed out')
            i,label=cases[state['case']];root=models.get_instance_transform(i,True).translation
            loc=root+unreal.Vector(1700,-2100,900);aim=root+unreal.Vector(0,0,430)
            camera.set_world_location_and_rotation(loc,unreal.MathLibrary.find_look_at_rotation(loc,aim),False,True)
            state['elapsed']+=dt
            if state['elapsed']<.85:return
            command('HighResShot 1280x720 filename='+str(out/(label+'.png')))
            data=list(models.get_editor_property('per_instance_sm_custom_data'))[i*29:(i+1)*29]
            state['captures'].append({'sample':i,'image':label+'.png','game_seconds':unreal.GameplayStatics.get_time_seconds(world),'pose':data,'root':list(root.to_tuple())})
            state['elapsed']=0;state['case']+=1
            if state['case']==len(cases):finish()
        except:finish(traceback.format_exc())
    state['handle']=unreal.register_slate_post_tick_callback(tick)
    return state

HOVER_REVIEW=start_hover_review()
unreal.MCPythonHelper.submit_result(json.dumps({'started':True,'samples':9}))
