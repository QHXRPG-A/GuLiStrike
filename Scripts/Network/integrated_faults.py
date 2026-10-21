"""Real peer faults, ownership transfer and current-state resource recovery."""
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
    from acceptance_session import preparation
    @preparation
    def prepare_members(s):
        from integrated_commander import fund
        for team in ('Red','Blue'):
            fund(s,{'blue':128,'red':128},team=team)
        s.begin('wingman-vacancy-preparation')
        for team in ('Red','Blue'):
            endpoint=entry(s,team,'Air')
            if any(m['team']==team and m['life_state']==1 for m in s.states[endpoint['name']].get('wingman_states',[])):
                continue
            choices=[v for v in s.states[endpoint['name']]['scene_ui']['ship_choices'] if v['available']]
            if not choices:
                raise RuntimeError('No available assembly choice for vacancy lifecycle preparation')
            action,_=input_and_wait(s,'ship_equipment',endpoint['player'],node=choices[0]['node'])
            if not action['result'].get('committed'):
                raise RuntimeError('Vacancy preparation assembly did not commit')
        s.wait('real-wingmen-before-vacancy',lambda:all(any(m['team']=='Red' and m['life_state']==1 and m['entity']
            for m in s.states[e['name']].get('wingman_states',[])) for e in s.clients))
        s.finish()
    prepare_members(session)
    from integrated_roles import fixed_reconnect
    fixed_reconnect(session,abrupt=True)


