"""Ground placement, asynchronous payment, authored builder tasks and owner state."""
import json
from pathlib import Path
import time
import uuid

def run(plan, args, records, read_state, write_json, initial):
    output = Path(plan['output'])
    clients = [e for e, _ in records if e['name'] != 'server' and e['participating']]
    ground = {team: next((e for e in clients if e['team'] == team and e['seat'] == 'Ground')) for team in ('Red', 'Blue')}
    commander = next((e for e in clients if e['team'] == 'Red' and e['seat'] == 'Commander'))
    balance_sources = ground
    factory_case = args.case.startswith('factory_construction')
    definition = getattr(args, 'building_definition', 6 if factory_case else 1)
    manifest = json.loads(Path(plan['manifest']).read_text(encoding='utf-8-sig'))
    cost = {color: int(float(manifest['numbers'][f'building/{definition}/Cost/' + color.capitalize()])) for color in ('red', 'blue')}
    funding = None
    if any((initial[balance_sources[team]['name']]['economy'][team][color] < amount for team in ('Red', 'Blue') for color, amount in cost.items())):
        from mining_acceptance import run as mine
        funding = mine(plan, args, records, read_state, write_json, initial, required_balance=cost)
        initial = json.loads((output / 'mining-final.json').read_text(encoding='utf-8-sig'))
        write_json(output / 'construction-funding.json', funding)
    states = initial
    identity = lambda v: v['stable_id']
    previous_buildings = {identity(v) for v in initial[commander['name']].get('building_states', [])}
    orders = []
    for team, number in (('Red', 1), ('Blue', 2)):
        pawn = next((v for v in initial[ground[team]['name']]['ground_states'] if v['team'] == number))
        x, y, z = pawn['position']
        orders.append({'operation': f'{team}-building-{definition}-1', 'player': ground[team]['player'], 'type': definition, 'x': x + 1200, 'y': y, 'z': z, 'yaw': 0})
    control = {'schema': 1, 'run_id': plan['run_id'], 'phase': 'gameplay', 'building_orders': orders}
    write_json(output / 'construction-baseline.json', initial)
    write_json(output / 'control.json', control)
    baseline_balance = {team: initial[balance_sources[team]['name']]['economy'][team] for team in ('Red', 'Blue')}
    baseline_payments = initial[balance_sources['Red']['name']].get('placement_payments')
    identities = None
    initial_epoch = initial[commander['name']].get('epoch')
    requested_at = None
    active_work = False
    occupied = False
    final_at = None
    timeline = []
    deadline = time.monotonic() + args.gameplay_timeout
    try:
        while time.monotonic() < deadline:
            current = {}
            for e, p in records:
                if p.poll() is not None:
                    raise RuntimeError(f"{e['name']} exited during construction")
                s = read_state(Path(e['directory']) / 'state.json', plan['run_id'], p.pid)
                if s is None:
                    break
                current[e['name']] = s
            if len(current) != len(records):
                time.sleep(0.2)
                continue
            states = current
            for e in clients:
                if states[e['name']].get('failure'):
                    raise RuntimeError(f"{e['name']}: {states[e['name']]['failure']}")
            if any((not states[e['name']].get('ready') or states[e['name']].get('paused') for e in clients)):
                timeline.append({'monotonic_seconds': time.monotonic(), 'paused': True})
                time.sleep(0.2)
                continue
            view = states[commander['name']].get('building_states', [])
            buildings = [v for v in view if v['definition'] == definition and identity(v) not in previous_buildings and (v['builder'].lower() in {e['player'].lower() for e in ground.values()})]
            if len(buildings) > 2:
                raise RuntimeError('A production placement produced duplicate construction identities')
            if len(buildings) == 2:
                keys = {v['team']: identity(v) for v in buildings}
                if identities is None:
                    identities = keys
                    write_json(output / 'construction-admitted.json', states)
                if keys != identities:
                    raise RuntimeError('Construction recovery changed its stable object identities')
                owners = {key: [] for key in identities.values()}
                for entry in clients:
                    for value in states[entry['name']].get('building_states', []):
                        key = identity(value)
                        if key in owners and value['local_simulation']:
                            owners[key].append(entry['name'])
                if any((len(value) != 1 for value in owners.values())):
                    raise RuntimeError('A constructed building is missing its unique simulator')
                for v in buildings:
                    active_work |= v['active_builders'] and 0 < v['work_done'] < v['work_required']
                    occupied |= v['occupied_slots'] > 0
                complete = all((v['phase'] == 1 and v['work_done'] == v['work_required'] for v in buildings))
                consistent = True
                for e in clients:
                    replicas = {v['team']: v for v in states[e['name']].get('building_states', []) if identity(v) in identities.values()}
                    consistent &= set(replicas) == set(identities) and all((v['phase'] == 1 for v in replicas.values()))
                if complete and consistent and active_work and occupied:
                    if final_at is None:
                        final_at = time.monotonic()
                    if time.monotonic() - final_at >= 2:
                        break
                else:
                    final_at = None
            timeline.append({'monotonic_seconds': time.monotonic(), 'buildings': buildings, 'epochs': {e['name']: states[e['name']].get('epoch') for e in clients}, 'economy': {team: states[balance_sources[team]['name']]['economy'][team] for team in ('Red', 'Blue')}})
            time.sleep(0.2)
        else:
            raise RuntimeError('Formal Ground placement/building-owner work did not reach two consistent committed completions')
        for team in ('Red', 'Blue'):
            before = baseline_balance[team]
            after = states[balance_sources[team]['name']]['economy'][team]
            for color in ('red', 'blue'):
                if before[color] - after[color] != cost[color]:
                    raise RuntimeError(f'{team} payment was not exactly one formal building cost: {before} -> {after}')
        write_json(output / 'construction-final.json', states)
        if factory_case:
            required = {identity(value).lower() for value in buildings}
            factory_view = states[commander['name']]['factory_states']
            factory_identity = lambda v: v['stable_id']
            if not required.issubset({factory_identity(value).lower() for value in factory_view}):
                raise RuntimeError('Completed factory identities are missing from the production resource scene')
            for client in clients:
                book = states[client['name']].get('factory_states', [])
                if not required.issubset({identity(value).replace('-', '').lower() for value in book}):
                    raise RuntimeError('Dynamic factory admission did not replicate its FIFO inventory')
        return {'status': 'passed_construction', 'measurement_valid': False, 'scope': 'Formal Ground placement, one resource payment, production StateTree builder work and committed ' + ('resource factory' if factory_case else 'turret') + ' completion' + '' + ' only; no factory processing/turret combat/full-gameplay/performance pass', 'buildings': 2, 'definition': definition, 'samples': len(timeline), 'funding': funding}
    except Exception as error:
        write_json(output / 'construction-failure.json', {'failure': str(error), 'states': states})
        raise
    finally:
        write_json(output / 'construction-timeline.json', {'schema': 1, 'run_id': plan['run_id'], 'samples': timeline})
