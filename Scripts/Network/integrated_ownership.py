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
    # Real authored production and capability work supplies entities to transfer.
    buildings(session)
    teleport_and_cards(session)
    session.begin('capability-work-and-newborn-handoff')
    commander=entry(session,'Red','Commander')
    live={o['id']:o for o in objects(session,True) if o['life']==1 and o['team']==1}
    keys={k for k,o in directory(session).items() if k in live and o['policy']==1 and identity(o['controller'])==commander['player']}
    newborns={k for k in keys if live[k].get('parent') and int(identity(live[k]['parent']),16)}
    if not newborns: raise RuntimeError('Production did not produce transferable children')
    select(session,commander,keys)
    session.wait('source-authority',lambda:same_owners(session,keys,commander['player']) and session.settled())
    before=copy.deepcopy(directory(session));start_events=set(session.decisions)
    held=arm(session,commander,'receipt_hold',count=256)
    try:
        session.control('gameplay',enable_mining=True)
        session.wait('receipt-intercepted',lambda:fault(session,commander).get('intercepted',0)>0)
        pending={identity(v['event']) for v in fault(session,commander).get('messages',[])}
        loads(session,{'Commander':5.,'Ground':.1,'Air':.1})
        select(session,commander,set())
        session.wait('private-state-offloaded',lambda:any(identity(directory(session)[k]['owner'])!=commander['player'] for k in newborns),timeout=60)
        moved={k for k in keys if identity(directory(session)[k]['owner'])!=commander['player']}
        release(session,commander,held);held=None
        select(session,commander,keys)
        session.wait('work-rebound',lambda:same_owners(session,keys,commander['player']) and session.settled()
            and all(event in session.decisions for event in pending))
        transfers=[t for t in history(session,moved) if t['owner_version']>before[identity(t['object'])]['owner_version']]
        valid=bool(transfers) and all(t['frozen_state']==t['installed_state'] and int(t['installed_state']) for t in transfers)
        prove(session,'pending_work_handoff',valid and bool(pending),
              'Actual result deliveries were held across owner changes and retained their original event/result on release',
              'receipt-intercepted.json','private-state-offloaded.json','work-rebound.json')
        prove(session,'newborn_handoff',valid and bool(newborns.intersection(moved)),
              'A production-born entity transferred with the same stable life identity',
              'baseline.json','private-state-offloaded.json','work-rebound.json')
        transferred_caps={name for t in transfers for name in t.get('capabilities',[])}
        expected={name for k in moved for name in live[k].get('capabilities',{})}
        prove(session,'capability_handoff',valid and bool(expected) and expected.issubset(transferred_caps),
              'Authored capability-bearing objects completed validated private imports; no required capability was omitted',
              'baseline.json','private-state-offloaded.json','work-rebound.json')
        # Stable work identity and original source are observable in the committed
        # result, including sources retired by construction-to-unit conversion.
        retired={o['id'] for o in objects(session) if o['life']>=2}
        retained=[v for v in session.decisions.values() if v.get('work_source') in retired and int(identity(v.get('work_event','0')),16)]
        prove(session,'retained_source_work',bool(retained),
              'A retired source has a completed independently identified work result; repeated observations retain the same result hash',
              'work-rebound.json')
    finally:
        if held: release(session,commander,held)
        loads(session,{'Commander':-1.,'Ground':-1.,'Air':-1.})
    session.finish()
    session.begin('handoff-message-delay-timeout-and-retry')
    # Offload a real object, then delay its owner's handoff lane. Other objects
    # and all network control paths must keep progressing during the five seconds.
    loads(session,{'Commander':5.,'Ground':.1,'Air':.1});select(session,commander,set())
    session.wait('available-peer-owner',lambda:any(identity(directory(session)[k]['owner'])!=commander['player'] for k in keys),timeout=60)
    key=next(k for k in keys if identity(directory(session)[k]['owner'])!=commander['player'])
    source=next(e for e in session.clients if e['player']==identity(directory(session)[key]['owner']))
    held=arm(session,source,'handoff_hold',count=1)
    try:
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


def verify_member_join(session):
    session.begin('joined-peer-state-readback')
    session.wait('joined-directory',session.settled)
    previous=next((s for s in reversed(session.stages) if s['status']=='passed' and
                   (s['id'].endswith('fixed-role-abrupt-reconnect') or s['id'].endswith('fixed-role-reconnect'))),None)
    if not previous: raise RuntimeError('No actual stopped/restarted member stage')
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
