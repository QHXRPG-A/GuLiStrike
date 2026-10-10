"""Authorized functional PIE readback through existing gameplay/lifecycle entry points.

This run is separate from fixed-view performance samples. It uses transient
construction samples, real possession/respawn and the editor's native late join.
"""
from __future__ import annotations
import json, time
from run_four_stage_review import OUT, run, result, launch, stop, COUNTERS, LOCK_VIEW

events=[]
def note(name,code):
    value=run(code);events.append({'name':name,'value':value})
    (OUT/'runtime-lifecycle-final-review.json').write_text(json.dumps(events,indent=2),encoding='utf-8')
    print(json.dumps({'name':name,'value':value}),flush=True);return value

def wait_for(code,key,seconds=30):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        value=run(code)
        if value.get(key):return value
        time.sleep(.25)
    events.append({'name':'readiness_failure','value':value})
    (OUT/'runtime-lifecycle-final-review.json').write_text(json.dumps(events,indent=2),encoding='utf-8')
    raise RuntimeError('Readback not ready: '+str(value))

try:
    launch('flight','data_pool')
    result("ws=list(unreal.EditorLevelLibrary.get_pie_worlds(True))\nserver=ws[0]\nw=ws[1]\npc=unreal.GameplayStatics.get_player_controller(w,0)\ncam=pc.get_view_target()\nflight=next(s for s in unreal.ObjectIterator(unreal.GuLiFlightAcceptanceSubsystem) if s.get_outer()==server)")
    # Verify that the loaded source fix restores the independent 5/30 Hz domains.
    result("perf=next(s for s in unreal.ObjectIterator(unreal.GuLiPerformanceSubsystem) if s.get_outer()==server)\nperf.begin_capture()")
    time.sleep(3)
    note('server_domain_rates',"perf.end_capture()\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'profile':json.loads(perf.get_capture_json())}))")
    note('flight_first_load',COUNTERS)
    # Native replenishment naturally reuses retired slots; restarting a still-live
    # fixture would leave old authoritative recipes and invalidate the 500 count.
    time.sleep(3)
    note('flight_reused_load',COUNTERS)
    # The shared bridge retains Python globals; COUNTERS visits every World.
    # Restore the selected local client explicitly before manipulating its view.
    result("ws=list(unreal.EditorLevelLibrary.get_pie_worlds(True))\nserver=ws[0]\nw=ws[1]\npc=unreal.GameplayStatics.get_player_controller(w,0)\ncam=pc.get_view_target()")

    # Construction authority and client state use the original work/replication path.
    note('construction_spawn',"assert unreal.GuLiConstructionReviewLibrary.fund_review(server)\nbuilding=unreal.GuLiConstructionReviewLibrary.spawn_sample(server,6,unreal.Vector(15000,72000,901.8),0,False)\nassert building\nlifecycle=building.get_component_by_class(unreal.GuLiBuildingLifecycleComponent)\nunreal.SystemLibrary.execute_console_command(server,'guli.builder.Spawn red 17000 72000 901.8')\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'building':building.get_name(),'state':str(lifecycle.get_state())}))")
    wait_for("builders=list(unreal.GameplayStatics.get_all_actors_of_class(server,unreal.GuLiConstructionVehiclePawn))\nready=lifecycle.are_construction_slots_ready() and len(lifecycle.get_construction_slot_poses())>0 and bool(builders)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'ready':ready,'slots':len(lifecycle.get_construction_slot_poses()),'nav_building':unreal.NavigationSystemV1.is_navigation_being_built_or_locked(server),'world_time':unreal.GameplayStatics.get_time_seconds(server)}))",'ready',120)
    note('construction_order',"builder=builders[-1]\nassert builder.issue_construction(lifecycle,False)\nbuilder_name=builder.get_name()\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'builder':builder_name}))")
    wait_for("client_builder=next((a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiConstructionVehiclePawn) if a.get_name()==builder_name),None)\nconstruction=client_builder.get_component_by_class(unreal.GuLiConstructionPresentationComponent) if client_builder else None\nd=json.loads(construction.get_presentation_diagnostics_json()) if construction else {}\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'ready':d.get('active',False),'diag':d}))",'ready',45)
    result("center=(client_builder.get_actor_location()+building.get_actor_location())*.5\ncam.set_actor_location(center+unreal.Vector(0,-4000,2500),False,True)\ncam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(cam.get_actor_location(),center),True)\ncam.get_component_by_class(unreal.CameraComponent).set_editor_property('field_of_view',35)")
    result(LOCK_VIEW+"\nw=ws[1]\npc=unreal.GameplayStatics.get_player_controller(w,0)\ncam=pc.get_view_target()")
    time.sleep(.15)
    wait_for("d=json.loads(construction.get_presentation_diagnostics_json())\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'ready':d['showing'],'diag':d,'view_target':pc.get_view_target().get_actor_label(),'camera':str(pc.player_camera_manager.get_camera_location())}))",'ready',5)
    note('construction_visible',"work=builder.get_component_by_class(unreal.GuLiConstructionWorkComponent)\nwork.set_component_tick_enabled(False)\nlifecycle.set_component_tick_enabled(False)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'diag':json.loads(construction.get_presentation_diagnostics_json())}))")
    result("cam.set_actor_location(center+unreal.Vector(0,-100000,10000),False,True)\ncam.set_actor_rotation(unreal.Rotator(0,0,0),True)")
    time.sleep(.3)
    note('construction_suspended',"unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'diag':json.loads(construction.get_presentation_diagnostics_json())}))")
    result("cam.set_actor_location(center+unreal.Vector(0,-4000,2500),False,True)\ncam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(cam.get_actor_location(),center),True)")
    time.sleep(.08)
    note('construction_restored',"unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'diag':json.loads(construction.get_presentation_diagnostics_json())}))")
    result('assert unreal.GuLiConstructionReviewLibrary.advance_sample(lifecycle,1.0)\nwork.set_component_tick_enabled(True)')
    time.sleep(.25)
    note('construction_completed_stop',"unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'phase':str(lifecycle.get_state().phase),'diag':json.loads(construction.get_presentation_diagnostics_json())}))")

    # Air client owns the existing Ship panel nodes. Read actual node transforms/textures.
    panels="r={'success':True,'worlds':[]}\nfor cw in unreal.EditorLevelLibrary.get_pie_worlds(True)[1:]:\n cp=unreal.GameplayStatics.get_player_controller(cw,0)\n pawn=cp.get_controlled_pawn()\n reg=next(s for s in unreal.ObjectIterator(unreal.GuLiSceneUISourceRegistry) if s.get_outer()==cw)\n ui=next((s for s in unreal.ObjectIterator(unreal.GuLiSceneUIWidget) if s.get_world()==cw),None)\n nodes=[]\n if pawn:\n  for node in pawn.get_components_by_class(unreal.WidgetComponent):nodes.append({'name':node.get_name(),'visible':node.is_visible(),'transform':str(node.get_world_transform()),'texture':str(node.get_render_target())})\n r['worlds'].append({'world':cw.get_path_name(),'pawn':pawn.get_name() if pawn else None,'nodes':nodes,'registry':json.loads(reg.get_registry_stats_json()),'ui':json.loads(ui.get_frame_stats_json()) if ui else None})\nunreal.MCPythonHelper.submit_result(json.dumps(r))"
    note('ship_panels_before',panels)
    note('ship_unpossess',"ship=next(s for s in unreal.GameplayStatics.get_all_actors_of_class(server,unreal.GuLiStrikeShip) if s.get_controller())\nship_controller=ship.get_controller()\nship_controller.un_possess()\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'old_ship':ship.get_name()}))")
    time.sleep(.3)
    note('ship_panels_unpossessed',panels)
    result('ship_controller.possess(ship)')
    time.sleep(.4)
    note('ship_panels_repossessed',panels)
    note('ship_respawn',"old_ship_name=ship.get_name()\nship_controller.un_possess()\nship.destroy_actor()\nunreal.GameplayStatics.get_game_mode(server).restart_player(ship_controller)\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'old_ship':old_ship_name}))")
    wait_for("new_ship=ship_controller.get_controlled_pawn()\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'ready':bool(new_ship) and new_ship.get_name()!=old_ship_name,'pawn':new_ship.get_name() if new_ship else None}))",'ready',10)
    time.sleep(.8)
    note('ship_panels_after_respawn',panels)

    # Late join receives the server's live recipes via the existing bootstrap path.
    stop()
    launch('flight','data_pool')
    result("ws=list(unreal.EditorLevelLibrary.get_pie_worlds(True))\nserver=ws[0]\nassert unreal.GuLiConstructionReviewLibrary.join_review_client()")
    wait_for("worlds=list(unreal.EditorLevelLibrary.get_pie_worlds(True))\nunreal.MCPythonHelper.submit_result(json.dumps({'success':True,'ready':len(worlds)==4 and bool(unreal.GameplayStatics.get_player_controller(worlds[-1],0)),'worlds':len(worlds)}))",'ready',30)
    time.sleep(3)
    note('late_join_flights',COUNTERS)
    note('late_join_sources',panels)
finally:
    stop()
    original=json.loads((OUT/'benchmark-original-cvars.json').read_text())
    result("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"+'\n'.join(f'unreal.SystemLibrary.execute_console_command(w,{f"{k} {v:g}"!r})' for k,v in original.items()))
