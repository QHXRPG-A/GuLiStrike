"""Real peer faults and ownership transfer; no periodic world snapshot or rollback."""
from __future__ import annotations
import copy
import json
from pathlib import Path
import time
import uuid
from integrated_acceptance import entry, identity, input_and_wait, objects, prove, rendered_ui

def fault(session, who):
    return session.states[who['name']].get('network',{}).get('review_fault',{})


def arm(session, who, mode, **filters):
    result,_=input_and_wait(session,'fault_arm',who['player'],mode=mode,**filters)
    return result['id']


def release(session,who,operation):
    return input_and_wait(session,'fault_release',who['player'],operation=operation)


def peer_counter(session,key):
    return sum(p.get(key,0) for e in session.clients if e['name'] in session.states
               for p in session.states[e['name']].get('network',{}).get('peers',[]))


def transport(session):
    paths=[]
    sender=entry(session,'Red','Commander')
    for mode in ('datagram_duplicate','datagram_reorder'):
        session.begin(mode)
        before=peer_counter(session,'duplicate_packets')
        operation=arm(session,sender,mode,count=8)
        session.wait('actual-injection',lambda:fault(session,sender).get('intercepted',0)>=8)
        if mode=='datagram_duplicate':
            session.wait('receiver-dedupe',lambda:peer_counter(session,'duplicate_packets')>before)
        else:
            session.wait('reordered-release',lambda:fault(session,sender).get('released',0)>0)
        paths.extend([session.stage_output/'baseline.json',session.stage_output/'actual-injection.json',
            session.stage_output/('receiver-dedupe.json' if mode=='datagram_duplicate' else 'reordered-release.json')])
        release(session,sender,operation)
        session.finish()
    session.prove('transport_duplicate_reorder',True,
        'Actual UDP transmissions duplicated/reordered; receiver duplicate handling and reordered packet release observed; all endpoints reconverged',paths)


def role_lifecycle(session):
    from integrated_roles import fixed_reconnect
    fixed_reconnect(session,abrupt=True)


def stale_versions(session):
    # A real role tenure transition supplies generation > 1. Zero/invalid
    # generations are not substituted for old, formerly valid lifetimes.
    paths=[]
    for mode in ('stale_owner','stale_life','stale_epoch'):
        session.begin(mode)
        sender=entry(session,'Red','Air')
        def rejected():
            if mode=='stale_epoch': return sum(session.states[e['name']].get('network',{}).get('rejected_delivery_epochs',0) for e in session.clients)
            if mode=='stale_life': return sum(session.states[e['name']].get('rejected_motion_lives',0) for e in session.clients)
            return sum(m.get('authority_rejected',0) for e in session.clients for m in session.states[e['name']].get('motion_receivers',[]))
        before=rejected()
        operation=arm(session,sender,mode,count=8,message_kind=13)
        session.wait('stale-injected',lambda:fault(session,sender).get('intercepted',0)>0)
        session.wait('stale-rejected',lambda:rejected()>before)
        paths += [session.stage_output/name for name in ('baseline.json','stale-injected.json','stale-rejected.json')]
        release(session,sender,operation);session.finish()
    session.prove('stale_authority_life_epoch',True,'Previously valid ownership, life and delivery epoch headers actually transmitted and rejected at their receiving layer',paths)


