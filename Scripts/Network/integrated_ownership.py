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


def run_ui_selection_handoff(session):
    """The visible command cohort must survive its actual ownership takeover."""
    session.begin('commander-ui-cohort-through-handoff')
    commander=entry(session,'Red','Commander')
    try:
        loads(session,{'Commander':5.,'Ground':.1,'Air':.1})
        select(session,commander,set())
        def remote():
            return sorted((o for o in objects(session,True) if o['kind']==1 and o['team']==1 and o['life']==1
                and identity(o['simulator'])!=commander['player']),key=lambda o:o['ordinal'])
        session.wait('remote-live-cohort',lambda:bool(remote()))
        chosen=remote()[:12];keys={identity(o['id']) for o in chosen};ordinals={o['ordinal'] for o in chosen}
        before={k:copy.deepcopy(directory(session)[k]) for k in keys}
        action,receipt=input_and_wait(session,'selection_objects',commander['player'],
            objects=[o['id'] for o in chosen],command_selection=True)
        missing=[]
        def installed():
            if not same_owners(session,keys,commander['player']) or not session.settled(): return False
            shown=set(session.states[commander['name']]['scene_ui']['selected_soldiers'])
            if not ordinals.issubset(shown):
                missing.append(session.states[commander['name']]['snapshot_finished_qpc'])
                if len(set(missing))>=2:
                    raise RuntimeError('A live selected command cohort was removed from the UI during ownership takeover')
                return False
            replicas=[[o for e in session.clients for o in session.states[e['name']]['scene_objects']
                if identity(o['id'])==key] for key in keys]
            return all(len(values)==len(session.clients) and sum(o['local_simulation'] for o in values)==1
                and all(o['life']==1 and o['entity'] for o in values) for values in replicas)
        session.wait('owned-and-visibly-selected',installed,timeout=60)
        session.write_json(session.stage_output/'ui-handoff-contract.json',{'before':before,'selected_ordinals':sorted(ordinals),
            'transfers':history(session,keys),'selection_input':action})
        prove(session,'commander_ui_selection_handoff',installed(),
            'The same command cohort remains visibly selected after real takeover, with one simulator per live identity',
            receipt,'owned-and-visibly-selected.json','ui-handoff-contract.json')
    finally:
        loads(session,{'Commander':-1.,'Ground':-1.,'Air':-1.})
    session.finish()


