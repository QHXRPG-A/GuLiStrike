"""Drive the real Ground input/weapon and verify committed health on all clients."""
from __future__ import annotations
import math
from pathlib import Path
import time
import uuid

def run(plan, args, records, read_state, write_json, initial):
    output = Path(plan['output'])
    clients = [e for e, _ in records if e['name'] != 'server' and e['participating']]
    shooter = next((e for e in clients if e['team'] == 'Red' and e['seat'] == 'Ground'), None)
    target = next((e for e in clients if e['team'] == 'Blue' and e['seat'] == 'Ground'), None)
    if not shooter or not target:
        raise RuntimeError('Ground combat requires both team Ground seats')
    states = initial
    timeline = []

    def ground(state, team):
        found = [v for v in state.get('ground_states', []) if v.get('team') == team]
        if len(found) != 1 or found[0].get('max_health', 0) <= 0 or (not int(found[0].get('health_target', '0'), 16)):
            raise RuntimeError('The real Ground health target/configuration is missing or ambiguous')
        return found[0]
    source = ground(states[shooter['name']], 1)
    enemy = ground(states[shooter['name']], 2)
    target_id = enemy['health_target']
    initial_health = enemy['health']
    initial_shots = source['shots_fired']
    minimum_health = initial_health
    initial_position = source['position']
    write_json(output / 'ground-combat-baseline.json', states)
    import json
    manifest = json.loads(Path(plan['manifest']).read_text(encoding='utf-8-sig'))
    mass = [v for v in manifest['objects'] if v['kind'] == 1]
    corridor_x = max((float(v['x']) for v in mass)) + 4000
    center_y = (source['position'][1] + enemy['position'][1]) / 2
    routes = {shooter['name']: [[corridor_x, source['position'][1]], [corridor_x, center_y], [-2000, center_y]], target['name']: [[corridor_x, enemy['position'][1]], [corridor_x, center_y], [2000, center_y]]}
    route_index = {name: 0 for name in routes}
    input_start = {e['name']: states[e['name']]['logical_tick'] for e in (shooter, target)}
    write_json(output / 'ground-combat-route.json', {'schema': 1, 'routes': routes, 'rocket_period_ticks': 600, 'rocket_held_ticks': 180, 'waypoint_tolerance_cm': 600})
    deadline = time.monotonic() + args.gameplay_timeout
    stage = 'approach'
    terminal_at = None
    initial_epoch = states[shooter['name']].get('epoch')
    try:
        while time.monotonic() < deadline:
            current = {}
            for entry, process in records:
                if process.poll() is not None:
                    raise RuntimeError(f"{entry['name']} exited during Ground combat")
                value = read_state(Path(entry['directory']) / 'state.json', plan['run_id'], process.pid)
                if value is None:
                    break
                current[entry['name']] = value
            if len(current) != len(records):
                time.sleep(0.2)
                continue
            states = current
            source = ground(states[shooter['name']], 1)
            enemy = ground(states[shooter['name']], 2)
            if enemy['health_target'] != target_id:
                raise RuntimeError('Ground combat changed the target life identity')
            if any((states[e['name']].get('epoch') != initial_epoch for e in clients)):
                raise RuntimeError('Ground combat changed delivery epoch without membership change')
            minimum_health = min(minimum_health, enemy['health'])
            dx = enemy['position'][0] - source['position'][0]
            dy = enemy['position'][1] - source['position'][1]
            distance = math.hypot(dx, dy)
            fire = enemy['alive'] and distance <= 4500
            stage = 'fire' if fire else 'approach' if enemy['alive'] else 'confirm_death'
            inputs = []
            for entry, actor, aim, held in ((shooter, source, enemy['position'], fire), (target, enemy, source['position'], False)):
                name = entry['name']
                index = route_index[name]
                while index < len(routes[name]) and math.dist(actor['position'][:2], routes[name][index]) <= 600:
                    index += 1
                route_index[name] = index
                pursuing = entry is shooter and index == len(routes[name]) and enemy['alive'] and not held
                moving = (index < len(routes[name]) or pursuing) and enemy['alive'] and (not held)
                if moving:
                    wx, wy = enemy['position'][:2] if pursuing else routes[name][index]
                    offset = (wx - actor['position'][0], wy - actor['position'][1])
                    length = math.hypot(*offset)
                    move = (offset[1] / length, offset[0] / length)
                else:
                    move = (0, 0)
                elapsed = max(0, states[name]['logical_tick'] - input_start[name])
                # The authored route can meet terrain on any leg. Continue
                # using the real periodic thrust input, including pursuit of
                # an opponent that has not reached its planned meeting point.
                rocket = moving and (elapsed % 600 < 180)
                inputs.append({'player': entry['player'], 'move_x': move[0], 'move_y': move[1], 'aim_x': aim[0], 'aim_y': aim[1], 'aim_z': aim[2], 'sprint': True, 'fire': held, 'rocket': rocket})
            control = {'schema': 1, 'run_id': plan['run_id'], 'phase': 'ground_combat', 'ground_inputs': inputs}
            write_json(output / 'control.json', control)
            timeline.append({'monotonic_seconds': time.monotonic(), 'stage': stage, 'distance_cm': distance, 'shots': source['shots_fired'], 'health': enemy['health'], 'health_revision': enemy['health_revision'], 'position': source['position'], 'target_position': enemy['position'], 'fuel': source['fuel'], 'route_indices': dict(route_index), 'inputs': inputs, 'coordinator': [{key: room.get(key) for key in ('room_phase', 'membership_paused', 'handoffs')} for room in states['server'].get('rooms', [])], 'epochs': {e['name']: states[e['name']].get('epoch') for e in clients}})
            if not enemy['alive']:
                if terminal_at is None:
                    terminal_at = time.monotonic()
                    write_json(output / 'ground-combat-terminal.json', states)
                confirmed = [ground(states[e['name']], 2) for e in clients]
                if all((v['health_target'] == target_id and v['health'] == 0 and (not v['alive']) for v in confirmed)):
                    if time.monotonic() - terminal_at >= 2:
                        break
            time.sleep(0.2)
        else:
            raise RuntimeError('The production Ground input/weapon did not reach a committed death on all clients')
        if minimum_health >= initial_health or source['shots_fired'] <= initial_shots:
            raise RuntimeError('Ground death has no real weapon/health evidence')
        if getattr(args, 'history_route_required', True) and math.dist(initial_position, source['position']) <= 100:
            raise RuntimeError('Ground combat did not use the real movement input')
        if states[shooter['name']].get('scene_ground_inputs_accepted', 0) == 0:
            raise RuntimeError('The Ground production semantic input did not execute')
        write_json(output / 'ground-combat-final.json', states)
        return {'status': 'passed_ground_combat', 'measurement_valid': False, 'scope': 'Ground production movement/fire, ledger death and all-client committed health' + '' + ' only; no full-gameplay/performance pass', 'initial_health': initial_health, 'target': target_id, 'shots': source['shots_fired'] - initial_shots, 'samples': len(timeline)}
    except Exception as error:
        write_json(output / 'ground-combat-failure.json', {'stage': stage, 'failure': str(error), 'states': states})
        raise
    finally:
        write_json(output / 'ground-combat-timeline.json', {'schema': 1, 'run_id': plan['run_id'], 'samples': timeline})
