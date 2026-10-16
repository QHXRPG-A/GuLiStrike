"""Production equipment selection, controller-owned members and owner handoff."""
from __future__ import annotations
from pathlib import Path
import time

def run(plan, args, records, read_state, write_json, states):
    output = Path(plan['output'])
    endpoints = [(entry, process) for entry, process in records if entry['name'] != 'server' and entry['participating']]
    air = [entry for entry, _ in endpoints if entry['seat'] == 'Air']
    stages = []

    def control(phase, **extra):
        write_json(output / 'control.json', {'schema': 1, 'run_id': plan['run_id'], 'phase': phase, **extra})

    def refresh():
        for entry, process in records:
            if process.poll() is not None:
                raise RuntimeError(f"{entry['name']} exited during Wingman review")
            value = read_state(Path(entry['directory']) / 'state.json', plan['run_id'], process.pid)
            if value:
                states[entry['name']] = value

    def live(state):
        return {(obj['stable_id'], obj['life']): obj for obj in state.get('wingman_states', []) if obj['life_state'] == 1}

    def wait(label, predicate):
        preparation = getattr(args,'continue_remaining',False) and label == 'all-members'
        deadline = time.monotonic() + (args.preparation_timeout if preparation else args.gameplay_timeout)
        while time.monotonic() < deadline:
            refresh()
            if preparation and any(o['definition']=='player/ship' and o['life']>=2
                                   for entry,_ in endpoints for o in states.get(entry['name'],{}).get('scene_objects',[])):
                from acceptance_session import PreparationSkipped
                write_json(output / 'wingman-preparation-lost-carrier.json', states)
                raise PreparationSkipped('A Ship died before the equipment/member fixture was complete')
            if all((entry['name'] in states for entry, _ in endpoints)) and predicate():
                write_json(output / f'wingman-{len(stages) + 1:02d}-{label}.json', states)
                stages.append(label)
                return
            time.sleep(0.25)
        write_json(output / f'wingman-failed-{label}.json', states)
        if preparation:
            from acceptance_session import PreparationSkipped
            raise PreparationSkipped('The actual fifty-member equipment fixture was not ready within its short setup window')
        raise RuntimeError(f'Production Wingman review timed out at {label}')
    if plan['formation'] != '3v3' or len(air) != 2:
        raise RuntimeError('The formal member recovery case requires Hybrid and six active seats')
    prepared=getattr(args,'prepared_equipment',None)
    if prepared:
        by_player={record['player']:record for record in prepared}
        for entry in air:
            receipt=by_player.get(entry['player'])
            if not receipt or not receipt['reply']['result'].get('committed') or not Path(receipt['evidence']).is_file() \
                    or states[entry['name']].get('scene_ui',{}).get('ship_build_revision',0)<receipt['reply']['result']['revision']:
                raise RuntimeError('The original Hangar receipt does not match the current fixed player/build')
        write_json(output/'wingman-01-equipment-committed.json',{'prepared_receipts':prepared,'states':states})
        stages.append('equipment-committed')
    else:
        control('ship_equipment')
        wait('equipment-committed', lambda: all((states[entry['name']].get('ship_equipment_complete') for entry in air)))
        for entry in air:
            if not states[entry['name']].get('ship_equipment_committed'):
                raise RuntimeError('A production Hangar choice did not commit')
    expected = 25 * len(air)
    wait('all-members', lambda: all((len(live(states[entry['name']])) == expected and all((member['entity'] for member in live(states[entry['name']]).values())) for entry, _ in endpoints)))
    before = live(states[endpoints[0][0]['name']])
    epoch = states[endpoints[0][0]['name']]['epoch']
    control('ship_movement')
    wait('movement', lambda: all((states[entry['name']].get('ship_complete') for entry in air)))
    control('prepare')
    wait('settled', lambda: all((states[entry['name']].get('ready') and len(live(states[entry['name']])) == expected and all((member['entity'] and (member.get('published', 0) >= 20 if member['local_simulation'] else member.get('received', 0) >= 1) for member in live(states[entry['name']]).values())) for entry, _ in endpoints)))
    views = [live(states[entry['name']]) for entry, _ in endpoints]
    if any((set(view) != set(before) for view in views)):
        raise RuntimeError('Ordinary recovery changed stable member identity or lifetime')
    for key, original in before.items():
        copies = [view[key] for view in views]
        owners = [copy for copy in copies if copy['local_simulation']]
        if len(owners) != 1:
            raise RuntimeError('Member has a missing or duplicate simulator')
        owner = owners[0]
        ship_owners = {ship['controller'] for entry, _ in endpoints for ship in states[entry['name']].get('ship_states', []) if ship['stable_id'] == original['parent']}
        if ship_owners != {owner['controller']} or any((copy['controller'] != owner['controller'] or copy['simulator'] != owner['controller'] or copy['dormant'] for copy in copies)):
            raise RuntimeError('Member simulation/synchronization belongs to a different process from Ship control')
        if owner['cadence_errors'] or owner['simulation_samples'] < owner['published'] * 3 - 3:
            raise RuntimeError('Native member simulation does not follow 30 Hz / 10 Hz cadence')
        if owner['last_publish_tick'] - owner['first_publish_tick'] != (owner['published'] - 1) * 6:
            raise RuntimeError('Motion was not published every six logical ticks')
        for copy in copies:
            if not copy['local_simulation'] and (copy['simulation_samples'] or copy['published']):
                raise RuntimeError('A query entity simulated or loop-published received state')
            age = abs(copy['pose_tick'] - owner['pose_tick'])
            if age > 120 or sum(((a - b) ** 2 for a, b in zip(copy['position'], owner['position']))) > (50 + 18000 * age / 60) ** 2:
                raise RuntimeError('Remote member query position is stale or inconsistent')
    return {'status': 'passed_' + args.case, 'measurement_valid': False, 'wingman_stages': stages, 'members': expected, 'scope': 'Production Hangar choice, births, Ship-owned simulation, 30/10 Hz motion and query entities' + '' + '; no attack, vacancy or performance pass'}
