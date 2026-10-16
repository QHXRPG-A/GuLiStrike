"""Ownership stimuli use the real map input and service scheduler.

Reported-load fixtures are explicit development-only inputs, never performance
measurements. Entity state, ownership commits and P2P delivery stay production.
"""
from __future__ import annotations

import copy
import time
from integrated_acceptance import entry, identity, input_and_wait, objects, prove


def room(session):
    rooms = session.states['server'].get('rooms', [])
    if len(rooms) != 1:
        raise RuntimeError('Ownership evidence requires this run\'s single room')
    return rooms[0]


def directory(session):
    return {identity(o['id']): o for o in room(session)['objects']}


def history(session, keys):
    result = {}
    for e in session.clients:
        if e['name'] not in session.states: continue
        for row in session.states[e['name']].get('handoff_history', []):
            if identity(row['object']) in keys:
                result[(row['transfer'], row['object'])] = row
    return list(result.values())


def select(session, commander, keys):
    return input_and_wait(session, 'selection_objects', commander['player'], objects=sorted(keys))


def loads(session, values):
    for e in session.clients:
        if e['team'] == 'Red':
            input_and_wait(session, 'fault_load_report', e['player'], milliseconds=values[e['seat']])
    if all(v >= 0 for v in values.values()):
        session.wait('three-load-samples', lambda: all(
            abs(m['simulation_ms']-values[next(e['seat'] for e in session.clients if e['player']==identity(m['player']))]) < .01
            for m in room(session)['members'] if m['team']==1 and m['online']), timeout=15)


def observe(session, label, seconds, assertion):
    start = time.monotonic()
    timeline = []
    def ready():
        if any(session.states[e['name']].get('paused') for e in session.clients):
            raise RuntimeError('Ordinary object handoff paused the world')
        assertion()
        timeline.append({'at':time.monotonic(), 'room':copy.deepcopy(room(session))})
        return time.monotonic()-start >= seconds
    session.wait(label, ready, timeout=seconds+15)
    session.write_json(session.stage_output/(label+'-timeline.json'), timeline)


def same_owners(session, keys, player):
    view = directory(session)
    return all(identity(view[k]['owner']) == player for k in keys)


