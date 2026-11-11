"""Frozen native movement workloads; preparation runs outside timing windows."""
from __future__ import annotations


def make_extended_recipe(diagnostics: dict, scenario: str, context: dict) -> dict:
    all_units = sorted(diagnostics['soldiers'], key=lambda s: s['id'])
    local = [s for s in all_units if s['alive'] and s['can_simulate'] and s['team'] == 1]
    groups = {i: [s for s in local if s['unit_type'] == i] for i in range(1, 7)}
    members, commands = [], []
    recipe = {'schema': 'guli-movement-capture/v1', 'scenario': scenario,
              'map': '/Game/Maps/LVL_CommanderMassPrototype', 'automatic_fire': scenario == 'attack',
              'members': members, 'commands': commands}

    def place(units, origin, pitch=650):
        for i, unit in enumerate(units):
            members.append({'id': unit['id'], 'unit_type': unit['unit_type'],
                            'position': [origin[0] - (i // 5) * pitch, origin[1] + (i % 5 - 2) * pitch, origin[2]]})

    def command(units, target, at=.5, kind='move', **extra):
        commands.append({'at': at, 'members': [s['id'] for s in units], 'target': target, 'kind': kind, **extra})

    if scenario in ('buildings', 'ore'):
        center = context['factory']['ground'] if scenario == 'buildings' else context['ore_cluster']['center']
        ground, air = groups[1][:15], groups[2][:10]
        place(ground, [center[0]-4000, center[1], center[2]])
        place(air, [center[0]-7500, center[1], center[2]], 1300)
        command(ground, [center[0]+4000, center[1], center[2]])
        command(air, [center[0]+5500, center[1], center[2]], .8)
        if scenario == 'ore':
            events = []
            nodes = context['ore_cluster']['nodes']
            for remaining, at in [(2,5.), (1,10.)]:
                events += [{'at': at, 'node': n['id'], 'remaining': remaining}
                           for n in nodes if n['initial'] == 3][:4]
            events += [{'at': 15., 'node': n['id'], 'remaining': 0} for n in nodes]
            events += [{'at': 22., 'node': n['id'], 'remaining': n['initial']} for n in nodes]
            recipe['ore_projection_events'] = events
            recipe['ore_event_scope'] = 'Local obstacle/visual projection cost, not a resource-ledger transaction test. Real mining is measured in work.'
    elif scenario == 'work':
        miners, builders = groups[3][:4], groups[4][:4]
        cluster, building = context['mining_cluster'], context['construction']
        center, target = cluster['center'], building['ground']
        place(miners, [center[0]-1600, center[1], center[2]], 400)
        # Start outside the south-facing entrance. The old navigation graph
        # cannot project a fixture directly onto its interior unload marker.
        factory = context['factory']['ground']
        members[0]['position'] = [factory[0], factory[1]-1200, factory[2]]
        place(builders, [target[0]-1700, target[1], target[2]], 400)
        command(miners, center, .5, 'mine', cluster=cluster['id'])
        command(builders, target, .8, 'construct')
        command(miners, context['factory']['ground'], 12., 'return')
        command(miners, center, 26., 'mine', cluster=cluster['id'])
        recipe['construction_preparation'] = {'definition': building['definition'], 'ground': target}
    elif scenario in ('mixed', 'attack'):
        large = next(s for s in all_units if s['alive'] and s['unit_type'] == 6)
        center = large['position']
        target = [center[0]+2500, center[1], 900]
        ground = groups[1][:20]
        if scenario == 'mixed':
            ground += groups[5][:1]
        air = groups[2][:10]
        place(ground, [center[0]-6500, center[1], 900])
        # Reuse the already validated open LowAir staging area. A regular
        # rectangle east of the large target intersects existing map obstacles.
        for i, unit in enumerate(air):
            members.append({'id': unit['id'], 'unit_type': unit['unit_type'],
                            'position': [-30000+(i//5)*1300, 62000+(i%5-2)*1300, 900]})
        kind = 'attack_move' if scenario == 'attack' else 'move'
        command(ground, target, .5, kind)
        command(air, target, .8, kind)
        if scenario == 'mixed':
            workers = groups[3][:2]+groups[4][:2]
            place(workers, [center[0]-3500, center[1]+5000, 900], 400)
            command(workers, target, 1.1, 'worker_move')
        recipe['large_obstacle_id'] = large['id']
    else:
        raise ValueError(scenario)
    return recipe
