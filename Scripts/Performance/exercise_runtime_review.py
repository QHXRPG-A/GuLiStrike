"""Opt-in functional readback in the authorized three-World PIE review session."""
import json, time, sys
from run_four_stage_review import OUT, run, result, launch, stop

events=[]
def note(name,code):
    value=run(code);events.append({'name':name,'value':value})
    (OUT/'runtime-functional-review.json').write_text(json.dumps(events,indent=2),encoding='utf-8')
    print(json.dumps({'name':name,'value':value}),flush=True);return value

try:
    launch('runtime','optimized_controls')
    if '--remaining' not in sys.argv:
        result("ws=unreal.EditorLevelLibrary.get_pie_worlds(True)\nserver=ws[0]\nw=ws[1]\npc=unreal.GameplayStatics.get_player_controller(w,0)\ncam=pc.get_view_target()\nunreal.SystemLibrary.execute_console_command(server,'gs.Avoidance.VerifyPrefix 1')\np=next(s for s in unreal.ObjectIterator(unreal.GuLiPerformanceSubsystem) if s.get_outer()==server)\np.begin_capture()")
        time.sleep(2)
        note('prefix_audit',"p.end_capture()\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'capture':json.loads(p.get_capture_json())}))")
        result("unreal.SystemLibrary.execute_console_command(server,'gs.Avoidance.VerifyPrefix 0')")
        note('stop_order',"ok=unreal.GuLiComponentSkillQALibrary.order_performance_population(server,0)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':ok}))")
        time.sleep(.5)
        note('stopped_population',"unreal.MCPythonHelper.submit_result(unreal.GuLiComponentSkillQALibrary.performance_population_snapshot(server))")
        note('replacement_order',"ok=unreal.GuLiComponentSkillQALibrary.order_performance_population(server,-1)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':ok}))")
        time.sleep(1.5)
        note('reordered_population',"unreal.MCPythonHelper.submit_result(unreal.GuLiComponentSkillQALibrary.performance_population_snapshot(server))")
        setup=note('ui_initial',"reg=next(s for s in unreal.ObjectIterator(unreal.GuLiSceneUISourceRegistry) if s.get_outer()==w)\nui=next(x for x in unreal.ObjectIterator(unreal.GuLiSceneUIWidget) if x.get_world()==w)\nhud=pc.get_hud()\nhud.set_editor_property('show_hud',False)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'registry':json.loads(reg.get_registry_stats_json()),'ui':json.loads(ui.get_frame_stats_json())}))")
        for action in ['rect','empty','remount']:
            note('hud_'+action,f"unreal.MCPythonHelper.submit_result(unreal.GuLiComponentSkillQALibrary.scene_ui_performance_probe(pc,{action!r},unreal.Vector(0,0,0),unreal.Vector(0,0,0)))")
        note('hud_expiry_submit',"unreal.MCPythonHelper.submit_result(unreal.GuLiComponentSkillQALibrary.scene_ui_performance_probe(pc,'rect',unreal.Vector(0,0,0),unreal.Vector(0,0,0)))")
        time.sleep(.2)
        note('hud_expired',"unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'ui':json.loads(ui.get_frame_stats_json()),'registry':json.loads(reg.get_registry_stats_json())}))")
        note('cross_screen_line',"center=cam.get_actor_location()+cam.get_actor_forward_vector()*3000\nright=cam.get_actor_right_vector()*100000\nunreal.MCPythonHelper.submit_result(unreal.GuLiComponentSkillQALibrary.scene_ui_performance_probe(pc,'read',center-right,center+right))")
        note('near_plane_line',"center=cam.get_actor_location()\nforward=cam.get_actor_forward_vector()*3000\nunreal.MCPythonHelper.submit_result(unreal.GuLiComponentSkillQALibrary.scene_ui_performance_probe(pc,'read',center-forward,center+forward))")
    else:
        result("ws=unreal.EditorLevelLibrary.get_pie_worlds(True)\nserver=ws[0]\nw=ws[1]\npc=unreal.GameplayStatics.get_player_controller(w,0)\ncam=pc.get_view_target()\nreg=next(s for s in unreal.ObjectIterator(unreal.GuLiSceneUISourceRegistry) if s.get_outer()==w)\nui=next(x for x in unreal.ObjectIterator(unreal.GuLiSceneUIWidget) if x.get_world()==w)")
    if '--laser' not in sys.argv:
        note('source_spawn',"unreal.SystemLibrary.execute_console_command(server,'guli.builder.Spawn red -20000 70000 0')\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'registry':json.loads(reg.get_registry_stats_json())}))")
        time.sleep(.4)
        note('source_registered',"probe=unreal.GameplayStatics.get_all_actors_of_class(server,unreal.GuLiConstructionVehiclePawn)[-1]\nlocal_pawn=pc.get_controlled_pawn()\noriginal_identity=local_pawn.player_state\nlocal_pawn.set_editor_property('player_state',None)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'registry':json.loads(reg.get_registry_stats_json()),'ui':json.loads(ui.get_frame_stats_json())}))")
        time.sleep(.15)
        note('source_identity_missing',"unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'ui':json.loads(ui.get_frame_stats_json())}))")
        note('source_late_identity',"local_pawn.set_editor_property('player_state',original_identity)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'identity':str(local_pawn.player_state)}))")
        time.sleep(.15)
        note('source_identity_restored',"unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'ui':json.loads(ui.get_frame_stats_json())}))")
        note('source_destroy',"probe.destroy_actor()\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'registry':json.loads(reg.get_registry_stats_json())}))")
        time.sleep(.4)
        note('source_unregistered',"unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'registry':json.loads(reg.get_registry_stats_json()),'ui':json.loads(ui.get_frame_stats_json())}))")
    note('mining_sources',"mining=[c for c in unreal.ObjectIterator(unreal.GuLiMiningPresentationComponent) if c.get_world()==w and json.loads(c.get_presentation_diagnostics_json()).get('presentation')]\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'sources':[{'owner':c.get_owner().get_name(),'diag':json.loads(c.get_presentation_diagnostics_json())} for c in mining]}))")
    result("m=mining[0]\nowner=m.get_owner()\norigin=owner.get_actor_location()\ntarget=origin+unreal.Vector(6000,0,0)\ncam.set_actor_location(origin+unreal.Vector(3000,-1500,800),False,True)\ncam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(cam.get_actor_location(),origin+unreal.Vector(3000,0,0)),True)\ncam.get_component_by_class(unreal.CameraComponent).set_editor_property('field_of_view',25)\nassert unreal.GuLiComponentSkillQALibrary.set_performance_mining_visual(owner,True,target)")
    time.sleep(.15)
    note('mining_midspan_visible',"unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'diag':json.loads(m.get_presentation_diagnostics_json())}))")
    result("cam.set_actor_location(origin+unreal.Vector(0,-100000,10000),False,True)\ncam.set_actor_rotation(unreal.Rotator(0,0,0),True)")
    time.sleep(.3)
    note('mining_suspended',"unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'diag':json.loads(m.get_presentation_diagnostics_json())}))")
    result("target=origin+unreal.Vector(5000,200,100)\nassert unreal.GuLiComponentSkillQALibrary.set_performance_mining_visual(owner,True,target)\ncam.set_actor_location(origin+unreal.Vector(3000,-1500,800),False,True)\ncam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(cam.get_actor_location(),origin+unreal.Vector(3000,0,0)),True)")
    time.sleep(.08)
    note('mining_restored_latest',"unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'diag':json.loads(m.get_presentation_diagnostics_json())}))")
    result("assert unreal.GuLiComponentSkillQALibrary.set_performance_mining_visual(owner,False,target)")
    note('mining_stopped',"unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'diag':json.loads(m.get_presentation_diagnostics_json())}))")
    note('flight_delivery',"sub=next(s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if s.get_outer()==w)\nrep=unreal.GameplayStatics.get_game_state(server).get_component_by_class(unreal.GuLiCombatEffectReplicationComponent)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'server':rep.get_flight_diagnostics(),'clients':[{'world':x.get_outer().get_path_name(),'active':x.get_active_visual_count(),'sources':[str(e.source.kind) for e in x.get_effect_states()]} for x in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if x.get_outer() in list(ws)[1:]]}))")
finally:
    stop()
    # Restore process-wide comparison controls to the recorded original values.
    original=json.loads((OUT/'benchmark-original-cvars.json').read_text())
    result("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"+'\n'.join(f'unreal.SystemLibrary.execute_console_command(w,{f"{k} {v:g}"!r})' for k,v in original.items()))
