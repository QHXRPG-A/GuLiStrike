"""Formal Commander input, cross-simulator mining, cargo/FIFO and owner state."""
import json
from pathlib import Path
import math
import time
import uuid
from runtime_evidence import additional_flight_counts, pending_object_handoffs

def _identity(value, hybrid):
    return value['stable_id'].replace('-', '').lower()

def run(plan, args, records, read_state, write_json, initial, required_balance=None):
    output = Path(plan['output'])
    clients = [entry for entry, _ in records if entry['name'] != 'server' and entry['participating']]
    server = next((entry for entry, _ in records if entry['name'] == 'server'))
    commanders = {team: next((e for e in clients if e['team'] == team and e['seat'] == 'Commander')) for team in ('Red', 'Blue')}
    reference = commanders['Red']
    extra_flights = int(plan.get('extra_flights', 0))

    def flight_counts(values):
        return additional_flight_counts({e['name']: values[e['name']] for e in clients})
    if extra_flights and sum(flight_counts(initial).values()) != extra_flights:
        raise RuntimeError('Mining started without the configured real additional-flight cohort')
    manifest = json.loads(Path(plan['manifest']).read_text(encoding='utf-8-sig'))
    # Newly produced hosts can coexist with this manifest's startup input cohort.
    expected={o['id'].replace('-','').lower() for o in manifest['objects']
              if manifest['strings'].get('adapter/definition/'+o['definition'])=='Miner'}
    def cohort(value):
        return [v for v in value.get('miner_states',[]) if _identity(v,True) in expected]
    def inventory_totals(value):
        if 'scene_objects' in value:
            hosts=[o['inventory'] for o in value['scene_objects'] if 'inventory' in o]
            return sum(v.get('blue',0)+v.get('red',0) for v in hosts),sum(v.get('queued_raw',0) for v in hosts)
        return (sum(v['cargo_blue']+v['cargo_red'] for v in value.get('miner_states',[])),
                sum(v['queued_raw'] for v in value.get('factory_states',[])))
    base=cohort(initial[reference['name']])
    if not expected or len(base)!=len(expected) or {_identity(v,True) for v in base}!=expected:
        raise RuntimeError('The configured startup mining cohort has missing or duplicate real entities')
    positions = {_identity(v, True): v['position'] for v in base}
    numbers = manifest['numbers']
    initial_raw = sum((int(float(v)) for k, v in numbers.items() if k.startswith('ore/') and k.endswith('/amount')))
    balance = initial[reference['name']]['economy']
    initial_processed = sum((initial[reference['name']]['resources']['processed_' + color] for color in ('blue', 'red')))
    baseline_remaining = initial[reference['name']].get('resource_query', {}).get('remaining_raw', initial_raw)
    baseline_cargo,baseline_queue=inventory_totals(initial[reference['name']])
    owners = {}
    for entry in clients:
        for value in cohort(initial[entry['name']]):
            if value['local_simulation']:
                key = _identity(value, True)
                if key in owners:
                    raise RuntimeError('A miner has two simulators')
                owners[key] = entry['name']
    if set(owners) != expected:
        raise RuntimeError('Not every formal miner has an assigned simulator')
    def choose_cluster(team, faction, values, members=None, excluded=()):
        local=values[commanders[team]['name']]
        members = members if members is not None else [v for v in cohort(local) if v['team'] == faction]
        if not members: raise RuntimeError('A mining input requires real cohort members')
        center = [sum((v['position'][i] for v in members)) / len(members) for i in range(3)]
        live={int(v['id']):v for v in local.get('mining_clusters',[])}
        if not live: raise RuntimeError('Missing current per-cluster mining evidence')
        candidates = []
        for index in range(int(float(numbers['resource_map/Clusters/count']))):
            prefix = f'resource_map/Clusters[{index}]/'
            cluster=int(float(numbers[prefix + 'ClusterId']))
            state=live.get(cluster)
            if cluster in excluded or not state or state['remaining']<=0 or not state[team.lower()+'_allowed']:
                continue
            if required_balance is not None:
                shortages = [color for color in ('red', 'blue') if required_balance[color]>0]
                if len(shortages) > 1:
                    raise RuntimeError('Split funding into resource phases before this mining stage')
                if shortages and int(float(numbers[prefix + 'ResourceType'])) != (1 if shortages[0] == 'red' else 0):
                    continue
            position = [float(numbers[prefix + 'Center/' + axis]) for axis in 'XYZ']
            candidates.append((math.dist(center, position), cluster, position))
        if not candidates:
            raise RuntimeError('No legal authored mining target')
        _, cluster, target = min(candidates)
        return {'player': commanders[team]['player'], 'actors': sorted((v['ordinal'] for v in members)), 'intent': 'Mine', 'cluster': cluster, 'x': target[0], 'y': target[1], 'z': target[2]}
    orders = []
    control = {'schema': 1, 'run_id': plan['run_id'], 'phase': 'gameplay', 'enable_mining': True}
    write_json(output / 'mining-baseline.json', initial)
    write_json(output / 'control.json', control)
    readiness = time.monotonic() + 15
    while time.monotonic() < readiness:
        ready_states = {e['name']: read_state(Path(e['directory']) / 'state.json', plan['run_id'], p.pid) for e, p in records}
        required = clients
        if all((ready_states[e['name']] and ready_states[e['name']].get('mining_ready') for e in required)):
            write_json(output / 'mining-ready.json', ready_states)
            break
        time.sleep(0.1)
    else:
        raise RuntimeError('The participating resource scenes did not enable before formal mining input')
    for team,faction in (('Red',1),('Blue',2)):
        members=sorted((v for v in cohort(ready_states[commanders[team]['name']]) if v['team']==faction),key=lambda v:v['ordinal'])
        # Funding prepares later combined stages. Split the real cohort across
        # two authored deposits instead of making half the vehicles wait for
        # the same work positions. The ordinary mining check still exercises
        # its original single-deposit contention and depletion behavior.
        groups=2 if required_balance is not None and len(members)>1 else 1
        split=(len(members)+groups-1)//groups
        used=set()
        for first in range(0,len(members),split):
            order=choose_cluster(team,faction,ready_states,members[first:first+split],used)
            order['command']=getattr(args,'command_base',0)+20000+len(orders)+1
            orders.append(order);used.add(order['cluster'])
    control['unit_orders'] = orders
    write_json(output / 'control.json', control)
    states = initial
    timeline = []
    accepted = set()
    initial_epoch = initial[reference['name']].get('epoch')
    requested_at = None

    freeze_at = None
    sealed = False
    extracted_peak = 0
    cargo_seen = queue_seen = False
    moved = set()
    depleted_waiting = {}
    depletion_transitions = []
    funding_returns = {}
    deadline = time.monotonic() + args.gameplay_timeout
    try:
        while time.monotonic() < deadline:
            current = {}
            for entry, process in records:
                if process.poll() is not None:
                    raise RuntimeError(f"{entry['name']} exited during mining acceptance")
                state = read_state(Path(entry['directory']) / 'state.json', plan['run_id'], process.pid)
                if not state:
                    break
                if state.get('failure'):
                    raise RuntimeError(f"{entry['name']}: {state['failure']}")
                current[entry['name']] = state
            if len(current) != len(records):
                time.sleep(0.1)
                continue
            states = current
            for entry in clients:
                if {_identity(v, True) for v in cohort(states[entry['name']])} != expected:
                    raise RuntimeError('Mining population or stable identity changed')
            changing = pending_object_handoffs([states[e['name']] for e in clients], expected)
            if changing:
                timeline.append({'monotonic_seconds': time.monotonic(), 'handoff_objects': sorted(changing),
                                 'scope': 'Awaiting this mining command cohort ownership; world continues',
                                 'additional_flights': flight_counts(states) if extra_flights else {}})
                time.sleep(0.2)
                continue
            active, active_views = {}, {}
            for entry in clients:
                for value in cohort(states[entry['name']]):
                    if value['local_simulation']:
                        key = _identity(value, True)
                        if key in active:
                            raise RuntimeError('A miner has multiple active simulators')
                        active[key] = value
                        active_views[key] = states[entry['name']]
            if set(active) != expected:
                raise RuntimeError('Mining population or life identity changed')
            for key, value in active.items():
                own=active_views[key]
                cluster=next((c for c in own['mining_clusters'] if c['id']==value['cluster']),None)
                blocked=cluster and cluster['remaining']==0 and value['task_state'] in (1,2)
                if blocked:
                    first=depleted_waiting.setdefault(key,{'work_seconds':value['work_seconds'],'before':dict(value)})
                    if value['work_seconds']-first['work_seconds']>2:
                        write_json(output/'mining-depletion-stall.json',{'first':first,'current':value,'cluster':cluster})
                        raise RuntimeError('A miner kept waiting at an exhausted cluster instead of returning cargo or finishing its task')
                elif key in depleted_waiting:
                    depletion_transitions.append({'source':key,**depleted_waiting.pop(key),'after':dict(value)})
            for key, value in active.items():
                if math.dist(positions[key], value['position']) > 250:
                    moved.add(key)
            cargo,queue=inventory_totals(states[reference['name']])
            factories = states[reference['name']].get('factory_states', [])
            remaining = states[reference['name']].get('resource_query', {}).get('remaining_raw')
            if remaining is None:
                raise RuntimeError('Missing actual remaining-ore evidence')
            processed_by_team = {team: sum((states[reference['name']]['economy'][team][color] - balance[team][color] for color in ('blue', 'red'))) for team in ('Red', 'Blue')}
            extracted = initial_raw - remaining
            extracted_peak = max(extracted_peak, extracted)
            cargo_seen |= cargo > 0
            queue_seen |= queue > 0
            for order in orders:
                team=next(t for t,e in commanders.items() if e['player']==order['player'])
                receipt = next((v for v in states[commanders[team]['name']].get('engineer_orders', []) if v['command'] == order['command']), None)
                if receipt and receipt['resolved']:
                    if set(receipt['accepted_actors']) != set(order['actors']):
                        raise RuntimeError(f'{team} formal mining input rejected: {receipt}')
                    accepted.add(order['command'])
            sample = {'monotonic_seconds': time.monotonic(), 'remaining_raw': remaining, 'cargo': cargo, 'queue': queue, 'processed_by_team': processed_by_team, 'moved_miners': len(moved), 'epochs': {e['name']: states[e['name']].get('epoch') for e in clients}, 'additional_flights': flight_counts(states) if extra_flights else {}}
            timeline.append(sample)
            enough = len(accepted) == len(orders) and cargo_seen and queue_seen and all((v >= 2 for v in processed_by_team.values()))
            if required_balance is not None:
                enough &= all((states[reference['name']]['economy'][team][color] >= amount for team in ('Red', 'Blue') for color, amount in required_balance.items()))
                # The combined room retains depletion from preceding stages.
                # Issue a fresh formal order after a completed empty-cluster
                # job; never restore ore or extend the stage deadline.
                if not freeze_at and not enough:
                    changed=False
                    for team,faction in (('Red',1),('Blue',2)):
                        if all(states[reference['name']]['economy'][team][c]>=n for c,n in required_balance.items()): continue
                        if team in funding_returns: continue
                        last=next(o for o in reversed(orders) if o['player']==commanders[team]['player'])
                        local=states[commanders[team]['name']]
                        cluster=next(v for v in local['mining_clusters'] if v['id']==last['cluster'])
                        miners=[v for v in active.values() if v['team']==faction]
                        # Once this real cohort has earned the requested amount,
                        # use the player's Return order to settle its carried ore.
                        # Do not wait for another full cargo/automatic work cycle.
                        # Depleted-cluster transitions are still checked above.
                        queued={c:sum(q['amount'] for f in local['factory_states'] for q in f['queue']
                                      if q['beneficiary']==faction and q['type']==(0 if c=='blue' else 1)) for c in ('blue','red')}
                        carried={c:sum(m['cargo_'+c] for m in miners) for c in ('blue','red')}
                        can_return=all(o['command'] in accepted for o in orders if o['player']==commanders[team]['player']) and all(local['economy'][team][c]+queued[c]+carried[c]>=n
                            for c,n in required_balance.items())
                        returning=[m for m in miners if m['cargo_blue']+m['cargo_red']>0 and m['task_state'] not in (3,4)]
                        if can_return and returning:
                            order={'player':commanders[team]['player'],'command':getattr(args,'command_base',0)+20000+len(orders)+1,
                                'actors':sorted(m['ordinal'] for m in returning),'intent':'Return',
                                'x':last['x'],'y':last['y'],'z':last['z']}
                            funding_returns[team]={'order':order,'balance':dict(local['economy'][team]),
                                'queued':queued,'carried':carried,'required':required_balance,'time':time.monotonic()}
                            orders.append(order);changed=True;continue
                        if last['command'] in accepted and cluster['remaining']==0 and all(v['work_complete'] for v in miners):
                            order=choose_cluster(team,faction,states)
                            order['command']=getattr(args,'command_base',0)+20000+len(orders)+1
                            orders.append(order);changed=True
                    if changed:
                        if len(orders)>128: raise RuntimeError('Mining preparation exceeded its bounded formal order budget')
                        control['unit_orders']=orders
                        write_json(output/'control.json',control)
            if enough and freeze_at is None:
                freeze_at = time.monotonic()
                control['enable_mining'] = False
                write_json(output / 'control.json', control)
                seal_evidence = getattr(args, 'seal_evidence', None)
                if seal_evidence:
                    seal_evidence('mining-owner-state', enable_mining=False)
                    sealed = True
                    continue
            if freeze_at and (sealed or time.monotonic() - freeze_at >= 3):
                # Read one complete client application, then compare actual entities.
                # No global revision or clock barrier is part of gameplay.
                book = states[reference['name']]['resources']
                views = [states[e['name']]['resources'] for e in clients]
                fields = ('remaining_raw','cargo_raw','queued_raw','processed_blue','processed_red')
                if any(any(v[k] != book[k] for k in fields) for v in views):
                    time.sleep(.2)
                    continue
                processed_delta = book['processed_blue'] + book['processed_red'] - initial_processed
                extracted_delta = baseline_remaining - book['remaining_raw']
                if extracted_delta != book['cargo_raw'] - baseline_cargo + book['queued_raw'] - baseline_queue + processed_delta:
                    raise RuntimeError(f'Client resource conservation failed: {book}')
                reference_miners = states[reference['name']]['miner_states']
                entity_cargo,queue=inventory_totals(states[reference['name']])
                if book['remaining_raw'] != remaining or entity_cargo != book['cargo_raw'] or queue != book['queued_raw']:
                    time.sleep(.2)
                    continue
                if processed_delta <= 0:
                    raise RuntimeError('No committed processor output')
                for entry in clients:
                    if {_identity(v, True): (v['cargo_blue'],v['cargo_red']) for v in states[entry['name']]['miner_states']} != {_identity(v, True): (v['cargo_blue'],v['cargo_red']) for v in reference_miners}:
                        break
                else:
                    if extra_flights and sum(flight_counts(states).values()) != extra_flights:
                        raise RuntimeError('The additional-flight cohort is incomplete')
                    if sum(states[e['name']].get('executed_miner_commands',0)-initial[e['name']].get('executed_miner_commands',0) for e in clients) != sum(len(o['actors']) for o in orders):
                        raise RuntimeError('Frozen mining inputs did not execute once per miner')
                    break
                time.sleep(.2)
                continue
            time.sleep(0.2)
        else:
            raise RuntimeError('Formal mining input did not reach consistent extraction, unloading and processing')
        write_json(output / 'mining-final.json', states)
        return {'status': 'passed_mining', 'measurement_valid': False, 'scope': 'Formal Commander selection, ' + 'three same-team Hybrid simulators' + ', navigation, reserved extraction, cargo, FIFO and output' + '' + ' only; no full-gameplay or performance pass', 'miners': len(expected), 'factories': len(factories), 'samples': len(timeline), 'extracted_raw': extracted_peak, 'processed_by_team': processed_by_team, 'additional_flights': flight_counts(states) if extra_flights else {}, 'additional_flight_minima': {e['name']: min((s['additional_flights'][e['name']] for s in timeline)) for e in clients} if extra_flights else {}}
    except Exception as error:
        write_json(output / 'mining-failure.json', {'failure': str(error), 'states': states})
        raise
    finally:
        write_json(output / 'mining-timeline.json', {'schema': 1, 'run_id': plan['run_id'], 'samples': timeline})
        write_json(output / 'mining-depletion-transitions.json', {'completed':depletion_transitions,'waiting':depleted_waiting})
        write_json(output / 'mining-funding-return-inputs.json', funding_returns)