def run_selection(session):
    session.begin('commander-priority-selection-and-idle-load')
    commander = entry(session, 'Red', 'Commander')
    keys = {k for k,o in directory(session).items() if o['policy']==1 and identity(o['controller'])==commander['player']}
    if not keys:
        raise RuntimeError('No data-defined CommanderActivity objects')
    before = copy.deepcopy(directory(session))
    source_owners = {identity(before[k]['owner']) for k in keys}
    try:
        loads(session, {'Commander':5., 'Ground':5., 'Air':5.})
        _, selected = select(session, commander, keys)
        session.wait('priority-committed', lambda: same_owners(session, keys, commander['player']) and session.settled())
        moved = {k for k in keys if identity(before[k]['owner']) != commander['player']}
        transfers = history(session, moved)
        good = len(source_owners)==3 and all(any(identity(t['object'])==k and t['reason']==0
            and t['frozen_state']==t['installed_state'] and int(t['frozen_state'])
            and t['owner_version']==before[k]['owner_version']+1 for t in transfers) for k in moved)
        prove(session, 'commander_selection_takeover', good,
              'Selection of real objects across three owners committed matching private-state digests and one new owner version',
              'baseline.json', selected, 'priority-committed.json')

        def pinned():
            view=directory(session)
            if not same_owners(session,keys,commander['player']) or not all(view[k]['selected'] for k in keys):
                raise RuntimeError('A selected object was offloaded')
        loads(session, {'Commander':5., 'Ground':.1, 'Air':.1})
        observe(session, 'selection-held', 12, pinned)
        prove(session, 'selection_pinning', True, 'Selected objects remain at Commander beyond the idle interval despite a sufficient reported load gap',
              'selection-held.json','selection-held-timeline.json')

        loads(session, {'Commander':5., 'Ground':5., 'Air':5.})
        _, deselected=select(session, commander, set())
        def not_migrated():
            if not same_owners(session,keys,commander['player']):
                raise RuntimeError('Object moved before the idle/load conditions were met')
        observe(session,'deselected-under-ten-seconds',8,not_migrated)
        observe(session,'idle-with-insufficient-gap',5,not_migrated)
        loads(session, {'Commander':5., 'Ground':.1, 'Air':.1})
        session.wait('idle-batch-committed',lambda:any(identity(directory(session)[k]['owner'])!=commander['player'] for k in keys))
        offloaded={k for k in keys if identity(directory(session)[k]['owner'])!=commander['player']}
        # Reselect while additional background batches may still be preparing.
        select(session,commander,keys)
        session.wait('selection-preempts-background',lambda:same_owners(session,keys,commander['player']) and session.settled())
        # Retained flights may drain at the old owner after their producer moves.
        # The following steady check is not a performance sample and does not
        # count an interrupted refill as a continuously full cohort.
        session.wait('handoff-cohort-refilled',lambda: session.settled() and sum(
            session.states[e['name']].get('flight_load',{}).get('live_additional',0)
            for e in session.clients)==500)
        all_transfers=history(session,keys)
        valid=bool(offloaded) and all(t['frozen_state']==t['installed_state'] and int(t['installed_state']) for t in all_transfers)
        prove(session,'idle_offload',valid,'No early or equal-load migration; sufficient reported load permits idle transfer; selection wins back ownership',
              deselected,'deselected-under-ten-seconds-timeline.json','idle-with-insufficient-gap-timeline.json',
              'idle-batch-committed.json','selection-preempts-background.json')
        prove(session,'handoff_state',valid,'Committed transfers have identical exported/installed state digests; ownership and live entities remain present',
              'priority-committed.json','selection-preempts-background.json')
    finally:
        loads(session, {'Commander':-1., 'Ground':-1., 'Air':-1.})
    session.finish()


