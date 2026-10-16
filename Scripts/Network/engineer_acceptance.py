"""Production commander selection/task input and engineer navigation."""
from pathlib import Path
import math
import time
import uuid
import json
from runtime_evidence import pending_object_handoffs

def run(plan, args, records, read_state, write_json, initial):
    output = Path(plan['output'])
    clients = [entry for entry, _ in records if entry['name'] != 'server' and entry['participating']]
    commanders = {team: next((e for e in clients if e['team'] == team and e['seat'] == 'Commander')) for team in ('Red', 'Blue')}
    baseline = initial
    reference = commanders['Red']
    manifest=json.loads(Path(plan['manifest']).read_text(encoding='utf-8-sig'))
    expected={o['id'].replace('-','').lower() for o in manifest['objects']
              if manifest['strings'].get('adapter/definition/'+o['definition'])=='Engineer'}
    def cohort(value):
        return [v for v in value.get('engineer_states',[]) if v['stable_id'].replace('-','').lower() in expected]
    base = cohort(baseline[reference['name']])
    if len(base) != 16 or len({v['ordinal'] for v in base}) != 16:
        raise RuntimeError('The formal startup must contain exactly sixteen distinct engineering actors')
    identity = lambda v: v['stable_id']
    identities = {v['ordinal']: identity(v) for v in base}
    positions = {v['ordinal']: v['position'] for v in base}
    owners = {}
    for e in clients:
        for v in cohort(baseline[e['name']]):
            if v['local_simulation']:
                if v['ordinal'] in owners:
                    raise RuntimeError('An engineering object has multiple simulators')
                owners[v['ordinal']] = e['name']
    if set(owners) != set(identities):
        raise RuntimeError('Engineering grants do not cover the real actors')
    command_ids = {team: getattr(args, 'command_base', 0) + value for team, value in (('Red', 10001), ('Blue', 10002))}
    orders = []
    for team, number in (('Red', 1), ('Blue', 2)):
        members = [v for v in base if v['team'] == number]
        center = [sum((v['position'][i] for v in members)) / len(members) for i in range(3)]
        center[1] += -6000 if number == 1 else 6000
        orders.append({'player': commanders[team]['player'], 'command': command_ids[team], 'actors': sorted((v['ordinal'] for v in members)), 'x': center[0], 'y': center[1], 'z': center[2]})
    control = {'schema': 1, 'run_id': plan['run_id'], 'phase': 'gameplay', 'engineer_orders': orders}
    write_json(output / 'engineer-baseline.json', baseline)
    write_json(output / 'control.json', control)
    states = initial
    timeline = []
    initial_epoch = initial[commanders['Red']['name']].get('epoch')
    requested_at = None
    settled_at = None
    start_counts = {e['name']: initial[e['name']].get('executed_engineer_commands', 0) for e in clients}
    deadline = time.monotonic() + args.gameplay_timeout
    try:
        while time.monotonic() < deadline:
            current = {}
            for e, p in records:
                if p.poll() is not None:
                    raise RuntimeError(f"{e['name']} exited during engineering acceptance")
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
            if any((s.get('paused') or not s.get('world_ready') for name, s in states.items() if name != 'server')):
                timeline.append({'monotonic_seconds': time.monotonic(), 'paused': True, 'epochs': {e['name']: states[e['name']].get('epoch') for e in clients}})
                time.sleep(0.2)
                continue
            for e in clients:
                objects = cohort(states[e['name']])
                if {v['ordinal']: identity(v) for v in objects} != identities:
                    raise RuntimeError('Engineering input changed or omitted a stable object identity')
            # Selection starts a real priority handoff. Its export/import boundary
            # temporarily has no simulator; the exact-one assertion applies after
            # these engineers' grants agree, without waiting for unrelated objects.
            changing = pending_object_handoffs([states[e['name']] for e in clients], identities.values())
            if changing:
                timeline.append({'monotonic_seconds': time.monotonic(), 'handoff_objects': sorted(changing),
                                 'scope': 'Awaiting this command cohort ownership; world continues'})
                settled_at = None
                time.sleep(0.2)
                continue
            actual = {}
            entries = clients
            for e in entries:
                for v in cohort(states[e['name']]):
                    if v['local_simulation']:
                        if v['ordinal'] in actual:
                            raise RuntimeError('Duplicate active engineering simulator')
                        actual[v['ordinal']] = v
            if set(actual) != set(identities):
                raise RuntimeError('The active engineering population is incomplete')
            distances = {ordinal: math.dist(positions[ordinal], v['position']) for ordinal, v in actual.items()}
            resolved = []
            for team in ('Red', 'Blue'):
                receipts = states[commanders[team]['name']].get('engineer_orders', [])
                receipt = next((v for v in receipts if v['command'] == command_ids[team]), None)
                resolved.append(bool(receipt and receipt['resolved'] and (set(receipt['accepted_actors']) == set(orders[0 if team == 'Red' else 1]['actors']))))
                if receipt and receipt['resolved'] and (not resolved[-1]):
                    raise RuntimeError(f'{team} production engineering task was rejected: {receipt}')
            timeline.append({'monotonic_seconds': time.monotonic(), 'distances_cm': distances, 'move_status': {o: v['move_status'] for o, v in actual.items()}, 'active_command': {o: v['active_command'] for o, v in actual.items()}, 'epochs': {e['name']: states[e['name']].get('epoch') for e in clients}})
            enough = all(resolved) and min(distances.values()) > 1000
            if enough:
                if settled_at is None:
                    settled_at = time.monotonic()
                if time.monotonic() - settled_at >= 3:
                    break
            else:
                settled_at = None
            time.sleep(0.2)
        else:
            raise RuntimeError('The formal engineering selection/task did not move every object')
        executed = sum((states[e['name']].get('executed_engineer_commands', 0) - start_counts[e['name']] for e in clients))
        if executed != 16:
            raise RuntimeError(f'Engineering commands executed {executed} times for sixteen objects')
        write_json(output / 'engineer-final.json', states)
        return {'status': 'passed_engineer', 'measurement_valid': False, 'scope': 'Six-client formal engineering factory, commander selection/task, cross-owner navigation' + '' + ' only; no construction/full-gameplay/performance pass', 'objects': 16, 'samples': len(timeline)}
    except Exception as error:
        write_json(output / 'engineer-failure.json', {'failure': str(error), 'states': states})
        raise
    finally:
        write_json(output / 'engineer-timeline.json', {'schema': 1, 'run_id': plan['run_id'], 'samples': timeline})
