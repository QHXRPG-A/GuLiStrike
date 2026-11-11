"""Qualify recorded movement windows and compare identical frozen workloads.

Reads JSON evidence only. A valid old window may expose old gameplay failures;
it must not be discarded merely because its arrival rate is poor.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import statistics

SCENARIOS = ('crossing', 'hold', 'gather', 'mixed', 'attack', 'buildings', 'ore', 'work')


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def distribution(values):
    ordered = sorted(values)
    if not ordered:
        return {'count': 0, 'mean': None, 'p95': None, 'p99': None}
    return {'count': len(ordered), 'mean': statistics.fmean(ordered),
            'p95': ordered[max(0, math.ceil(.95*len(ordered))-1)],
            'p99': ordered[max(0, math.ceil(.99*len(ordered))-1)]}


def movement_behavior(movement):
    """Compare fixed-step positions, not the old version's extra applications."""
    previous, directions, streaks, initial_failures, failed_at = {}, {}, {}, {}, {}
    reversals = maximum_streak = 0
    for observation in movement['observations']:
        for member in observation['members']:
            identity, position = member['id'], member['position']
            failed = member.get('failed_order', 0)
            initial_failures.setdefault(identity, failed)
            if failed and failed != initial_failures[identity]:
                failed_at.setdefault(identity, observation['seconds'])
            if identity in previous:
                delta = (position[0]-previous[identity][0], position[1]-previous[identity][1])
                length = math.hypot(*delta)
                prior = directions.get(identity)
                reverse = length >= 2 and prior and (delta[0]*prior[0]+delta[1]*prior[1])/length < -.8
                streaks[identity] = streaks.get(identity, 0)+1 if reverse else 0
                reversals += bool(reverse)
                maximum_streak = max(maximum_streak, streaks[identity])
                directions[identity] = (delta[0]/length, delta[1]/length) if length >= 2 else None
            previous[identity] = position
    return {'fixed_step_reversals': reversals, 'maximum_consecutive_reversals': maximum_streak,
            'first_failed_order_seconds': distribution(list(failed_at.values())),
            'latest_first_failed_order_seconds': max(failed_at.values(), default=None),
            'orders_still_active_at_end': sum(bool(r['order']) for r in movement['observations'][-1]['members'])}


def summarize(path):
    result, capture, recipe = (read(path/name) for name in ('result.json','capture.json','recipe.json'))
    m, p = capture['movement'], capture['performance']
    assert result['valid_window'] and m['finished'] and not m['error'], f'{path}: incomplete window'
    assert m['seconds'] >= 30 and p['capture_valid'] and not p['capturing'], f'{path}: invalid timing'
    assert m['observation_schema'] == 3 and m['observed_logical_steps'] >= 290, f'{path}: observer missing'
    assert m['commands_sent'] == len(recipe['commands']), f'{path}: timeline incomplete'
    assert not p['frame_samples_dropped'] and not p['individual_samples_dropped'], f'{path}: samples lost'
    assert all(key in recipe for key in ('_units','_population','_scene')), f'{path}: incomplete frozen state'
    assert recipe['_scene']['source_map_stat'], f'{path}: map metadata absent'
    assert len(recipe['_units']) == 632 and m['census_before']['live_soldiers'] == 600, f'{path}: reduced workload'
    fixed = [a for a in m['applications'] if not a['immediate']]
    assert len(fixed) == m['observed_logical_steps'], f'{path}: fixed observation mismatch'
    keys = sorted({k for a in fixed for k in a.get('step_counters', {})})
    metrics = {k: distribution([a.get('step_counters', {}).get(k, 0) for a in fixed]) for k in keys}
    per_unit_metrics = {k: distribution([a.get('step_counters', {}).get(k, 0)/a['active_units']
                                        for a in fixed if a['active_units'] > 0]) for k in keys if k.endswith('Ms')}
    members = [r for r in m['members'] if r['commanded']]
    arrived = [r['arrival_seconds'] for r in members if r['arrival_seconds'] >= 0]
    active_steps = sum(a['active_units'] for a in fixed)
    movement_ms = sum(a['application_ms'] for a in m['applications'])
    # The retired predictive processor runs outside TickAuthority. New circle
    # solving is inside it; adding the retired processor's total is therefore
    # required for an inclusive old/new comparison, without nested sub-timings.
    movement_ms += p['counters'].get('Avoidance.TotalMs', {}).get('total', 0)
    work = [w for o in m['observations'] for w in o.get('work', [])]
    coverage = {'ore_events': m['ore_projection_events_applied'],
                'mining_seen': any(w.get('mining') for w in work),
                'docking_seen': any(w.get('task_state') == 4 for w in work),
                'maximum_cargo': max((w.get('cargo', 0) for w in work), default=0),
                'maximum_construction_work': max((w.get('work_done', 0) for w in work), default=0),
                'construction_completed': any(w.get('completed') for w in work),
                'command_kinds': [c.get('kind', 'move') for c in recipe['commands']]}
    scenario = recipe['scenario']
    if scenario == 'ore':
        assert coverage['ore_events'] == len(recipe['ore_projection_events']) > 0, f'{path}: ore events absent'
    if scenario == 'work':
        assert {'mine','return','construct'} <= set(coverage['command_kinds']), f'{path}: missing work command'
    if scenario == 'attack':
        assert recipe['automatic_fire'] and set(coverage['command_kinds']) == {'attack_move'}, f'{path}: no attack'
    return {'directory': str(path), 'scenario': scenario, 'seconds': m['seconds'],
            'logical_steps': len(fixed), 'applications': len(m['applications']), 'metrics_per_step': metrics,
            'metrics_per_active_unit_step': per_unit_metrics,
            'movement_total_per_step_ms': distribution([a['application_ms'] +
                a.get('step_counters', {}).get('Movement.ImmediateMs', 0) +
                a.get('step_counters', {}).get('Avoidance.TotalMs', 0) for a in fixed]),
            'movement_worker_ms_per_step': 0 if not (p['hotspot_mask'] & 32) else None,
            'movement_thread_basis': 'Frozen hotspot mask 31: movement/avoidance execute on the game thread; Hybrid network workers are separate',
            'fixed_application_ms': distribution([a['application_ms'] for a in fixed]),
            # Includes all old immediate applications; do not compare only its 10 Hz portion.
            'movement_ms_per_logic_step': movement_ms/len(fixed),
            'movement_ms_per_active_unit_step': movement_ms/active_steps if active_steps else None,
            'active_unit_steps': active_steps, 'commanded': len(members), 'arrived': len(arrived),
            'live_soldiers_before': m['census_before']['live_soldiers'],
            'live_soldiers_after': m['census_after']['live_soldiers'],
            'arrival_rate': len(arrived)/len(members) if members else None,
            'arrival_seconds': distribution(arrived),
            'maximum_stall_seconds': max((r['maximum_stall_seconds'] for r in members), default=0),
            'blocked': sum(bool(r['blocked']) for r in members),
            'dynamic_sweep_hits': m['dynamic_sweep_hits'], 'static_sweep_hits': m['static_sweep_hits'],
            'static_initial_overlap_escapes': sum(bool(move.get('static_initial_overlap_escape')) for a in m['applications'] for move in a['moves']),
            'memory_delta_bytes': m['memory_after_bytes']-m['memory_before_bytes'],
            'hotspot_mask': p['hotspot_mask'], 'coverage': coverage,
            'identity': p['simulation_census']['sync_identity'], 'movement_behavior': movement_behavior(m)}, recipe