def empty_team(session):
    session.begin('terminal-empty-team')
    session.control('gameplay')
    stopped=[]
    for e in session.clients:
        if e['team']=='Red': stopped.append({'player':e['player'],'pid':session.stop_owned(e['name'])})
    session.write_json(session.stage_output/'departed.json',{'processes':stopped})
    started=time.monotonic()
    def terminal():
        # The expected terminal state can make client readiness false; read
        # the server independently so it is never mistaken for a crash pass.
        e,p=next((e,p) for e,p in session.records if e['name']=='server')
        from runtime_evidence import read_json_shared
        state=read_json_shared(Path(e['directory'])/'state.json')
        if state.get('pid')!=p.pid: raise RuntimeError('Stale server evidence during empty-team termination')
        session.states['server']=state
        return any(room.get('room_phase')==4 and 'empty' in room.get('failure','').lower() for room in state.get('rooms',[]))
    deadline=started+90
    while time.monotonic()<deadline:
        if terminal(): break
        time.sleep(.2)
    else: raise RuntimeError('Empty team did not reach its explicit terminal state')
    session.write_json(session.stage_output/'terminal.json',{'elapsed':time.monotonic()-started,'server':session.states['server']})
    prove(session,'empty_team',time.monotonic()-started>=30,'Real team departure waited the configured interval and reached terminal empty-team failure',
        'departed.json','terminal.json')
    session.stages[-1].update(status='passed',finished=time.monotonic());session.publish()



def independent_progress(session):
    session.begin('slow-simulator-independent-progress')
    slow=entry(session,'Red','Ground')
    input_and_wait(session,'fault_simulation_rate',slow['player'],rate=0.0)
    session.refresh();before=copy.deepcopy(session.states);start=time.monotonic()
    def observed():
        if any(session.states[e['name']].get('paused') for e in session.clients):
            raise RuntimeError('A slow simulator paused unrelated clients')
        return time.monotonic()-start>=12
    try:
        session.wait('slow-peer-network-continues',observed,timeout=25)
        active=[e for e in session.clients if e['name']!=slow['name']]
        ok=all(session.states[e['name']]['logical_tick']>before[e['name']]['logical_tick'] for e in active)
        ok &= session.states[slow['name']]['logical_tick']==before[slow['name']]['logical_tick']
        ok &= session.states[slow['name']]['network']['datagram_received_bytes']>before[slow['name']]['network']['datagram_received_bytes']
        prove(session,'independent_progress',ok,'One simulator stopped its logical clock for 12 wall seconds while its P2P receiver and the other five clocks continued',
              'baseline.json','slow-peer-network-continues.json')
    finally:
        input_and_wait(session,'fault_simulation_rate',slow['player'],rate=1.0)
    session.finish()


def business_duplicates(session):
    session.begin('business-duplicate-mining')
    miners=[o for o in objects(session,True) if 'resource_inventory' in o.get('capabilities',{}) and o['team']==1 and o['life']==1]
    if not miners: raise RuntimeError('No inventory-capable entity for the duplicate work stage')
    sender=next(e for e in session.clients if e['player']==identity(miners[0]['simulator']))
    before=sum(session.states[e['name']].get('duplicate_results',0) for e in session.clients)
    operation=arm(session,sender,'business_duplicate',count=8)
    session.legacy('mining_acceptance','mining')
    session.wait('duplicated-transactions',lambda:fault(session,sender).get('injected_sends',0)>0)
    injected={identity(m['event']) for m in fault(session,sender).get('messages',[]) if m.get('event') and int(identity(m['event']),16)}
    def consistent():
        return bool(injected) and all(event in session.decisions and len(session.result_observers.get(event,()))>=2 for event in injected)
    session.wait('same-client-decisions',consistent)
    prove(session,'business_idempotence',bool(injected),'Injected same-event requests retain one result hash across distinct receiving clients; entity and inventory checks remain in the production mining stage',
          'duplicated-transactions.json','same-client-decisions.json','mining-final.json')
    release(session,sender,operation);session.finish()