def stale_versions(session):
    # A real role tenure transition supplies generation > 1. Zero/invalid
    # generations are not substituted for old, formerly valid lifetimes.
    from acceptance_contract import digest
    manifest=json.loads(Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
    strings=manifest.get('strings',{});prior=None
    if strings.get('acceptance/prior_match'):
        path=Path(strings['acceptance/prior_launch'])
        if digest(path)!=strings['acceptance/prior_launch_sha256']: raise RuntimeError('Prior match preparation provenance changed')
        previous=json.loads(path.read_text(encoding='utf-8-sig'))
        if identity(previous['room'])!=identity(strings['acceptance/prior_match']) or identity(previous['room'])==identity(session.plan['room']):
            raise RuntimeError('Old-match preparation does not identify a different actual launched room')
        prior={'prior_match':identity(previous['room']),'prior_epoch':int(strings['acceptance/prior_epoch']),
               'launch':str(path),'sha256':digest(path)}
    paths=[];completed=[];failures=[]
    def collect_mode(mode):
        if mode=='stale_epoch' and not prior and max(session.states[e['name']].get('epoch',0) for e in session.clients)<=1:
            from acceptance_session import PreparationSkipped
            raise PreparationSkipped('No formerly valid prior match epoch is available in this room; epoch zero is not substituted for the old-match assertion')
        session.begin(mode)
        with session.preparation('Previously valid motion header '+mode):
            def eligible():
                active_players={e['player'] for e in session.clients if e['name'] in session.states}
                return [o for o in objects(session,True) if o['life']==1 and o['entity'] and o['kind'] in (1,2,3,4)
                    and identity(o['simulator']) in active_players
                    and (o['owner_version']>1 if mode=='stale_owner' else o['generation']>1 if mode=='stale_life' else True)]
            session.wait('eligible-motion-source',lambda:bool(eligible()))
            source=eligible()[0]
            sender=next(e for e in session.clients if e['player']==identity(source['simulator']))
            session.write_json(session.stage_output/'motion-source-preparation.json',{'source':source,'sender':sender['player'],
                'scope':'An actual living local simulator with a positive formerly valid ownership/life header; no fabricated generation zero.'})
        def rejected():
            active=[e for e in session.clients if e['name'] in session.states]
            if mode=='stale_epoch': return sum(session.states[e['name']].get('network',{}).get('rejected_delivery_identity' if prior else 'rejected_delivery_epochs',0) for e in active)
            if mode=='stale_life': return sum(session.states[e['name']].get('rejected_motion_lives',0)
                +sum(m.get('authority_rejected',0) for m in session.states[e['name']].get('motion_receivers',[])) for e in active)
            return sum(m.get('authority_rejected',0) for e in active for m in session.states[e['name']].get('motion_receivers',[]))
        before=rejected()
        filters={k:prior[k] for k in ('prior_match','prior_epoch')} if mode=='stale_epoch' and prior else {}
        operation=arm(session,sender,mode,count=8,message_kind=13,**filters)
        try:
            with session.preparation('Eligible original motion transmission '+mode):
                session.wait('stale-injected',lambda:fault(session,sender).get('injected_sends',0)>0)
            messages=fault(session,sender).get('transmitted_messages',[])
            if not messages: raise RuntimeError('No actual stale datagram header was observed at the transport send')
            if filters:
                if not messages or any(identity(v['match'])!=prior['prior_match'] or v['epoch']!=prior['prior_epoch'] or v['epoch']<=0 for v in messages):
                    raise RuntimeError('Injected header does not retain the actual prior positive match identity')
                session.write_json(session.stage_output/'prior-match-provenance.json',prior);paths.append(session.stage_output/'prior-match-provenance.json')
            def exact_rejection():
                fields=('match','sender','object','kind','epoch','owner_version','generation','sent_at')
                def signature(value):
                    return tuple(identity(str(value[k])) if k in ('match','sender','object') else value[k] for k in fields)
                for sent in fault(session,sender).get('transmitted_messages',[]):
                    receiver=next(e for e in session.clients if e['player']==identity(sent['peer']))
                    state=session.states[receiver['name']]
                    received=state.get('network',{}).get('recent_header_rejections',[]) if mode=='stale_epoch' else state.get('recent_receive_rejections',[])
                    if any(signature(value)==signature(sent) for value in received): return True
                return False
            session.wait('stale-rejected',lambda:rejected()>before and exact_rejection())
            paths.extend(session.stage_output/name for name in ('baseline.json','stale-injected.json','stale-rejected.json'))
        finally:
            release(session,sender,operation)
        session.finish()
        completed.append(mode)
    modes=('stale_owner','stale_life','stale_epoch')
    if getattr(session.args,'collect_only',False):
        from integrated_remaining import attempt
        for mode in modes:
            attempt(session,mode,lambda s,m=mode:collect_mode(m),failures)
        session.write_json(session.output/'stale-version-collection.json',{'passed_subcases':completed,'failures':failures,
            'scope':'Independent old-header branches; the aggregate assertion requires all three in this room.'})
    else:
        for mode in modes: collect_mode(mode)
    if len(completed)==len(modes):
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


def replay_credit_fixture(session, manifest):
    """Use one admitted setup credit as the original business request, then replay its exact cached bytes."""
    session.begin('business-duplicate-original-credit')
    commander=entry(session,'Red','Commander')
    definition=manifest['strings']['army/root_definition']
    source=next(o for o in session.states[commander['name']]['objects']
                if o['definition']==definition and o['life']==1 and not o.get('dormant')
                and identity(o['controller'])==commander['player'])
    owner=next(e for e in session.clients if e['player']==identity(source['simulation_owner']))
    original_input,original_receipt=input_and_wait(session,'resource_supply',owner['player'],team='Red',color='blue',amount=128)
    if not original_input['result'].get('committed'):
        raise RuntimeError('Original business credit did not actually commit')
    event=identity(original_input['request'])
    session.wait('original-business-committed',lambda:event in session.decisions and session.decisions[event]['kind']==1
                 and session.decisions[event]['status']==0 and session.observed_by_active(event))
    original=dict(session.decisions[event])
    session.write_json(session.stage_output/'original-business-request.json',{'input':original_input,'source':source,
        'owner':owner['player'],'decision':original,
        'scope':'Existing authorized setup credit uses the actual numeric business path. It supplies a real original request for idempotence; it is not mining or production evidence.'})
    # The existing replay input releases its own injection immediately after
    # returning the eight authenticated local deliveries; no second release.
    action,replay_receipt=input_and_wait(session,'fault_arm',owner['player'],mode='business_duplicate',count=8,
                                       business_kind=1,source=source['id'],replay_event=event)
    proof=action['result']
    session.write_json(session.stage_output/'native-business-replay.json',{'native':proof,'original':original,
        'scope':'Eight exact original requests delivered to the current authenticated numeric owner; no UDP transmission claimed.'})
    session.wait('same-client-decisions',lambda:event in session.decisions
                 and session.decisions[event]['decision_hash']==original['decision_hash']
                 and len(session.result_observers.get(event,()))>=2)
    valid=(proof.get('scope')=='local-authenticated-request-replay' and proof.get('local_replays')==8
           and proof.get('duplicate_delta')==8 and bool(proof.get('numeric_before_sha256'))
           and proof.get('numeric_before_sha256')==proof.get('numeric_after_sha256')
           and proof.get('decision_hash_before')==proof.get('decision_hash_after')==original['decision_hash']
           and identity(proof.get('original_event','0'))==event and bool(proof.get('request_fingerprint_sha256')))
    prove(session,'business_idempotence',valid,
          'An actual admitted same-event credit retained its original request fingerprint and immutable decision across clients; eight exact local replays returned eight duplicates without changing numeric fragments. G mining retains its independent real extraction, FIFO and conservation assertions.',
          original_receipt,'original-business-committed.json','original-business-request.json',replay_receipt,
          'native-business-replay.json','same-client-decisions.json')
    session.finish()


def business_duplicates(session):
    manifest=json.loads(Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
    if manifest.get('strings',{}).get('acceptance/preparation')=='gf-fast-spatial-v2':
        return replay_credit_fixture(session,manifest)
    from population_combat_preparation import prepare_mining
    from acceptance_session import PreparationSkipped
    prepare_mining(session)
    session.begin('business-duplicate-mining')
    miners=[o for o in objects(session,True) if o.get('inventory',{}).get('cargo') and o['team']==1 and o['life']==1]
    if not miners:
        raise PreparationSkipped('No actual cargo-capable carrier for the duplicate work fixture')
    sender=next(e for e in session.clients if e['player']==identity(miners[0]['simulator']))
    decision_start=session.last_decision
    operation=arm(session,sender,'business_duplicate',count=8)
    arguments={'gameplay_timeout':session.args.preparation_timeout} if getattr(session.args,'collect_only',False) else None
    try:
        try:
            session.legacy('mining_acceptance','mining',arguments=arguments)
        except RuntimeError as error:
            if getattr(session.args,'collect_only',False):
                raise PreparationSkipped('Real mining fixture was not ready within the short preparation budget: '+str(error)) from error
            raise
        paths=['mining-final.json']
        local_proof=None
        with session.preparation('Actual original-request duplicate injection'):
            if fault(session,sender).get('injected_sends',0):
                session.wait('duplicated-transactions',lambda:fault(session,sender).get('injected_sends',0)>=8)
                injected={identity(m['event']) for m in fault(session,sender).get('messages',[])
                          if m.get('event') and int(identity(m['event']),16)}
                paths.append('duplicated-transactions.json')
            else:
                # A resource group normally executes entirely at one owner, so
                # there is no transaction datagram for the peer hook to copy.
                # Replay that owner's recorded canonical request through the
                # same production ReceivePeer/StartWork handler instead.
                release(session,sender,operation);operation=None
                def candidates():
                    return [(event,value) for event,value in session.decisions.items()
                            if value['sequence']>decision_start and value['status']==0
                            and value['kind'] in (9,10,11) and len(session.result_observers.get(event,()))>=2]
                session.wait('original-mining-decisions',lambda:bool(candidates()))
                event,original=max(candidates(),key=lambda pair:(pair[1]['kind']==11,pair[1]['sequence']))
                action,receipt=input_and_wait(session,'fault_arm',sender['player'],mode='business_duplicate',count=8,replay_event=event)
                operation=None  # The native exact-request replay releases its own arm.
                local_proof=action['result']
                session.write_json(session.stage_output/'native-business-replay.json',{
                    'native':local_proof,'original':original,'input':str(receipt),
                    'scope':'Eight original canonical requests delivered to the authenticated local numeric owner. This branch does not claim UDP transmission.'})
                injected={event};paths.extend(['native-business-replay.json','original-mining-decisions.json',receipt])
        def consistent():
            return bool(injected) and all(event in session.decisions and len(session.result_observers.get(event,()))>=2 for event in injected)
        session.wait('same-client-decisions',consistent)
        valid=bool(injected)
        if local_proof is not None:
            valid &= (local_proof.get('scope')=='local-authenticated-request-replay'
                      and local_proof.get('local_replays')==8 and local_proof.get('duplicate_delta')==8
                      and local_proof.get('numeric_before_sha256')==local_proof.get('numeric_after_sha256')
                      and bool(local_proof.get('numeric_before_sha256'))
                      and local_proof.get('decision_hash_before')==local_proof.get('decision_hash_after')==original['decision_hash']
                      and identity(local_proof.get('original_event','0'))==event
                      and bool(local_proof.get('request_fingerprint_sha256')))
        paths.append('same-client-decisions.json')
        prove(session,'business_idempotence',valid,
              'Original same-event requests retain the complete immutable decision across receiving clients; local replay also proves eight duplicate results and unchanged numeric fragments. Production mining retains its FIFO, conservation and single-consumption checks.',*paths)
    finally:
        if operation: release(session,sender,operation)
    session.finish()


def service_restart(session):
    from integrated_commander import fund, prepare_construction_site
    # Use the same public placement input to exercise late numeric birth
    # delivery when the metadata link is absent for longer than its defer TTL.
    if not any(s['status']=='passed' and s['id'].endswith('construction-site-input') for s in session.stages):
        prepare_construction_site(session)
    fund(session,{'blue':64,'red':64})
    session.begin('coordinator-disconnect-and-metadata-restart')
    before=copy.deepcopy(session.states);old=session.stop_owned('server');start=time.monotonic()
    try:
        ground=entry(session,'Red','Ground')
        pawn=next(o for o in objects(session,True) if o['definition']=='player/ground' and o['team']==1 and o['life']==1)
        point=list(pawn['position']);point[0]+=1200
        previous={o['id'] for o in objects(session,True)}
        birth_action,birth_input=input_and_wait(session,'building_place',ground['player'],definition=5,point=point)
        if not birth_action['result'].get('accepted'):
            raise RuntimeError('Placement during the control-plane outage did not reach a real accepted birth')
        session.wait('owner-born-without-metadata',lambda:any(o['id'] not in previous and o['definition']=='building/5'
            and o['life']==1 for o in objects(session,True)))
        newborn=next(o['id'] for o in objects(session,True) if o['id'] not in previous and o['definition']=='building/5' and o['life']==1)
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
    finally:
        new=session.restart_owned('server')
    session.wait('metadata-reconnected',lambda:session.settled() and all(session.states[e['name']].get('network',{}).get('connected') for e in session.clients))
    session.wait('late-birth-recovered-on-six',lambda:all(any(o['id']==newborn and o['life']==1 and o['entity']
        for o in session.states[e['name']]['scene_objects']) for e in session.clients))
    session.write_json(session.stage_output/'restart-pids.json',{'old':old,'new':new,'database':str(session.output/'room-metadata.sqlite')})
    ok=old!=new and all(session.states[e['name']]['epoch']==before[e['name']]['epoch'] for e in session.clients)
    ok &= all(session.decisions[key]['result_hash']==value for key,value in prior.items())
    prove(session,'service_restart',ok,'Metadata-only restart retained world/result history and recovered the original real birth admitted while control was absent',
          'restart-pids.json','metadata-reconnected.json','p2p-without-coordinator.json',birth_input,
          'owner-born-without-metadata.json','late-birth-recovered-on-six.json')
    session.finish()


def ownership_handoff(session):
    from integrated_ownership import run_handoff_faults
    run_handoff_faults(session)


def retained_source_work(session):
    from population_combat_preparation import prepare_ground_duel, position
    prepare_ground_duel(session)
    session.begin('source-death-before-retained-hit-work')
    source=entry(session,'Blue','Ground')
    source_object=next(o for o in objects(session,True) if o['team']==2 and o['definition']=='player/ground')
    target=next(o for o in objects(session,True) if o['team']==1 and o['definition']=='player/ground' and o['life']==1)
    with session.preparation('Retained-source initial hit'):
        position(session,source_object,[8000,-500,source_object['position'][2]])
        position(session,target,[8000,500,target['position'][2]])
    # Damage work is keyed by its retained flight, while the original Ground
    # source is in the linked hit fact. Validate that exact original below.
    operation=arm(session,source,'work_hold',count=256,business_kind=0)
    try:
        # Establish an actual original hit before allowing the one-shot target
        # to die. Both damage paths still use native aim/fire and geometry.
        with session.preparation('Retained-source pending work'):
            aim=next(o['position'] for o in objects(session,True) if o['id']==target['id'])
            session.control('ground_combat',ground_inputs=[{'player':source['player'],'move_x':0,'move_y':0,
                'aim_x':aim[0],'aim_y':aim[1],'aim_z':aim[2],'sprint':False,'fire':True,'rocket':False}])
            session.wait('original-native-hit-held',lambda:fault(session,source).get('intercepted',0)>0)
        session.control()
        # The original Blue hit is already held above. Keep that source from
        # creating hundreds of extra hits while Red performs the real kill:
        # continued return fire exhausts the bounded hold and kills Red first.
        # This preserves the required attack-before-death ordering and does
        # not change health, damage, cadence or the all-client death assertion.
        # The duel already has a validated clear lane. Use the existing short
        # approach fixture, retaining real movement, fire and all-client death;
        # the unrelated map-wide tour can take a fleeing target out of sight.
        session.legacy('ground_combat_acceptance','ground_combat',arguments={'target_return_fire':False,'history_route_required':False,'continue_remaining':True})
        session.refresh()
        events={identity(m['event']) for m in fault(session,source).get('messages',[])}
        session.write_json(session.stage_output/'post-source-death-before-release.json',{
            'source':source_object['id'],'fault':fault(session,source),'original_events':sorted(events),
            'already_decided':{e:session.decisions[e] for e in events if e in session.decisions},
            'source_lives':{e['name']:next((o for o in session.states[e['name']]['scene_objects']
                if o['id']==source_object['id']),None) for e in session.clients}})
        if not events or any(e in session.decisions for e in events):
            raise RuntimeError('Source death requires original hit work held before its damage decision')
        dead=all(any(o['id']==source_object['id'] and o['life']>=2 for o in session.states[e['name']]['scene_objects']) for e in session.clients)
        if not dead: raise RuntimeError('The original Ground source did not genuinely die on all six clients')
        hits={e:session.hits[e] for e in events if e in session.hits}
        if set(hits)!=events or any(identity(h['source'])!=identity(source_object['id']) for h in hits.values()):
            raise RuntimeError('Held hit work does not identify the original dead source')
        session.write_json(session.stage_output/'source-dead-work-pending.json',{'source':source_object,'hits':hits,
            'fault':fault(session,source),'states':session.states})
        release(session,source,operation);operation=None
        session.wait('original-work-after-death',lambda:all(e in session.decisions and session.decisions[e]['kind']==0
            and session.decisions[e]['status']==0 and session.decisions[e]['applied_damage']>0
            and identity(session.decisions[e]['target'])==identity(hits[e]['target'])
            and session.observed_by_active(e) for e in events))
        session.write_json(session.stage_output/'retained-source-result-contract.json',{'source':source_object['id'],
            'original_hits':hits,'final_decisions':{e:session.decisions[e] for e in events}})
        prove(session,'retained_source_work',True,'Actual original native hit events were held until their Ground source died, then committed the same target damage once and were observed on all six clients',
            'source-dead-work-pending.json','original-work-after-death.json','retained-source-result-contract.json')
    finally:
        if operation: release(session,source,operation)
    session.finish()


def ground_handoff_stability(session):
    from integrated_acceptance import movement
    from integrated_ownership import select, loads, directory
    paths=[]
    for repeat in range(3):
        movement(session,'ground')
        session.begin('ground-route-handoff-observation-'+str(repeat+1))
        commander=entry(session,'Red','Commander')
        living={identity(o['id']) for o in objects(session,True) if o['life']==1 and o['entity']}
        keys={k for k,o in directory(session).items() if k in living and o['policy']==1 and identity(o['controller'])==commander['player']}
        initial={e['name']:(session.states[e['name']]['epoch'],session.states[e['name']]['membership_version'],session.states[e['name']]['logical_tick']) for e in session.clients}
        started=time.monotonic();timeline=[]
        try:
            with session.preparation('Ground handoff observation source ownership'):
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


def hybrid_sync_mixed(session):
    """The synchronization regression shares one room and its accumulated history."""
    from integrated_acceptance import prepare_cohort
    from integrated_ownership import resource_group, run_selection, run_ui_selection_handoff, resource_work_handoff
    from integrated_commander import buildings, teleport_landing, teleport_cancel
    from integrated_roles import fixed_reconnect
    from integrated_supply import unpaid
    prepare_cohort(session)
    resource_group(session)
    run_selection(session)
    run_ui_selection_handoff(session)
    unpaid(session)
    admitted=buildings(session)
    teleport_landing(session)
    teleport_cancel(session)
    resource_work_handoff(session,admitted)
    fixed_reconnect(session)
    # Deliberate source death is last; production setup needs the living Blue
    # Ground participant to release population through real weapon damage.
    retained_source_work(session)


def run(session):
    from integrated_ui_rebind import run as ui_rebind
    from integrated_resource_recovery import owner_process_loss, backup_process_loss
    from integrated_remaining import attempt
    from runtime_evidence import RuntimeEvidenceUnavailable
    from integrated_ownership import verify_member_join, ordinary_handoff_faults, run_ui_selection_handoff
    failures=[]
    phases=[('hybrid-sync-mixed',hybrid_sync_mixed),
            ('selection-ui-handoff',run_ui_selection_handoff),
            ('independent-progress',independent_progress),('transport',transport),
            ('service-restart',service_restart),('ground-handoff-stability',ground_handoff_stability),
            ('business-idempotence',business_duplicates),
            ('ordinary-handoff',ordinary_handoff_faults),('ownership-handoff',ownership_handoff),
            ('resource-owner-process-loss',owner_process_loss),
            ('resource-backup-process-loss',backup_process_loss),
            # Population setup needs the living Blue Ground shooter. Complete
            # resource/newborn preparation before deliberately killing it.
            ('retained-source-work',retained_source_work),
            # The pending Air UI request consumes the available assembly
            # choice. Vacancy preparation then reuses those actual wingmen.
            ('ui-rebind',ui_rebind),('member-loss-rejoin',role_lifecycle),
            ('stale-versions',stale_versions),('member-join',verify_member_join),('empty-team',empty_team)]
    requested=getattr(session.args,'collect_fault_steps',None)
    if not requested:
        # This focused composition is opt-in. The existing full F suite keeps
        # its own independent preparation and assertion sequence.
        phases=[phase for phase in phases if phase[0] not in ('hybrid-sync-mixed','selection-ui-handoff')]
    if requested:
        available={label for label,_ in phases}
        if set(requested)-available:
            raise RuntimeError('Unknown existing F phases: '+str(sorted(set(requested)-available)))
        phases=[phase for phase in phases if phase[0] in requested]
    role=getattr(session.args,'collect_ui_role',None)
    if role:
        from integrated_ui_rebind import collect_role
        phases=[('ui-rebind',lambda s:collect_role(s,role))]
    session.write_json(session.output/'planned-fault-sweep.json',{'items':[name for name,_ in phases]})
    manifest=json.loads(Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
    if manifest.get('strings',{}).get('acceptance/preparation')=='gf-fast-spatial-v2' and any(name=='ownership-handoff' for name,_ in phases):
        def prepare_hosts(s):
            from integrated_commander import prepare_construction_site,prepare_population,construct,fund
            # This fault case checks snapshot recovery. A separate gameplay
            # case owns the real death-release assertion; never fake its proof.
            s.population_preparation_only=True
            prepare_construction_site(s)
            prepare_population(s,32,target_limit=50)
            admitted={}
            for definition,offset in ((4,(0,1200)),(101,(0,-1200)),(102,(3600,-2400))):
                construct(s,definition,offset,admitted)
            fund(s,{'blue':128,'red':128})
            s.write_json(s.stage_output/'fault-host-preparation.json',{'hosts':admitted,
                'scope':'Prepare real hosts early. Native production proceeds while independent faults run; handoff still requires actual newborns and pending work.'})
        try:
            attempt(session,'resource-host-preparation',prepare_hosts,failures)
        except RuntimeEvidenceUnavailable as error:
            session.write_json(session.output/'blocked-by-evidence.json',{'after':'resource-host-preparation',
                'remaining':[name for name,_ in phases],'reason':str(error)})
            return failures
    for index,(name,execute) in enumerate(phases):
        try:
            attempt(session,name,execute,failures)
        except RuntimeEvidenceUnavailable as error:
            session.write_json(session.output/'blocked-by-evidence.json',{'after':name,
                'remaining':[label for label,_ in phases[index+1:]],'reason':str(error)})
            break
    return failures