def run_handoff_faults(session):
    from integrated_faults import arm, release, fault
    from integrated_commander import buildings, teleport_and_cards
    import json
    from pathlib import Path
    manifest=json.loads(Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
    if manifest.get('strings',{}).get('acceptance/preparation')=='gf-fast-spatial-v2':
        from integrated_commander import construct, prepare_population, fund, production
        from population_combat_preparation import prepare_mining
        admitted={}
        for definition in (4,101,102):
            with session.preparation('resource handoff host '+str(definition)):
                construct(session,definition,(0,0),admitted)
        if not all(any(o['parent']==admitted[d] and o['life']==1 and o['entity']
                       for o in objects(session,True)) for d in (4,101)):
            prepare_population(session,8)
        fund(session,{'blue':128,'red':128})
        with session.preparation('resource handoff real production births'):
            production(session,admitted)
        prepare_mining(session)
        resource_work_handoff(session,admitted)
        return
    # Real authored production and capability work supplies entities to transfer.
    admitted=buildings(session)
    teleport_and_cards(session)
    resource_work_handoff(session, admitted)


def ordinary_handoff_faults(session):
    from integrated_faults import arm, release, fault
    session.begin('ordinary-handoff-cohort')
    commander=entry(session,'Red','Commander')
    live={identity(o['id']):o for o in objects(session,True) if o['life']==1 and o['team']==1}
    keys={k for k,o in directory(session).items() if k in live and o['policy']==1 and identity(o['controller'])==commander['player']}
    with session.preparation('Ordinary handoff source cohort'):
        if not keys: raise RuntimeError('No actual living CommanderActivity object for handoff injection')
        select(session,commander,keys)
        session.wait('source-authority',lambda:same_owners(session,keys,commander['player']) and session.settled())
    session.finish()
    session.begin('handoff-message-delay-timeout-and-retry')
    # Offload a real object, then delay its owner's handoff lane. Other objects
    # and all network control paths must keep progressing during the five seconds.
    held=None
    try:
        with session.preparation('Ordinary handoff injection target'):
            loads(session,{'Commander':5.,'Ground':.1,'Air':.1});select(session,commander,set())
            session.wait('available-peer-owner',lambda:any(identity(directory(session)[k]['owner'])!=commander['player'] for k in keys),timeout=60)
        key=next(k for k in keys if identity(directory(session)[k]['owner'])!=commander['player'])
        source=next(e for e in session.clients if e['player']==identity(directory(session)[key]['owner']))
        held=arm(session,source,'handoff_hold',count=1)
        select(session,commander,{key})
        session.wait('handoff-packets-held',lambda:fault(session,source).get('intercepted',0)>0)
        owner_version=directory(session)[key]['owner_version']
        def unchanged():
            o=directory(session)[key]
            if identity(o['owner'])!=source['player'] or o['owner_version']!=owner_version:
                raise RuntimeError('An incomplete handoff committed')
        observe(session,'handoff-timeout',6,unchanged)
        release(session,source,held);held=None
        select(session,commander,set());select(session,commander,{key})
        session.wait('handoff-retry',lambda:same_owners(session,{key},commander['player']) and session.settled())
    finally:
        if held: release(session,source,held)
        loads(session,{'Commander':-1.,'Ground':-1.,'Air':-1.})
    session.finish()


def resource_work_handoff(session, admitted):
    """Resource capabilities, work and newborns migrate as one actual group."""
    from integrated_faults import arm, fault, release
    from integrated_roles import leave, returned
    session.begin('resource-group-pending-work-and-newborn-handoff')
    before={identity(o['id']):copy.deepcopy(o) for o in objects(session,True)
            if o['ownership_policy']==4 and o['life']==1}
    owners={identity(o['simulator']) for o in before.values()}
    if len(owners)!=1: raise RuntimeError('Resource group was split before the fault')
    owner=next(e for e in session.clients if e['player']==next(iter(owners)))
    newborns={k:o for k,o in before.items() if o.get('parent') in {admitted[4],admitted[101]}}
    if not newborns: raise RuntimeError('No real production newborn is in the resource group')
    held=arm(session,owner,'receipt_hold',count=1,business_kind=11)
    departed=False
    try:
        # The action-only control used to arm a fault clears the mining query
        # flag. Restore that preparation before reading legal targets.
        with session.preparation('Mining query after fault arm'):
            session.control('gameplay',enable_mining=True)
            session.wait('mining-query-restored',lambda:all(session.states[e['name']].get('mining_ready') for e in session.clients))
        import json,math
        from pathlib import Path
        manifest=json.loads(Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
        orders=[]
        for team,faction in (('Red',1),('Blue',2)):
            commander=entry(session,team,'Commander');state=session.states[commander['name']]
            miners=[m for m in state['miner_states'] if m['team']==faction]
            legal={c['id'] for c in state['mining_clusters'] if c['remaining']>0 and c[team.lower()+'_allowed']}
            positions=[]
            for i in range(int(float(manifest['numbers']['resource_map/Clusters/count']))):
                prefix=f'resource_map/Clusters[{i}]/';cluster=int(float(manifest['numbers'][prefix+'ClusterId']))
                if cluster in legal: positions.append((cluster,[float(manifest['numbers'][prefix+'Center/'+a]) for a in 'XYZ']))
            if not miners or not positions: raise RuntimeError('No real mining cohort/target for pending processing work')
            center=[sum(m['position'][i] for m in miners)/len(miners) for i in range(3)]
            cluster,point=min(positions,key=lambda v:math.dist(center,v[1]))
            orders.append({'player':commander['player'],'command':session.stage_number*100000+800+faction,
                'actors':sorted(m['ordinal'] for m in miners),'intent':'Mine','cluster':cluster,'x':point[0],'y':point[1],'z':point[2]})
        session.control('gameplay',enable_mining=True,unit_orders=orders)
        def pending():
            events={identity(v['event']) for v in fault(session,owner).get('messages',[])}
            return any(identity(o.get('inventory',{}).get('pending','0')) in events for o in objects(session,True))
        if manifest.get('strings',{}).get('acceptance/preparation')=='gf-fast-spatial-v2':
            from integrated_commander import deliver_inventory
            host=next(o for o in objects(session,True) if o['id']==admitted[102] and o['life']==1
                and o['completed'] and o.get('inventory',{}).get('processing'))
            # Cargo extraction, positioning and actual delivery each retain
            # their existing short preparation scopes. They must not consume
            # the separate budget for observing the held processing receipt.
            deliver_inventory(session,host,wait_for_processing=False)
        with session.preparation('Actual pending processing work for handoff'):
            session.wait('original-processing-pending',pending)
        events={identity(v['event']) for v in fault(session,owner)['messages']}
        sources={identity(o['id']):copy.deepcopy(o) for o in objects(session,True)
                 if identity(o.get('inventory',{}).get('pending','0')) in events}
        prior={event:copy.deepcopy(session.decisions[event]) for event in events if event in session.decisions}
        session.write_json(session.stage_output/'pending-work-before-departure.json',{'owner':owner['player'],
            'events':sorted(events),'sources':sources,'decisions':prior,'group':before,'newborns':newborns})
        # Production shutdown exports the committed numeric/private group. The
        # former process's held local notification cannot stand in for import.
        old_pid=leave(session,owner);held=None;departed=True
        def installed():
            active=[o for o in objects(session,True) if o['ownership_policy']==4 and o['life']==1]
            grants=set()
            for obj in active:
                endpoint=next(e for e in session.clients if e['player']==identity(obj['simulator']))
                numeric=next((v for v in session.states[endpoint['name']]['objects'] if identity(v['id'])==identity(obj['id'])),None)
                if not numeric or numeric['generation']!=obj['generation'] or numeric['owner_version']!=obj['owner_version'] \
                    or identity(numeric['simulation_owner'])!=identity(obj['simulator']):
                    return False
                grants.add((identity(numeric['simulation_owner']),numeric['group_version'],identity(numeric['ownership_group'])))
            return bool(active) and len(grants)==1 and next(iter(grants))[0]!=owner['player'] and session.settled()
        session.wait('whole-group-imported',installed)
        def replayed():
            active={identity(o['id']):o for o in objects(session,True)}
            return all(event in session.decisions for event in events) and all(
                key in active and identity(active[key].get('inventory',{}).get('pending','0')) not in events for key in sources)
        session.wait('original-work-replayed',replayed)
        active={identity(o['id']):o for o in objects(session,True)}
        moved={k for k in before if k in active and identity(active[k]['simulator'])!=owner['player']}
        transfers=[t for t in history(session,moved) if t['owner_version']>before[identity(t['object'])]['owner_version']]
        imported={identity(t['object']):t for t in transfers if t['frozen_state']==t['installed_state'] and int(t['installed_state'])}
        capability_keys={k for k in moved if before[k].get('capabilities')}
        valid=bool(capability_keys) and capability_keys.issubset(imported) and all(
            set(before[k]['capabilities']).issubset(imported[k].get('capabilities',[])) for k in capability_keys)
        if any(session.decisions[event]['decision_hash']!=d['decision_hash'] for event,d in prior.items()):
            raise RuntimeError('Handoff changed the original work decision')
        session.write_json(session.stage_output/'resource-group-import-contract.json',{'old_pid':old_pid,
            'original_events':sorted(events),'decisions':{e:session.decisions[e] for e in events},'imports':list(imported.values()),
            'moved':sorted(moved),'capability_hosts':sorted(capability_keys),'newborns':newborns})
        evidence=[session.stage_output/p for p in ('pending-work-before-departure.json','whole-group-imported.json',
                 'original-work-replayed.json','resource-group-import-contract.json')]
        session.prove('pending_work_handoff',valid and bool(sources),'Original pending processing event resolved after whole resource-group private import, retaining its immutable decision',evidence)
        session.prove('capability_handoff',valid,'Every resource capability host in the migrated group has a validated matching frozen/imported private-state digest',evidence)
        session.prove('newborn_handoff',valid and all(k in moved and active[k]['generation']==v['generation'] for k,v in newborns.items()),
                      'Real producer newborns migrated with their resource group and retained their stable life identity',evidence)
    finally:
        if held: release(session,owner,held)
        if departed:
            # A failed assertion must not leave five clients behind and let
            # unrelated UI phases accidentally continue with a missing seat.
            import sys,traceback
            original=sys.exc_info()[1]
            if original:
                session.write_json(session.stage_output/'resource-group-original-failure.json',{
                    'failure':str(original),'traceback':traceback.format_exc(),'states':session.states})
            if next(p for e,p in session.records if e['name']=='server').poll() is not None:
                from runtime_evidence import RuntimeEvidenceUnavailable
                raise RuntimeEvidenceUnavailable('Server already stopped after evidence capture; resource-owner restart cannot run') from original
            try:
                session.restart_owned(owner['name']);returned(session,owner)
            except RuntimeError as error:
                from runtime_evidence import RuntimeEvidenceUnavailable
                raise RuntimeEvidenceUnavailable('Resource-group owner replacement did not recover the shared room; '
                    +'original failure='+str(original)+'; replacement='+str(error)) from error
    session.finish()


def verify_member_join(session):
    session.begin('joined-peer-state-readback')
    session.wait('joined-directory',session.settled)
    previous=next((s for s in reversed(session.stages) if s['status']=='passed' and
                   (s['id'].endswith('fixed-role-abrupt-reconnect') or s['id'].endswith('fixed-role-reconnect'))),None)
    if not previous:
        from integrated_remaining import PrerequisiteMissing
        raise PrerequisiteMissing('Member join readback requires a completed stopped/restarted fixed-role stage')
    import json
    path=session.output/'stages'/previous['id']/'fixed-role-result.json'
    result=json.loads(path.read_text(encoding='utf-8-sig'))
    from runtime_evidence import ownership_views_agree
    views=[session.states[e['name']] for e in session.clients]
    returned=next((e for e in session.clients if e['player']==result['player']),None)
    valid=bool(returned and result['old_pid']!=result['returned_pid'] and
        session.states[returned['name']]['pid']==result['returned_pid'] and ownership_views_agree(views) and
        all(any(identity(m['player'])==result['player'] and m['online'] and m['role']==result['seat'] and m['team']==result['team']
                for m in v['role_members']) for v in views))
    session.prove('member_join',valid,'The same fixed-role identity returned in a new PID with matching persistent grants; transient births use their own receipts',
                  [path,session.stage_output/'joined-directory.json'])
    session.finish()


def resource_group(session):
    session.begin('whole-match-resource-authority')
    snapshots=[session.states[e['name']] for e in session.clients]
    group_views=[]
    for state in snapshots:
        members=[o for o in state['objects'] if o.get('ownership_policy')==4 and o.get('life')!=3]
        if not members or any(not int(identity(o['ownership_group']),16) for o in members):
            raise RuntimeError('Resource definition is not in the indivisible group')
        owners={(identity(o['simulation_owner']),o['group_version'],identity(o['ownership_group'])) for o in members}
        if len(owners)!=1: raise RuntimeError('Resource chain was split between owners or versions')
        group_views.append((next(iter(owners)),{o['id'] for o in members}))
        commander=next((m for m in state['role_members'] if m['online'] and identity(m['player'])==next(iter(owners))[0]),None)
        if not commander: raise RuntimeError('Resource owner is offline')
        if any(m['online'] and m['role']=='Commander' for m in state['role_members']) and commander['role']!='Commander':
            raise RuntimeError('Resource group failed to return to an online Commander')
    if any(v!=group_views[0] for v in group_views): raise RuntimeError('Resource group replicas disagree')
    session.prove('resource_group',True,'Both accounts, neutral resources and authored resource capability hosts share one live owner/version',
                  [session.stage_output/'baseline.json'])
    session.finish()
