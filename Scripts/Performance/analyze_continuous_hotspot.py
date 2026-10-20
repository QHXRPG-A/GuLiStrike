"""Analyze sustained successful business and queues from the native mixed schedule.

Pilot runs remain pilots. A cleared final queue never overrides growth flags or
an interval with no successful production, mining, movement or damage.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from analyze_hybrid_soak import observation_report
from hybrid_workload import validate_minimum


def summarize(data):
    work = data['continuous_workload']
    if work['schema'] != 'hybrid-continuous-business/v1':
        raise ValueError('Unknown native business schedule')
    if not work['complete'] or work['error'] or not data['complete'] or data['dropped_samples']:
        raise ValueError('Native continuous run failed or was interrupted: ' + work['error'])
    validate_minimum(work['checkpoints'][0]['simulation_census'])
    observation = observation_report(data, False)
    intervals = []
    receipt_kinds = {'damage': 0, 'extraction': 9, 'delivery': 10, 'processing': 11}
    children = lambda row: {(c['object'], c['generation']) for c in row['children_of_local_producers']}
    for before, after in zip(work['checkpoints'], work['checkpoints'][1:]):
        a = before['transactions']['receipt_kind_status']
        b = after['transactions']['receipt_kind_status']
        success = {name: b.get(f'{kind}/0', 0) - a.get(f'{kind}/0', 0)
                   for name, kind in receipt_kinds.items()}
        success['production_children'] = len(children(after) - children(before))
        success['accepted_move_commands'] = after['accepted_moves'] - before['accepted_moves']
        success['actually_displaced_commands'] = after['displaced_commands'] - before['displaced_commands']
        seconds = after['age_seconds'] - before['age_seconds']
        intervals.append({'start_seconds': before['age_seconds'], 'end_seconds': after['age_seconds'],
            'seconds': seconds, 'successful_business': success,
            'all_business_active': all(value > 0 for value in success.values()),
            'inventory_before': before['team_inventory'], 'inventory_after': after['team_inventory'],
            'history_before': before['simulation_census']['directory_records'],
            'history_after': after['simulation_census']['directory_records'],
            'simulation_before': before['simulation_census']['by_definition'],
            'simulation_after': after['simulation_census']['by_definition'],
            'receipt_status_deltas': {key: b.get(key, 0) - a.get(key, 0) for key in sorted(a.keys() | b.keys())}})
    coverage = (len(intervals) == 9 and all(299 <= row['seconds'] <= 305 for row in intervals)
                and data['sampled_seconds'] >= 2700 and work['requested_seconds'] == 2700)
    continuous = bool(intervals) and all(row['all_business_active'] for row in intervals)
    return {'schema': 'hybrid-continuous-business-analysis/v1', 'complete_requested_run': True,
        'formal_45_minute_coverage': coverage, 'sustained_successful_business': continuous,
        'every_native_window_valid': bool(work['windows']) and all(w['capture_valid'] for w in work['windows']),
        'business_intervals': intervals, 'observation': observation,
        'core_queue_coverage_complete':observation['core_queue_coverage_complete'],
        'queue_growth_requires_review': observation['continuous_growth_observed'],
        'scope': 'Native schedule; normal combat casualties retained. Performance and response gates require the separate frozen-build pairs.',
        'pass_requires': '45 minute coverage, successful business in every interval, complete core queue coverage, valid native windows and independent review of any queue growth flags.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    result = summarize(json.loads((args.directory / 'observation.json').read_text(encoding='utf-8-sig')))
    path = args.directory / 'business-analysis.json'
    path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({key: value for key, value in result.items() if key not in ('business_intervals', 'observation')}, ensure_ascii=False))


if __name__ == '__main__':
    main()
