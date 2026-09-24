"""One read-only minute of the requested 15-minute engineering monitor."""
import argparse
from collections import Counter
import json
import time
from datetime import datetime, timezone
from pathlib import Path

from capture_engineering_navigation import run, SNAPSHOT, memory_reader


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', required=True, type=Path)
    parser.add_argument('--editor-pid', type=int)
    parser.add_argument('--start', action='store_true')
    args = parser.parse_args()
    state_path = args.directory / 'monitor.json'
    if args.start:
        assert args.editor_pid and not state_path.exists(), 'Initialize a new monitor with the current editor PID.'
        args.directory.mkdir(parents=True, exist_ok=False)
        state = {'started_utc': datetime.now(timezone.utc).isoformat(), 'editor_pid': args.editor_pid,
                 'samples_completed': 0, 'target_samples': 15, 'status': 'active'}
        index = 0
    else:
        state = json.loads(state_path.read_text(encoding='utf-8'))
        if state['samples_completed'] >= state['target_samples']:
            print(json.dumps({'status': 'complete', 'samples_completed': state['samples_completed']}))
            return
        if time.time() - state.get('last_sample_unix', 0) < 45:
            print(json.dumps({'status': 'too_early', 'samples_completed': state['samples_completed']}))
            return
        index = state['samples_completed'] + 1
    try:
        snapshot = run(SNAPSHOT)
        read_memory, close_memory = memory_reader(state['editor_pid'])
        try:
            snapshot['editor_memory'] = read_memory()
        finally:
            close_memory()
        snapshot['sample_index'] = index
        snapshot['sample_utc'] = datetime.now(timezone.utc).isoformat()
        previous = json.loads((args.directory / f'minute-{index-1:02}.json').read_text(encoding='utf-8')) if index else None
        if previous and previous.get('success'):
            assert previous['world'] == snapshot['world'] and snapshot['world_seconds'] >= previous['world_seconds'], 'PIE was restarted during monitoring.'
        deltas = {}
        if previous and previous.get('success'):
            deltas['world_elapsed_seconds'] = snapshot['world_seconds'] - previous['world_seconds']
            deltas['elapsed_seconds'] = (datetime.fromisoformat(snapshot['sample_utc'])
                                        - datetime.fromisoformat(previous['sample_utc'])).total_seconds()
            for key in ('ManualQueries', 'WorkQueries', 'RepathQueries', 'ManualMs', 'WorkMs', 'RepathMs'):
                deltas[key] = snapshot['path'][key] - previous['path'][key]
            deltas['ore_mined'] = previous['ore_remaining_raw'] - snapshot['ore_remaining_raw']
            deltas['private_bytes'] = snapshot['editor_memory']['private_bytes'] - previous['editor_memory']['private_bytes']
            deltas['actor_count'] = snapshot.get('actor_count', 0) - previous.get('actor_count', 0)
            interval = deltas['elapsed_seconds']
            if interval > 0:
                deltas['queries_per_second'] = sum(deltas[key] for key in ('ManualQueries', 'WorkQueries', 'RepathQueries')) / interval
                deltas['query_ms_per_second'] = sum(deltas[key] for key in ('ManualMs', 'WorkMs', 'RepathMs')) / interval
        snapshot['minute_delta'] = deltas
        vehicles = snapshot.get('vehicles', [])
        snapshot['vehicle_summary'] = {
            'total': len(vehicles),
            'by_team_and_class': dict(Counter(f"{v.get('team', 'unknown')}:{v['class']}" for v in vehicles)),
            'mining_states': dict(Counter(v['state'] for v in vehicles if 'state' in v)),
            'cargo_raw': sum(sum(v.get('cargo', {}).values()) for v in vehicles),
            'failed_paths': sum(v.get('travel_counters', {}).get('FailedPaths', 0) for v in vehicles),
            'failed_path_bytes': sum(v.get('travel_counters', {}).get('FailedPathBytes', 0) for v in vehicles),
            'active_path_points': sum(v.get('travel_counters', {}).get('PathPoints', 0) for v in vehicles),
        }
    except Exception as error:
        snapshot = {'success': False, 'sample_index': index, 'sample_utc': datetime.now(timezone.utc).isoformat(), 'error': str(error)}
    (args.directory / f'minute-{index:02}.json').write_text(json.dumps(snapshot, ensure_ascii=False, indent=2), encoding='utf-8')
    state['samples_completed'] = index
    state['last_sample_unix'] = time.time()
    state['last_read_succeeded'] = snapshot['success']
    if index >= state['target_samples']:
        state['status'] = 'complete'
    state_path.write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding='utf-8')
    compact = {key: snapshot.get(key) for key in ('success', 'sample_index', 'sample_utc', 'world_seconds', 'navigation_building', 'actor_count', 'path', 'slots', 'minute_delta', 'ore_remaining_raw', 'editor_memory', 'vehicle_summary', 'error')}
    compact['miners'] = [{key: v.get(key) for key in ('name', 'team', 'state', 'cluster', 'node', 'cargo')}
                         for v in snapshot.get('vehicles', []) if 'state' in v]
    compact['inventory'] = snapshot.get('inventory', {})
    compact['factory_queued_raw'] = sum(snapshot.get('factory_queues', {}).values())
    compact['construction'] = [b for b in snapshot.get('buildings', []) if 'UNDER_CONSTRUCTION' in b['phase']]
    compact['monitor_status'] = state['status']
    print(json.dumps(compact, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
