"""Focused real-pool cohort checks. These never mark full gameplay/performance complete."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import time

def run(plan, args, records, read_state, write_json, initial):
    output = Path(plan['output'])
    clients = [e for e, _ in records if e['name'] != 'server' and e['participating']]
    owners = clients
    rows = []
    witnessed = set()
    sources = {}
    receipts = {e['name']: {} for e in clients}

    def completed_by_source():
        result = {}
        for key in witnessed:
            if all(receipts[e['name']].get(key) == 3 for e in clients):
                result.setdefault(sources[key], []).append(key)
        return result

    def read():
        result = {}
        for entry, process in records:
            if process.poll() is not None:
                raise RuntimeError(f"{entry['name']} exited during the real flight cohort case")
            value = read_state(Path(entry['directory']) / 'state.json', plan['run_id'], process.pid)
            if not value:
                return None
            result[entry['name']] = value
        return result

    def clock(state):
        return state['logical_tick'] / 60

    def check(states, stage):

        def fail(reason):
            write_json(output / 'flight-failure-state.json', {'stage': stage, 'failure': reason, 'states': states})
            raise RuntimeError(reason)
        ids = set()
        for entry in owners:
            value = states[entry['name']]
            load = value.get('flight_load', {})
            if load.get('path') != 'GuLiProjectilePoolSubsystem/Strafe' or load.get('configured_speed_cm_s') != 4800:
                fail(f"{stage}: {entry['name']} did not use the production Strafe pool/configuration")
            objects = load.get('objects', [])
            if len(objects) != load.get('live_additional'):
                fail(f'{stage}: real object evidence and live pool count disagree')
            for obj in objects:
                if obj['id'] in ids:
                    fail(f'{stage}: an additional flight is simulated by two processes')
                if not obj.get('local_simulation') or obj.get('simulator', '').lower() != entry['player'] or obj.get('owner_version', 0) <= 0:
                    fail(f'{stage}: a real pool flight has the wrong simulation authority')
                source = obj.get('source', '').lower()
                if not source or sources.get(obj['id'], source) != source:
                    fail(f'{stage}: an admitted flight lost or changed its source')
                sources[obj['id']] = source
                ids.add(obj['id'])
            for receipt in value.get('additional_flight_receipts', []):
                previous = receipts[entry['name']].get(receipt['id'], 0)
                receipts[entry['name']][receipt['id']] = previous | receipt['observed']
        if len(ids) != 500:
            fail(f'{stage}: local simulators account for {len(ids)} actual flights')
        witnessed.update(ids)
        rows.append({'monotonic_seconds': time.monotonic(), 'stage': stage, 'cohort_hash': hashlib.sha256(''.join(sorted(ids)).encode()).hexdigest(), 'endpoints': {e['name']: {'battle_seconds': clock(states[e['name']]), 'epoch': states[e['name']].get('epoch'), 'paused': states[e['name']].get('paused', False), 'additional': states[e['name']].get('active_additional_flights'), 'load': {k: v for k, v in states[e['name']].get('flight_load', {}).items() if k != 'objects'}} for e in [*clients, *[]]}})
        return ids
    control = {'schema': 1, 'run_id': plan['run_id'], 'phase': 'flights'}
    write_json(output / 'control.json', control)
    baseline_ids = check(initial, 'baseline')
    expected_sources = {sources[key] for key in baseline_ids}
    if len(expected_sources) != 500:
        raise RuntimeError('The sustained cohort does not contain 500 distinct production sources')
    write_json(output / 'flight-baseline.json', initial)
    states = initial
    resumed_ids = set()
    try:
        deadline = time.monotonic() + args.gameplay_timeout
        while time.monotonic() < deadline:
            current = read()
            if current:
                check(current, 'sustain')
                states = current
                by_source = completed_by_source()
                completed = [key for keys in by_source.values() for key in keys]
                if all(len(by_source.get(source, [])) >= 3 for source in expected_sources):
                    break
            time.sleep(0.2)
        else:
            by_source = completed_by_source()
            missing = sum(len(by_source.get(source, [])) < 3 for source in expected_sources)
            raise RuntimeError(f'{missing}/500 sources lack three complete flight lifetimes on all endpoints; '
                               f'{sum(map(len, by_source.values()))} complete lifetimes recorded')
        total_samples = 0
        for entry in owners:
            load = states[entry['name']]['flight_load']
            samples = load.get('speed_samples', 0)
            if load['local_goal'] and (samples < 20 or abs(load['minimum_measured_speed_cm_s'] - 4800) > 0.02 or abs(load['maximum_measured_speed_cm_s'] - 4800) > 0.02):
                raise RuntimeError(f"{entry['name']}: actual movement does not confirm the 4800 cm/s Strafe configuration")
            total_samples += samples
        write_json(output / 'flight-final.json', states)
        write_json(output / 'flight-delivery.json', {'actual_owned_ids': sorted(witnessed),
                   'completed_on_all_endpoints': sorted(completed), 'completed_by_source': by_source,
                   'flight_sources': sources, 'receipts': receipts})
        return {'status': 'passed_flights', 'measurement_valid': False, 'scope': 'Real 500-flight Strafe cohort, three lifetimes, unique authority and measured speed; ' + '' + 'no full-gameplay or performance pass', 'correctness_samples': len(rows), 'speed_samples': total_samples}
    finally:
        write_json(output / 'flight-last-observation.json', states)
        write_json(output / 'flight-receipt-progress.json', {
            'actual_owned_ids': sorted(witnessed),
            'completed_on_all_endpoints': sorted(key for key in witnessed
                if all(receipts[e['name']].get(key) == 3 for e in clients)),
            'receipts': receipts,
            'flight_sources': sources,
            'completed_by_source': completed_by_source(),
            'last_battle_seconds': {e['name']: clock(states[e['name']]) for e in clients},
            'scope': 'Last observed lifecycle receipts, including on failure; not a gameplay pass'})
        write_json(output / 'flight-timeline.json', {'schema': 1, 'run_id': plan['run_id'], 'samples': rows})