def run_handoff_faults(session):
    from integrated_commander import construct, production, production_fixture, prepare_construction_site, prepare_population
    from population_combat_preparation import prepare_mining
    # The default 600-unit fixture must prepare the same real business hosts
    # as the fast spatial profile. Missing prerequisites are never a pass.
    # Do not rerun unrelated unfunded-placement or modal-card assertions in a
    # room that has already accumulated resources and other business history.
    session.population_preparation_only=True
    admitted={}
    # Reuse authored completed hosts and stop unrelated factories from eating
    # prepared population. Re-enable ordinary production before the exit test.
    with production_fixture(session,[]):
        prepare_construction_site(session)
        import json
        from pathlib import Path
        manifest=json.loads(Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
        batch=sum(int(float(manifest['numbers'][f'building/{definition}/ProductionCount'])) for definition in (4,101))
        prepare_population(session,batch,target_limit=64)
        for definition,offset in ((4,(0,1200)),(101,(0,-1200)),(102,(3600,-2400))):
            hosts=[o for o in objects(session,True) if o['definition']==f'building/{definition}'
                   and o['team']==1 and o['life']==1 and o['entity'] and o['completed']]
            if hosts: admitted[definition]=min(hosts,key=lambda o:o['ordinal'])['id']
            else: construct(session,definition,offset,admitted)
        production(session,admitted)
    prepare_mining(session)
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
        from integrated_commander import deliver_inventory
        host=next(o for o in objects(session,True) if o['id']==admitted[102] and o['life']==1
            and o['completed'] and o.get('inventory',{}).get('processing'))
        # Both profiles retain real extraction and public delivery. The
        # position fixture only removes the unrelated long approach.
        deliver_inventory(session,host,wait_for_processing=False)
        with session.preparation('Actual pending processing work for handoff'):
            session.wait('original-processing-pending',pending)
        events={identity(v['event']) for v in fault(session,owner)['messages']}
        sources={identity(o['id']):copy.deepcopy(o) for o in objects(session,True)
                 if identity(o.get('inventory',{}).get('pending','0')) in events}
        prior={event:copy.deepcopy(session.decisions[event]) for event in events if event in session.decisions}
        carried=deliver_inventory(session,host,prepare_only=True)
        cargo={color:carried['inventory'][color] for color in ('blue','red')}
        session.write_json(session.stage_output/'pending-work-before-departure.json',{'owner':owner['player'],
            'events':sorted(events),'sources':sources,'decisions':prior,'group':before,'newborns':newborns,
            'carried_cargo':carried})
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
        def resumed():
            active={identity(o['id']):o for o in objects(session,True)}
            return all(
                key in active and identity(active[key].get('inventory',{}).get('pending','0')) not in events for key in sources)
        session.wait('snapshot-work-resumed',resumed)
        quit_receipt=json.loads((session.stage_output/('exit-receipt-'+owner['name']+'.json')).read_text(encoding='utf-8-sig'))
        final_audit=quit_receipt.get('final_network_evidence',{})
        interrupted=final_audit.get('local_submission_rejections',[])
        for event in events:
            if sum(identity(v['event'])==event and v['status']==14 for v in interrupted)!=1:
                raise RuntimeError('The held old resource request lacks exactly one connection-interrupted notification: '+event)
        active={identity(o['id']):o for o in objects(session,True)}
        transported=active.get(identity(carried['id']))
        if not transported or transported['generation']!=carried['generation'] or transported['ordinal']!=carried['ordinal'] \
                or any(transported['inventory'][color]!=cargo[color] for color in cargo):
            raise RuntimeError('A stopped loaded miner lost its stable life or original cargo during handoff')
        new_owner=next(e for e in session.clients if e['player']==identity(transported['simulator']))
        delivery,delivery_input=input_and_wait(session,'inventory_delivery',new_owner['player'],source=carried['id'],target=host['id'])
        if not delivery['result'].get('committed'): raise RuntimeError('The imported loaded miner could not continue its public delivery')
        delivery_event=identity(delivery['request'])
        session.wait('imported-cargo-delivered',lambda:delivery_event in session.decisions
            and session.decisions[delivery_event]['kind']==10 and session.decisions[delivery_event]['status']==0
            and identity(session.decisions[delivery_event]['target'])==identity(host['id'])
            and identity(session.decisions[delivery_event].get('work_source','0'))==identity(carried['id'])
            and session.observed_by_active(delivery_event))
        session.write_json(session.stage_output/'carried-cargo-handoff-contract.json',{'before':carried,'imported':transported,
            'delivery_input':delivery_input,'decision':session.decisions[delivery_event]})
        moved={k for k in before if k in active and identity(active[k]['simulator'])!=owner['player']}
        snapshots={name:state.get('resource_snapshots',[]) for name,state in session.states.items() if name!='server'}
        snapshots[owner['name']]=final_audit.get('resource_snapshots',[])
        installed=[]
        for name,groups in snapshots.items():
            cuts=[g for g in groups if int(g.get('installed_recovery',0)) and len(g.get('digest',''))==64]
            if len(cuts)!=1: raise RuntimeError(name+': missing unique, complete resource snapshot installation evidence')
            installed.append(cuts[0])
        identities={(g['group'],g['installed_recovery'],g['snapshot'],g['snapshot_epoch'],g['sample'],g['digest']) for g in installed}
        if len(identities)!=1: raise RuntimeError('Clients did not install the identical complete resource snapshot')
        imported=[{identity(o['id']):o for o in g['installed_objects']} for g in installed]
        capability_keys={k for k in moved if before[k].get('capabilities')}
        valid=bool(capability_keys) and all(capability_keys.issubset(part) for part in imported) and all(
            set(before[k]['capabilities']).issubset(active[k].get('capabilities',[])) for k in capability_keys)
        if any(session.decisions[event]['decision_hash']!=d['decision_hash'] for event,d in prior.items()):
            raise RuntimeError('Handoff changed the original work decision')
        session.write_json(session.stage_output/'resource-group-import-contract.json',{'old_pid':old_pid,
            'original_events':sorted(events),'decisions':{e:session.decisions[e] for e in events if e in session.decisions},
            'interrupted_notifications':[v for v in interrupted if identity(v['event']) in events],
            'snapshots':snapshots,
            'moved':sorted(moved),'capability_hosts':sorted(capability_keys),'newborns':newborns})
        evidence=[session.stage_output/p for p in ('pending-work-before-departure.json','whole-group-imported.json',
                 'snapshot-work-resumed.json','resource-group-import-contract.json')]
        session.prove('pending_work_handoff',valid and bool(sources),'Every held old processing request was interrupted once; current snapshot tasks resumed without replaying the old request',evidence)
        session.prove('capability_handoff',valid,'Every resource capability host belongs to the identical complete snapshot installed on every client',evidence)
        session.prove('newborn_handoff',valid and all(k in moved and active[k]['generation']==v['generation'] for k,v in newborns.items()),
                      'Real producer newborns migrated with their resource group and retained their stable life identity',evidence)
        session.prove('carried_cargo_handoff',valid and bool(sum(cargo.values())),
            'A real loaded miner retained its exact cargo and stable life through group import, then delivered it at the new simulator',
            evidence+[session.stage_output/p for p in ('carried-cargo-before-handoff.json','imported-cargo-delivered.json','carried-cargo-handoff-contract.json')])
    finally:
        if held:
            # Preserve the membership/contract failure if the original process
            # exits while the fault cleanup is being requested.
            import sys,traceback
            original=sys.exc_info()[1]
            try:
                release(session,owner,held)
            except RuntimeError as cleanup_error:
                session.write_json(session.stage_output/'handoff-fault-cleanup-failure.json',{
                    'failure':str(cleanup_error),'original_failure':str(original) if original else None,
                    'traceback':traceback.format_exc()})
                if original is None: raise
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
