"""Resume existing unresolved G assertions, with local prerequisites and no gate."""
from __future__ import annotations

import json
import math
from pathlib import Path
import uuid

from acceptance_contract import digest
from acceptance_session import PreparationSkipped
from runtime_evidence import RuntimeEvidenceUnavailable

INVENTORIES = ('unit/101', 'unit/102', 'building/102')
GENERIC_STEPS = ('closure-population-preparation', 'generic-production-hosts', 'generic-production',
                 'shared-inventory-preparation', *("generic-inventory-"+d.replace('/', '-') for d in INVENTORIES),
                 'generic-card-submit', 'generic-content')
AIR_STEPS = ('air-assembly-preparation', 'closure-air-equipment', 'closure-air-combat',
             'closure-air-return-preparation', 'fixed-seat-admission')
ORDER = ('occupied-seat-admission', 'air-assembly-preparation', *GENERIC_STEPS,
         *AIR_STEPS[1:])
SUPPORTED = ('inventory_composition', 'generic_content', 'fixed_reconnect', 'fixed_role_rejections')


def select_remaining(path):
    original = json.loads(path.read_text(encoding='utf-8-sig'))
    if original.get('title') != '剩余玩法联合回归' or not isinstance(original.get('rooms'), list):
        raise RuntimeError('Resume input must be an original remaining-summary.json')
    for record in list(original.get('checks', {}).values()) + original.get('failures', []):
        evidence = record.get('evidence', [])
        if isinstance(evidence, str):
            evidence = [{'path': evidence, 'sha256': record.get('evidence_sha256')}]
        for item in evidence:
            if not item.get('sha256') or digest(Path(item['path'])) != item['sha256']:
                raise RuntimeError('Resume source evidence is missing or changed: '+str(item.get('path')))
    missing = set(original.get('missing_checks', []))
    if missing - set(SUPPORTED):
        raise RuntimeError('This closure supports only the remaining inventory/generic and fixed-seat branches: '
                           +str(sorted(missing-set(SUPPORTED))))
    inventory_failed = any(o.get('status') != 'passed' and o.get('step', '').startswith('generic-inventory-')
                           for o in original.get('outcomes', []))
    generic_failed = any(o.get('step') == 'generic-content' and o.get('status') != 'passed'
                         for o in original.get('outcomes', []))
    role_failed = any(o.get('step') == 'fixed-seat-admission' and o.get('status') != 'passed'
                      for o in original.get('outcomes', []))
    generic = bool(missing & {'inventory_composition', 'generic_content'}) or inventory_failed or generic_failed
    roles = bool(missing & {'fixed_reconnect', 'fixed_role_rejections'}) or role_failed
    wanted = set(GENERIC_STEPS if generic else ()) | set(AIR_STEPS if roles else ())
    if roles:
        wanted.add('occupied-seat-admission')
    required = tuple(c for c in SUPPORTED if (generic and c in SUPPORTED[:2]) or (roles and c in SUPPORTED[2:]))
    if not required:
        raise RuntimeError('The source summary has no unresolved supported assertions')
    return {'source_summary': str(path), 'source_sha256': digest(path),
            'source_baseline': original.get('baseline'), 'required_checks': required,
            'steps': [('G10', label) for label in ORDER if label in wanted],
            'policy': 'Historical passes select scope only. Every current assertion and prerequisite uses this room.'}


def occupied_seat(session):
    from admission_probe import reject
    session.begin('occupied-seat-admission')
    path = reject(session, uuid.uuid4().hex, 'Red', 'Ground', 'Role seat occupied')
    session.preliminary_admission_paths = [path]
    session.finish()