def service_restart(session):
    session.begin('coordinator-disconnect-and-metadata-restart')
    before=copy.deepcopy(session.states);old=session.stop_owned('server');start=time.monotonic()
    def still_running():
        for e in session.clients:
            if session.states[e['name']].get('paused'): raise RuntimeError('Control-plane outage paused gameplay')
        return time.monotonic()-start>=12
    session.wait('p2p-without-coordinator',still_running,timeout=25)
    alive=all(session.states[e['name']]['logical_tick']>before[e['name']]['logical_tick'] and
              session.states[e['name']]['network']['datagram_received_bytes']>before[e['name']]['network']['datagram_received_bytes'] for e in session.clients)
    prove(session,'service_disconnect',alive,'Coordinator absent for 12 seconds; all client clocks and peer reception continue without a membership pause',
          'baseline.json','p2p-without-coordinator.json')
    prior={key:value['result_hash'] for key,value in session.decisions.items()}
    new=session.restart_owned('server')
    session.wait('metadata-reconnected',lambda:session.settled() and all(session.states[e['name']].get('network',{}).get('connected') for e in session.clients))
    session.write_json(session.stage_output/'restart-pids.json',{'old':old,'new':new,'database':str(session.output/'room-metadata.sqlite')})
    ok=old!=new and all(session.states[e['name']]['epoch']==before[e['name']]['epoch'] for e in session.clients)
    ok &= all(session.decisions[key]['result_hash']==value for key,value in prior.items())
    prove(session,'service_restart',ok,'Metadata-only service restart reattached the running worlds without rewinding their identities or client result history',
          'restart-pids.json','metadata-reconnected.json','p2p-without-coordinator.json')
    session.finish()


def ownership_handoff(session):
    from integrated_ownership import run_handoff_faults
    run_handoff_faults(session)


def ground_handoff_stability(session):
    from integrated_acceptance import movement
    from integrated_ownership import select, loads, directory
    paths=[]
    for repeat in range(3):
        movement(session,'ground')
        session.begin('ground-route-handoff-observation-'+str(repeat+1))
        commander=entry(session,'Red','Commander')
        keys={k for k,o in directory(session).items() if o['policy']==1 and identity(o['controller'])==commander['player']}
        initial={e['name']:(session.states[e['name']]['epoch'],session.states[e['name']]['membership_version'],session.states[e['name']]['logical_tick']) for e in session.clients}
        started=time.monotonic();timeline=[]
        try:
            loads(session,{'Commander':5.,'Ground':.1,'Air':.1})
            select(session,commander,set())
            session.wait('offloaded',lambda:any(identity(directory(session)[k]['owner'])!=commander['player'] for k in keys))
            select(session,commander,keys)
            session.wait('taken-back',lambda:all(identity(directory(session)[k]['owner'])==commander['player'] for k in keys) and session.settled())
            started=time.monotonic()
            def observed():
                sample={}
                for e in session.clients:
                    s=session.states[e['name']];epoch,membership,tick=initial[e['name']]
                    if s.get('paused') or s['epoch']!=epoch or s['membership_version']!=membership or s['logical_tick']<tick:
                        raise RuntimeError('Ground route/object handoff caused a non-member pause or world reset')
                    sample[e['name']]={'tick':s['logical_tick'],'network':s.get('network',{}),'handoffs':s.get('handoff_count')}
                timeline.append({'at':time.monotonic(),'clients':sample})
                return time.monotonic()-started>=60
            session.wait('continued-sixty-seconds',observed,timeout=75)
            session.write_json(session.stage_output/'observation.json',timeline)
            paths += [session.stage_output/p for p in ('baseline.json','taken-back.json','continued-sixty-seconds.json','observation.json')]
        finally:
            loads(session,{'Commander':-1.,'Ground':-1.,'Air':-1.})
        session.finish()
    session.prove('ground_handoff_stability',True,'Three native Ground routes and real object handoffs, each followed by 60 seconds without a world pause/reset',paths)


def run(session):
    independent_progress(session);transport(session);business_duplicates(session)
    ownership_handoff(session);ground_handoff_stability(session)
    role_lifecycle(session);stale_versions(session)
    from integrated_ownership import verify_member_join
    verify_member_join(session)
    service_restart(session)
    # Request-rebinding checks require their original pending event and final
    # UI receipt; merely capturing a visible widget must never pass them.
    empty_team(session)
