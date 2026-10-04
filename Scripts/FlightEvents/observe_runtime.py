"""Bounded observations for the user-authorized flight/budget acceptance.

Load through commander_editor_python. Loading starts no PIE or benchmark.
"""
import json
import time
import traceback
from pathlib import Path
import unreal

flight_reports = Path('D:/UE5.7/test1/TestResults/FlightEvents20261004')
flight_run = {'done': True}


def flight_subsystem(world, cls):
    path = world.get_path_name()
    return next((s for s in unreal.ObjectIterator(cls)
                 if s.get_outer().get_path_name() == path), None)


def flight_sample():
    rows = []
    for world in unreal.EditorLevelLibrary.get_pie_worlds(True):
        gs = unreal.GameplayStatics.get_game_state(world)
        channel = gs.get_component_by_class(unreal.GuLiCombatEffectReplicationComponent)
        sub = flight_subsystem(world, unreal.GuLiCombatEffectPresentationSubsystem)
        counters = sub.get_counters() if sub else None
        rows.append({'world': world.get_path_name(),
                     'time': unreal.GameplayStatics.get_time_seconds(world),
                     'flights': channel.get_flight_diagnostics(),
                     'pool_capacity': counters.client_flight_actor_capacity if counters else 0,
                     'pool_active': counters.client_flight_actor_active if counters else 0,
                     'visual_cpu_ms': counters.last_update_milliseconds if counters else 0,
                     'received_states': counters.received_states if counters else 0,
                     'rejected_states': counters.rejected_states if counters else 0,
                     'roles': [str(pc.player_state.get_battle_role()) for pc in
                               unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerController)
                               if pc.player_state]})
    return rows


def flight_observe(label, seconds=30, commands=(), capture_network=True):
    global flight_run
    if not flight_run.get('done', True):
        raise RuntimeError('A bounded observation is already active')
    worlds = unreal.EditorLevelLibrary.get_pie_worlds(True)
    if not worlds:
        raise RuntimeError('PIE must already be running')
    server = worlds[0]
    for command in commands:
        unreal.SystemLibrary.execute_console_command(server, command)
    if capture_network:
        unreal.SystemLibrary.execute_console_command(server, 'netprofile enable')
    flight_run = {'label': label, 'done': False, 'seconds': seconds,
                  'start_wall': time.monotonic(), 'start_game': unreal.GameplayStatics.get_time_seconds(server),
                  'samples': [], 'frames': [], 'next_sample': 0,
                  'capture_network': capture_network, 'commands': list(commands)}

    def tick(delta):
        elapsed = time.monotonic() - flight_run['start_wall']
        try:
            flight_run['frames'].append(float(delta))
            if elapsed >= flight_run['next_sample']:
                flight_run['samples'].append({'elapsed': elapsed, 'worlds': flight_sample()})
                flight_run['next_sample'] = elapsed + .2
            if elapsed < seconds:
                return
        except Exception:
            flight_run['error'] = traceback.format_exc()
        if capture_network:
            unreal.SystemLibrary.execute_console_command(server, 'netprofile disable')
        flight_run['elapsed_wall'] = elapsed
        flight_run['elapsed_game'] = unreal.GameplayStatics.get_time_seconds(server) - flight_run['start_game']
        flight_run['done'] = True
        unreal.unregister_slate_post_tick_callback(flight_run['handle'])
        report = {k: v for k, v in flight_run.items() if k != 'handle'}
        (flight_reports / (label + '.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')

    flight_run['handle'] = unreal.register_slate_post_tick_callback(tick)
    return {'label': label, 'seconds': seconds, 'queued': True}


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'observer_loaded': True}))