def prepare_shared_inventory(session):
    """One 45-second budget for real cargo; every host still delivers/processes it."""
    from integrated_acceptance import entry, identity, objects
    from population_combat_preparation import positions
    with session.preparation('shared real inventory cargo'):
        session.begin('shared-inventory-cargo-preparation')
        session.control('gameplay', enable_mining=True)
        session.wait('shared-mining-ready', lambda: all(session.states[e['name']].get('mining_ready')
                                                     for e in session.clients), timeout=15)
        manifest = json.loads(Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
        commander = entry(session, 'Red', 'Commander')
        numbers = manifest['numbers']
        live = {int(c['id']): c for c in session.states[commander['name']]['mining_clusters']}
        deposits = []
        for index in range(int(float(numbers['resource_map/Clusters/count']))):
            prefix = f'resource_map/Clusters[{index}]/'
            cluster = int(float(numbers[prefix+'ClusterId']))
            state = live.get(cluster)
            if state and state['remaining'] > 0 and state['red_allowed']:
                deposits.append((cluster, [float(numbers[prefix+'Center/'+axis]) for axis in 'XYZ']))
        miners = [o for o in objects(session, True) if o['definition'] == 'unit/3'
                  and o['team'] == 1 and o['life'] == 1]
        assignments, used_miners, used_clusters = [], set(), set()
        for definition in INVENTORIES:
            hosts = [o for o in objects(session, True) if o['definition'] == definition
                     and o['team'] == 1 and o['life'] == 1 and (not definition.startswith('building/') or o['completed'])]
            choices = [(math.dist(m['position'], point)+math.dist(h['position'], point), m, h, cluster, point)
                       for m in miners if m['id'] not in used_miners for h in hosts
                       for cluster, point in deposits if cluster not in used_clusters]
            if not choices:
                raise PreparationSkipped('No independent legal miner/deposit/host for '+definition)
            _, miner, host, cluster, point = min(choices, key=lambda c: (c[0], c[1]['ordinal'], c[3]))
            assignments.append({'definition': definition, 'miner': miner, 'host': host,
                                'cluster': cluster, 'point': point})
            used_miners.add(miner['id']); used_clusters.add(cluster)
        session.shared_inventory_assignments = assignments
        receipts = positions(session, [(a['miner'], [a['point'][0]+1800, *a['point'][1:]], False, None)
                                       for a in assignments])
        commands = []
        for a in assignments:
            session.input_ordinal = getattr(session, 'input_ordinal', 0)+1
            commands.append({'player': commander['player'], 'command': session.stage_number*100000+session.input_ordinal,
                             'actors': [a['miner']['ordinal']], 'intent': 'Mine', 'cluster': a['cluster'],
                             **dict(zip(('x', 'y', 'z'), a['point']))})
        session.write_json(session.stage_output/'shared-mining-inputs.json',
                           {'assignments': assignments, 'positions': receipts, 'commands': commands})
        session.control('gameplay', enable_mining=True, unit_orders=commands)
        moved, moves_readback = {}, []
        def loaded():
            values = {identity(o['id']): o for o in objects(session, True)}
            if all((o := values.get(identity(a['miner']['id']))) and
                   o.get('inventory', {}).get('blue', 0)+o.get('inventory', {}).get('red', 0) > 0 for a in assignments):
                return True
            work, signatures = [], {}
            for endpoint in session.clients:
                for value in session.states[endpoint['name']].get('miner_states', []):
                    key = identity(value['stable_id'])
                    if not value.get('local_simulation') or key not in values:
                        continue
                    if key not in {identity(a['miner']['id']) for a in assignments}:
                        continue
                    point = value.get('reserved_work_position')
                    lease = value.get('lease', {})
                    if isinstance(point, list) and len(point) == 3 and value.get('task_state') == 1 \
                            and int(identity(lease.get('token', '0')), 16):
                        signature = (lease.get('token'), lease.get('task'), lease.get('sample'), *point)
                        if moved.get(key) != signature:
                            work.append((values[key], point, False, None))
                            signatures[key] = signature
            if work:
                readback = positions(session, work)
                moved.update(signatures)
                moves_readback.append({'reservations': signatures, 'receipts': readback})
                session.write_json(session.stage_output/'reserved-work-position-readbacks.json', {'moves': moves_readback})
                session.control('gameplay', enable_mining=True)
            return False
        session.wait('shared-real-cargo', loaded)
        # Keep the real cargo under one accepted player Move while the three
        # inventory interactions run. Do not pre-dock it and let an automatic
        # factory delivery consume it before its branch starts.
        session.input_ordinal += 1
        point = [assignments[0]['point'][0]+1800, *assignments[0]['point'][1:]]
        hold = {'player': commander['player'], 'command': session.stage_number*100000+session.input_ordinal,
                'actors': sorted(a['miner']['ordinal'] for a in assignments), 'intent': 'Move',
                **dict(zip(('x', 'y', 'z'), point))}
        session.control('gameplay', enable_mining=True, unit_orders=[hold])
        def held():
            active = {identity(m['stable_id']): m for e in session.clients
                      for m in session.states[e['name']].get('miner_states', []) if m.get('local_simulation')}
            return all((m := active.get(identity(a['miner']['id']))) and m.get('active_command') == hold['command']
                       and m['cargo_blue']+m['cargo_red'] > 0 for a in assignments)
        session.wait('shared-cargo-player-command-accepted', held)
        session.shared_inventory_sources = {a['definition']: identity(a['miner']['id']) for a in assignments}
        session.write_json(session.stage_output/'shared-cargo-readback.json', {
            'sources': session.shared_inventory_sources, 'hold_move': hold,
            'inventories': [o for o in objects(session, True) if identity(o['id']) in session.shared_inventory_sources.values()],
            'scope': 'Actual mining commands, reservations and cargo. No ore, lease, cargo or final receipt injection.'})
    # The setup deadline ends when real cargo is ready. Evidence sealing does
    # not turn completed preparation into a gameplay failure.
    session.finish()


def prepare_live_members(session):
    """Keep the existing fifty real members without replaying passed G09 motion/UI."""
    with session.preparation('original live Air cohort for membership'):
        session.begin('membership-live-members-preparation')
        session.wait('original-live-members-ready', lambda: all(
            all(sum(m['life_state'] == 1 and bool(m['entity']) and m['team'] == team
                    for m in session.states[e['name']].get('wingman_states', [])) == 25 for team in ('Red', 'Blue'))
            for e in session.clients))
        session.write_json(session.stage_output/'membership-members-readback.json', {
            'scope': 'Original real fifty-member prerequisite only. No replayed G09 movement, cadence or UI verdict.'})
    session.finish()


def prepare_combat_work(session):
    """One real attack and canonical damage receipt; retain its work for handoff."""
    from integrated_acceptance import entry, identity, objects, input_and_wait
    from population_combat_preparation import positions
    with session.preparation('real post-combat state for membership'):
        session.begin('membership-combat-work-preparation')
        ships = {o['team']: o for o in objects(session, True) if o['definition'] == 'player/ship' and o['life'] == 1}
        if set(ships) != {1, 2}:
            raise PreparationSkipped('Two live occupied Ships are required for the membership fixture')
        session.remaining_ship_homes = {identity(o['id']): list(o['position']) for o in ships.values()}
        # Keep the source carrier with its already-live cohort. Moving both
        # carriers to a remote arena leaves the real members travelling from
        # their old formation for the entire short preparation window.
        source_pose = list(ships[1]['position'])
        angle = math.radians(ships[1]['yaw'])
        target_pose = [source_pose[0]+4000*math.cos(angle),
                       source_pose[1]+4000*math.sin(angle), source_pose[2]]
        placed = positions(session, [(ships[2], target_pose, False, None)])
        pilot = entry(session, 'Red', 'Air')
        members = {identity(o['id']) for o in objects(session, True) if o['kind'] == 2 and o['life'] == 1
                   and identity(o['parent']) == identity(ships[1]['id'])}
        before = set(session.hits)
        target, target_path = input_and_wait(session, 'ship_target', pilot['player'], target=ships[2]['id'])
        for channel in session.states[pilot['name']]['scene_ui'].get('weapon_channels', []):
            if channel['enabled'] and channel['manual_input']:
                input_and_wait(session, 'weapon_input', pilot['player'], subject=channel['subject'], slot=channel['slot'], pressed=True)
                input_and_wait(session, 'weapon_input', pilot['player'], subject=channel['subject'], slot=channel['slot'], pressed=False)
        def commits():
            return [{'hit': hit, 'decision': session.decisions[event]}
                    for event, hit in session.hits.items() if event not in before and hit.get('source_known')
                    and hit.get('source_kind') == 2 and identity(hit['source']) in members
                    and identity(hit['target']) == identity(ships[2]['id']) and event in session.decisions
                    and session.decisions[event]['kind'] == 0 and session.decisions[event]['status'] in (0, 1)
                    and session.decisions[event]['applied_damage']+session.decisions[event]['absorbed_damage'] > 0]
        def ready():
            if commits():
                return True
            current = {o['team']: o for o in objects(session, True) if o['definition'] == 'player/ship' and o['life'] == 1}
            inputs = []
            for team, other, label in ((1, 2, 'Red'), (2, 1, 'Blue')):
                if team not in current or other not in current:
                    continue
                own, enemy = current[team], current[other]
                facing = math.degrees(math.atan2(enemy['position'][1]-own['position'][1], enemy['position'][0]-own['position'][0]))
                turn = ((facing-own['yaw']+180)%360-180)/30
                inputs.append({'player': entry(session, label, 'Air')['player'], 'thrust': [0, 0, 0],
                               'turn': max(-1, min(1, turn)), 'boost': False})
            session.control('gameplay', ship_inputs=inputs)
            return False
        session.wait('real-membership-combat-work', ready)
        session.write_json(session.stage_output/'membership-combat-readback.json', {
            'positions': placed, 'source_pose': source_pose, 'target_pose': target_pose,
            'target': target, 'target_input': target_path, 'commits': commits(),
            'members': sorted(members), 'effects': [o for o in objects(session, True)
                if o['kind'] == 4 and identity(o['parent']) in members],
            'scope': 'Real attack and canonical damage preparation only. All source/work lives and receipts retained; no new G09 verdict.'})
    session.finish()


def restore_combat_positions(session):
    from integrated_acceptance import identity, objects
    from population_combat_preparation import positions
    with session.preparation('return staged Ships without changing their work'):
        session.begin('membership-combat-position-return')
        homes = getattr(session, 'remaining_ship_homes', {})
        requests = [(o, homes[identity(o['id'])], False, None) for o in objects(session, True)
                    if o['definition'] == 'player/ship' and o['life'] == 1 and identity(o['id']) in homes]
        if requests:
            session.write_json(session.stage_output/'position-return-readback.json', {'positions': positions(session, requests),
                'scope': 'Positions only; no effect cleanup, healing, life reset, ownership or receipt injection.'})
    session.finish()


def run_session(session):
    from integrated_remaining import attempt, require
    from integrated_continuation import prepare_hosts, prime_equipment, generic_card
    from integrated_acceptance import prepare_cohort, objects
    from integrated_commander import prepare_population, production, inventory_combination, authored_closure
    from integrated_roles import fixed_reconnect
    selection = session.args.remaining_selection
    allowed = [label for _, label in selection['steps']]
    pending = set(session.args.collect_steps or allowed)
    # Rebuild only local state needed by unfinished assertions after a lost room.
    if pending & set(GENERIC_STEPS):
        pending.update(GENERIC_STEPS)
    if 'fixed-seat-admission' in pending:
        pending.update(AIR_STEPS); pending.add('occupied-seat-admission')
    pending = [label for label in allowed if label in pending]
    failures, outcomes, admitted = [], [], {}
    session.shared_inventory_sources = {}
    functions = {
        'occupied-seat-admission': occupied_seat,
        'air-assembly-preparation': prime_equipment,
        'closure-population-preparation': lambda s: prepare_population(s, 12),
        'generic-production-hosts': lambda s: prepare_hosts(s, admitted),
        'generic-production': require(lambda s: all(d in admitted for d in (4, 101)),
                                      'This room needs completed real production hosts', lambda s: production(s, admitted)),
        'shared-inventory-preparation': prepare_shared_inventory,
        'generic-card-submit': generic_card,
        'generic-content': lambda s: authored_closure(s, 'generic_content'),
        'closure-air-equipment': prepare_live_members,
        'closure-air-combat': prepare_combat_work,
        'closure-air-return-preparation': restore_combat_positions,
        'fixed-seat-admission': fixed_reconnect,
    }
    for definition in INVENTORIES:
        functions['generic-inventory-'+definition.replace('/', '-')] = require(
            lambda s, d=definition: d in s.shared_inventory_sources,
            'Shared cargo preparation unavailable; see the single shared-inventory-preparation result',
            lambda s, d=definition: inventory_combination(s, d))
    session.configure_test_items((('remaining', '剩余玩法联合回归', selection['required_checks']),))
    session.begin_test_item('remaining')
    def save(blocked=None):
        session.write_json(session.output/'remaining-progress.json', {
            'title': '剩余玩法联合回归', 'pending': pending, 'outcomes': outcomes,
            'blocked': blocked, 'blocked_kind': 'shared_runtime', 'checks': session.checks, 'failures': failures,
            'run_id': session.plan['run_id'], 'baseline': session.plan['baseline']['identity']})
    save()
    try:
        attempt(session, 'load-preparation', prepare_cohort, failures)
        if failures:
            save('Actual 600 + 500 load preparation did not complete')
            session.finish_test_item(blocked=True)
            return failures
        for label in allowed:
            if label not in pending:
                continue
            print(json.dumps({'task': '剩余玩法联合回归', 'step': label, 'run_id': session.plan['run_id']}, ensure_ascii=False), flush=True)
            before, blocked = len(failures), None
            try:
                attempt(session, label, functions[label], failures)
            except RuntimeEvidenceUnavailable as error:
                blocked = str(error)
            failure = failures[-1] if len(failures) != before else None
            if label == 'shared-inventory-preparation' and failure:
                session.shared_inventory_failure = {'evidence': failure['evidence'], 'sha256': failure['evidence_sha256']}
                from integrated_acceptance import identity
                current = {identity(o['id']): o for o in objects(session, True)}
                # A shared timeout cannot hide an independently ready host.
                for a in getattr(session, 'shared_inventory_assignments', []):
                    key = identity(a['miner']['id']); obj = current.get(key)
                    if obj and obj.get('inventory', {}).get('blue', 0)+obj.get('inventory', {}).get('red', 0)>0:
                        session.shared_inventory_sources[a['definition']] = key
                session.write_json(session.stage_output/'shared-cargo-partial-readback.json', {
                    'sources': session.shared_inventory_sources, 'upstream': session.shared_inventory_failure,
                    'scope': 'Only already extracted real cargo; remaining dependencies stay skipped.'})
            outcomes.append({'group': 'G10', 'step': label, 'status': failure['status'] if failure else 'passed',
                             'stage': session.stage_id, 'failure': failure})
            if not blocked:
                pending.remove(label)
            save(blocked)
            if blocked:
                session.finish_test_item(blocked=True)
                return failures
        session.finish_test_item()
        return failures
    except RuntimeEvidenceUnavailable as error:
        save(str(error)); session.finish_test_item(blocked=True)
        return failures
