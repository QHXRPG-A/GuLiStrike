"""Explicit 500-unit PIE A/B capture. Temporarily fixed camera; no persistent game settings or asset writes."""
import json,time,traceback
from pathlib import Path
import unreal

def start_hover_profile():
    out=Path('D:/UE5.7/test1/ArtSource/WarMachineHover_20260929/performance-held');out.mkdir(exist_ok=True)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world();assert world
    pc=unreal.GameplayStatics.get_player_controller(world,0);pawn=pc.get_controlled_pawn()
    camera=pawn.get_component_by_class(unreal.CameraComponent)
    actor=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.GuLiCommanderPresentationActor)[0]
    bodies=[c for c in actor.get_components_by_class(unreal.InstancedStaticMeshComponent) if c.static_mesh and '_Rigid' in c.static_mesh.get_name()]
    assert sum(c.get_instance_count() for c in bodies)==500,'Fixture must contain 500 body instances'
    wm=next(c for c in bodies if c.get_name()=='UnitInstances_Type_2_Team_2')
    points=[wm.get_instance_transform(i,True).translation for i in range(wm.get_instance_count())]
    center=sum(points,unreal.Vector())/len(points)
    pawn.set_actor_tick_enabled(False);pawn.get_component_by_class(unreal.SpringArmComponent).set_component_tick_enabled(False)
    camera.set_absolute(True,True,True);camera.set_field_of_view(50)
    command=lambda s:unreal.SystemLibrary.execute_console_command(world,s)
    for s in ['gs.Commander.HoverPreview 0','gs.Commander.HoverFX.LogStats 0','r.GPUCsvStatsEnabled 1','CsvCategory RHI enable','CsvCategory GuLiHover enable','CsvCategory NiagaraGpuCompute enable']:command(s)
    # A preceding alignment readback may have frozen the preview's source tick.
    # Let its normal off branch hide the seven samples and release their FX batch.
    for preview in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor):
        if preview.actor_has_tag('WarMachineHoverPreview20260929'):
            preview.get_component_by_class(unreal.GuLiWarMachineHoverPreviewComponent).set_component_tick_enabled(True)
    for component in unreal.ObjectIterator(unreal.GuLiBuildingProductionComponent):
        if component.get_world()==world:component.set_component_tick_enabled(False)
    settings={k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in ['r.ScreenPercentage','r.DynamicRes.OperationMode','r.VSync','t.MaxFPS']+['sg.'+q+'Quality' for q in ['ViewDistance','AntiAliasing','Shadow','GlobalIllumination','Reflection','PostProcess','Texture','Effects','Foliage','Shading']]}
    cases=[(distance,enabled) for distance in [10000,20000,36000] for enabled in [0,1]]
    state={'index':0,'frames':-120,'records':[],'done':False,'start':time.monotonic()}
    def finish(error=None):
        command('CsvProfile STOP');command('gs.Commander.HoverFX 1')
        unreal.unregister_slate_post_tick_callback(state['handle']);state['done']=True
        final_points=[wm.get_instance_transform(i,True).translation for i in range(wm.get_instance_count())]
        drift=max(((p-q).length() for p,q in zip(points,final_points)),default=0)
        report={'success':not error,'error':error,'scope':'Standalone PIE in Editor; 500 mixed bodies held by normal stop orders; blue formation in view, red remains distant; no SimCache during capture; not a packaged benchmark',
            'map':'/Game/Maps/LVL_CommanderMassPrototype','viewport':list(pc.get_viewport_size()),'settings':settings,'camera_center':list(center.to_tuple()),
            'body_counts':{c.get_name():c.get_instance_count() for c in bodies},'records':state['records'],'requested_frames_per_capture':240,'warmup_frames':120,'max_warmachine_root_drift_cm':drift}
        (out/'capture.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    def setup_case():
        distance,enabled=cases[state['index']]
        location=center+unreal.Vector(0,-.819152044*distance,.573576436*distance)
        camera.set_world_location_and_rotation(location,unreal.MathLibrary.find_look_at_rotation(location,center),False,True)
        command('gs.Commander.HoverFX '+str(enabled))
        state['frames']=-120
    def tick(dt):
        try:
            if time.monotonic()-state['start']>360:raise RuntimeError('Performance capture timed out')
            frame=state['frames'];distance,enabled=cases[state['index']]
            if sum(c.get_instance_count() for c in bodies)!=500:raise RuntimeError('Population changed during capture')
            if frame==0:
                stem=f'idle-{distance//100}m-fx{enabled}'
                assert not (out/(stem+'.csv')).exists(),'Refusing to overwrite an earlier capture'
                command('CsvProfile STARTFILE=../../../ArtSource/WarMachineHover_20260929/performance-held/'+stem+'.csv')
                command('CsvProfile FRAMES=240')
                state['records'].append({'csv':stem+'.csv','distance_to_fixture_center_cm':distance,'fx':bool(enabled),'time_seconds':unreal.GameplayStatics.get_time_seconds(world)})
            if frame==250:
                command('HighResShot 1280x720 filename='+str(out/(f'idle-{distance//100}m-fx{enabled}.png')))
            state['frames']+=1
            if frame>=275:
                state['index']+=1
                if state['index']==len(cases):finish()
                else:setup_case()
        except:finish(traceback.format_exc())
    setup_case();state['handle']=unreal.register_slate_post_tick_callback(tick)
    return state

HOVER_PROFILE=start_hover_profile()
unreal.MCPythonHelper.submit_result(json.dumps({'started':True,'cases':6,'frames_each':240}))