def qualify(root, prefix):
    runs, recipes = {}, {}
    manifest = read(root/prefix[1:]) if prefix.startswith('@') else None
    for scenario in SCENARIOS:
        runs[scenario] = []
        for i in range(1, 4):
            name = manifest[scenario][i-1] if manifest else (prefix.format(scenario=scenario, iteration=i) if '{' in prefix else f'{prefix}-{scenario}-{i:02}')
            row, recipe = summarize(root/name)
            if scenario in recipes:
                assert recipe == recipes[scenario], f'{scenario}: repeats have different frozen recipes'
            else:
                recipes[scenario] = recipe
            runs[scenario].append(row)
    return runs, recipes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--old-prefix', required=True, help='Prefix or template, e.g. old-{scenario}-v6-{iteration:02}')
    parser.add_argument('--new-prefix')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = {'baseline_gate_passed': False, 'performance_improvement': False,
              'scope': '8 workloads x 3 repeats x 30 seconds; complete 632-unit opening population',
              'penetration_acceptance': 'User subsequently accepted penetration; recorded observations are informational, not an acceptance gate.',
              'limits': ['Old static sweep observer covers registered ore/player circles; physical building hulls are governed by NavMesh.',
                         'Process memory deltas include allocator/GC noise and are not exclusive avoidance memory.',
                         'Gameplay API receipts are distinct from network acknowledgements.',
                         'Map file timestamps can change during editor navigation preparation; both stamps are retained and complete semantic snapshots must match.',
                         'Runtime observation and setup checks do not constitute player visual/feel acceptance.']}
    try:
        old, recipes = qualify(args.root, args.old_prefix)
        output.update(baseline_gate_passed=True, old=old)
        if args.new_prefix:
            new, new_recipes = qualify(args.root, args.new_prefix)
            assert recipes == new_recipes, 'Old/new frozen recipes differ'
            output['new'] = new
            comparisons = {}
            for scenario in SCENARIOS:
                before, after = old[scenario], new[scenario]
                a = [r['movement_ms_per_logic_step'] for r in before]
                b = [r['movement_ms_per_logic_step'] for r in after]
                saving = statistics.fmean(a)-statistics.fmean(b)
                noise = max(max(a)-min(a), max(b)-min(b))
                arrival_ok = statistics.fmean(r['arrival_rate'] for r in after) >= statistics.fmean(r['arrival_rate'] for r in before)
                penetration_ok = sum(r['dynamic_sweep_hits'] for r in after) <= sum(r['dynamic_sweep_hits'] for r in before)
                work_ok = scenario != 'work' or all(all(r['coverage'][k] for k in ('mining_seen','docking_seen','construction_completed')) for r in after)
                comparisons[scenario] = {'old_ms': statistics.fmean(a), 'new_ms': statistics.fmean(b),
                                         'saving_ms': saving, 'repeat_range_ms': noise,
                                         'arrival_not_worse': arrival_ok, 'dynamic_penetrations_not_worse': penetration_ok,
                                         'work_functions_observed': work_ok,
                                         'motion_observed_both_versions': any(r['active_unit_steps'] for r in before) and any(r['active_unit_steps'] for r in after),
                                         'improved': saving > noise and arrival_ok}
            output['comparison'] = comparisons
            output['performance_improvement'] = all(c['improved'] for c in comparisons.values())
    except (AssertionError, FileNotFoundError, KeyError) as error:
        output['error'] = str(error)
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k:v for k,v in output.items() if k not in ('old','new')}, ensure_ascii=False))
    if 'error' in output:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
