"""Explicit saved-map acceptance entry, after gs.Flights.PIE has finished loading.

Uses the real Air pawn and confirmed Hangar node 08; starts the native 500-flight
fixture after its authority source is registered. Does not modify saved assets.
"""
import json
import time
from pathlib import Path
import unreal

worlds = unreal.EditorLevelLibrary.get_pie_worlds(True)
if not worlds or not all('UEDPIE_' in w.get_path_name() for w in worlds):
    raise RuntimeError('Start gs.Flights.PIE and wait for all four clients to load first')
server = worlds[0]
marker = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.Actor)
              if unreal.Name('FlightEventsQAOrigin') in a.tags)
ship = unreal.GameplayStatics.get_all_actors_of_class(server, unreal.GuLiStrikeShip)[0]
if not ship.get_hangar_capability():
    origin = marker.get_actor_location()
    ship.set_actor_location(unreal.Vector(origin.x, origin.y, origin.z + 9100), False, True)
    reason = unreal.GuLiComponentSkillQALibrary.commit_ship_choice(
        ship.player_state.get_component_by_class(unreal.GuLiShipBuildComponent), '08')
    if reason is None:
        raise RuntimeError('Real confirmed Hangar choice failed')
start_wait = time.monotonic()


def prepare_tick(delta):
    rows = json.loads(unreal.GuLiComponentSkillQALibrary.wingman_snapshot(server))
    ready = any(r['lifecycle'] == 2 and r['config_usable'] for r in rows)
    if not ready and time.monotonic() - start_wait < 15:
        return
    unreal.unregister_slate_post_tick_callback(prepare_handle)
    fixture = next(s for s in unreal.ObjectIterator(unreal.GuLiFlightAcceptanceSubsystem)
                   if s.get_outer().get_path_name() == server.get_path_name())
    started = ready and fixture.start_load(500, 30)
    result = {'started': bool(started), 'wingman': rows, 'requested_flights': 500,
              'duration_game_seconds': 30, 'reason': '' if started else 'No live three-domain source; start a fresh mixed PIE'}
    Path('D:/UE5.7/test1/TestResults/FlightEvents20261004/prepared_sources.json').write_text(
        json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('Flight acceptance prepared: ' + json.dumps(result, ensure_ascii=False))


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
prepare_handle = unreal.register_slate_post_tick_callback(prepare_tick)
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'preparation_queued': True}))
