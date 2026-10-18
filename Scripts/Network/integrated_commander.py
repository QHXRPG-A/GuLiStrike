"""Commander phases driven through selection, orders and production UI requests."""
from __future__ import annotations

import base64
import json
import math
from pathlib import Path
import struct
import time

from integrated_acceptance import entry, identity, input_and_wait, objects, prove
from acceptance_session import preparation


def current(session, key):
    """A handoff may temporarily freeze every copy of this object."""
    return next((o for o in objects(session, True) if o["id"] == key and o["life"] == 1), None)


def center(values):
    return [sum(o["position"][i] for o in values)/len(values) for i in range(3)]


@preparation
def select(session, commander, values, wait_owners=True):
    if not values:
        raise RuntimeError("Selection has no living gameplay entities")
    # Exact preparation still installs the real cohort/UI selection snapshot.
    # The dedicated selection acceptance covers the actual gestures separately.
    action, evidence = input_and_wait(session,"selection_objects",commander["player"],
        objects=sorted(o['id'] for o in values),command_selection=True)
    expected={o["ordinal"] for o in values}
    session.wait("selection-"+action["id"],lambda: expected.issubset(set(session.states[commander["name"]]["scene_ui"]["selected_soldiers"])))
    owned={o['id'] for o in values if o['ownership_policy']==1}
    if owned and wait_owners:
        with session.preparation('selected source ownership'):
            session.wait('selection-owners-'+action['id'],lambda:all((o:=current(session,key)) is not None
                and identity(o['simulator'])==commander['player'] and not any(v.get('handoff_pending')
                    for e in session.clients for v in session.states[e['name']].get('objects',[]) if v['id']==key) for key in owned))
    return set(session.states[commander["name"]]["scene_ui"]["selected_soldiers"]),evidence


def order(session, commander, point, intent="Move", special=None):
    session.input_ordinal=getattr(session,"input_ordinal",0)+1
    values={"point":point,"intent":intent,"command":session.stage_number*100000+session.input_ordinal,
            "ground_ray":intent=="Move"}
    if special is not None: values["special"]=special
    action,evidence=input_and_wait(session,"unit_task",commander["player"],**values)
    if action["result"].get("accepted",0)<=0:
        raise RuntimeError("Formal selection order accepted no members: "+json.dumps(action))
    return action,evidence


def new_decisions(session):
    return [v for v in session.decisions.values() if v["sequence"]>session.decision_start]


def formation(session):
    session.begin("mass-frozen-formation-and-active-skill")
    commander=entry(session,"Red","Commander")
    army=[o for o in objects(session,True) if o["kind"]==1 and o["team"]==1 and o["life"]==1 and o["health"]>0]
    manifest=json.loads(Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
    if manifest.get('strings',{}).get('acceptance/preparation')=='gf-fast-spatial-v2':
        startup={identity(o['id']) for o in manifest['objects']}
        newborns=sorted((o for o in army if identity(o['id']) not in startup),key=lambda o:o['ordinal'])
        originals=[o for o in army if identity(o['id']) in startup and o['definition']=='unit/1' and not o.get('phased')]
        if newborns and originals:
            from population_combat_preparation import positions
            with session.preparation('newborn formation members beside the existing native cohort'):
                anchor=min(originals,key=lambda o:o['ordinal'])['position']
                requests=[(o,[anchor[0]+(i%5)*160,anchor[1]+(i//5+1)*160,anchor[2]],False,None)
                          for i,o in enumerate(newborns)]
                receipts=positions(session,requests,find_nearby=True)
                session.write_json(session.stage_output/'formation-newborn-position-preparation.json',{
                    'positions':receipts,'scope':'User-authorized positions beside the existing army, including remote producer spawns. Original frozen selection, native movement and once-only execution remain required.'})
            army=[o for o in objects(session,True) if o['kind']==1 and o['team']==1 and o['life']==1 and o['health']>0]
    # The full-cohort takeover is observed by the existing formal wait below.
    # Do not first run the identical ownership wait under the short setup clock.
    selected,_=select(session,commander,army,wait_owners=False)
    chosen=[o for o in army if o["ordinal"] in selected]
    chosen_keys={v['id'] for v in chosen}
    priority_keys={v['id'] for v in chosen if v['ownership_policy']==1}
    def selected_owners_committed():
        active=[o for o in objects(session,True) if o['id'] in chosen_keys]
        return len(active)==len(chosen_keys) and {o['id'] for o in active}==chosen_keys and all(
            identity(o['simulator'])==commander['player'] for o in active if o['ownership_policy']==1) and not any(
                v.get('handoff_pending') for e in session.clients for v in session.states[e['name']].get('objects',[])
                if v['id'] in priority_keys)
    session.wait("selected-owners-committed",selected_owners_committed)
    before={o["id"]:o["position"] for o in chosen}
    destination=center(chosen);destination[1]-=3500
    counters={e["name"]:session.states[e["name"]]["executed_commands_by_kind"]["1"] for e in session.clients}
    result,receipt=order(session,commander,destination)
    accepted=sum(c["accepted"] for c in result["result"]["cohorts"])
    if accepted!=len(selected):
        raise RuntimeError("Formal formation did not accept every frozen Mass member: "+json.dumps(result))
    session.wait("formation-executed",lambda: sum(session.states[e["name"]]["executed_commands_by_kind"]["1"]-counters[e["name"]]
        for e in session.clients)==accepted and sum(math.dist(o["position"],before[o["id"]])>100
        for o in objects(session,True) if o["id"] in before)>=accepted*.9)
    actual=sum(session.states[e["name"]]["executed_commands_by_kind"]["1"]-counters[e["name"]] for e in session.clients)
    prove(session,"mass_commands",accepted==len(selected) and actual==accepted,
        "One frozen command executed once per selected Mass member after priority ownership takeover",receipt,"formation-executed.json")
    # Q uses exactly the same captured selection and target as a player input.
    session.decision_start=session.last_decision
    reply,skill_receipt=input_and_wait(session,"unit_skill",commander["player"],point=destination)
    def committed_summons():
        births={b for d in new_decisions(session) if d.get("status")==0 and d.get("capability")=="object_batch"
                for b in d.get("births",[])}
        views=[{o["id"]:o for o in session.states[e["name"]].get("scene_objects",[])} for e in session.clients]
        return bool(births) and all(b in v and v[b]["entity"] and v[b]["life"]==1 for b in births for v in views)
    session.wait("active-skill-entities",committed_summons)
    prove(session,"skills",reply["result"].get("units",0)>0 and committed_summons(),
        "Selected-unit skill produced owner-committed birth results and real entities on all six clients",skill_receipt,"active-skill-entities.json")
    session.finish()


def begin_teleport(session, commander, point, label):
    operation=session.operation('teleport_source',commander['player'],point=point)
    receipt='input-'+operation+'-submitted.json'
    session.write_json(session.stage_output/receipt,{'operation':operation,'kind':'teleport_source',
        'player':commander['player'],'point':point,'scope':'Observe choosing state concurrently with the original request, before its final callback.'})
    def collected():
        action=session.action(operation)
        if action and action['status']=='completed' and action['result'].get('code')!=0:
            raise RuntimeError('Teleport source returned a final rejection: '+json.dumps(action))
        state=session.states[commander['name']]['scene_ui']['teleport']
        if identity(state['cast'])!=operation: return False
        if state['phase']==5:
            from acceptance_session import PreparationSkipped
            raise PreparationSkipped('Teleport collection returned before a stable choosing/target fixture was available')
        return state['phase']==2 and state.get('members',0)>0 and any(o['phased'] for o in objects(session,True))
    session.wait(label,collected)
    return operation,receipt


def teleport_source_receipt(session, operation):
    session.wait('original-source-final-receipt',lambda:bool((action:=session.action(operation))
        and action['status']=='completed' and action['result'].get('code')==0))
    return session.stage_output/'original-source-final-receipt.json'


@preparation
def prepare_teleport_source(session,commander,army):
    from population_combat_preparation import position
    eligible=[o for o in army if not o.get('phased') and 'relocation_body' in o.get('capabilities',{})
              and not relocation_body(o)['locked']]
    if not eligible:
        from acceptance_session import PreparationSkipped
        raise PreparationSkipped('No actual unlocked relocation source is available')
    middle=center(eligible)
    source=min(eligible,key=lambda o:(o['definition']!='unit/1',math.dist(o['position'],middle)))
    select(session,commander,[source])
    manifest=json.loads(Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
    isolated=manifest.get('strings',{}).get('acceptance/preparation')=='gf-fast-spatial-v2'
    point=[10000,6000,source['position'][2]] if isolated else source['position']
    reply,evidence=position(session,source,point,find_nearby=isolated)
    point=reply['result']['applied']
    session.write_json(session.stage_output/'teleport-source-position-preparation.json',{
        'object':source['id'],'input':evidence,'result':reply,
        'scope':'Authorized position preparation clears the previous movement order at a legal surface. Gold preparation uses the isolated central lane before real collection and cancellation; no action lock or cast state is injected.'})
    return point


def teleport_landing(session):
    session.begin("teleport-collection-and-landing")
    commander=entry(session,"Red","Commander")
    army=[o for o in objects(session,True) if o["kind"]==1 and o["team"]==1 and o["life"]==1 and o["health"]>0]
    source=prepare_teleport_source(session,commander,army)
    # The two-point tactical input performs collection, phase-out and relocation.
    source_operation,receipt=begin_teleport(session,commander,source,'teleport-collected')
    phased={o["id"]:o["position"] for o in objects(session,True) if o["phased"]}
    if not phased: raise RuntimeError("Teleport collection has no phased real objects")
    # A fixed +4500 offset is still inside the dense 600-unit/summon array.
    # Use the same outside-array corridor as the real Ground input route.
    # Landing geometry and collision remain authoritative at submission.
    occupied=[o for o in objects(session,True) if o['life']==1 and 'relocation_body' in o.get('capabilities',{})]
    destination=list(source);destination[0]=max(o['position'][0] for o in occupied)+4000
    session.write_json(session.stage_output/'teleport-route.json',{
        'source':source,'destination':destination,'array_max_x':destination[0]-4000,
        'scope':'Spatial input preparation; actual ground, collision and committed relocation still required'})
    reply,release=input_and_wait(session,"teleport_destination",commander["player"],point=destination)
    session.wait("teleport-landed",lambda: session.states[commander["name"]]["scene_ui"]["teleport"].get("landed")
        and all(not o["phased"] and math.dist(o["position"],phased[o["id"]])>1000 for o in objects(session,True) if o["id"] in phased))
    source_final=teleport_source_receipt(session,source_operation)
    prove(session,"teleport",bool(phased),"Two formal inputs collected, phased and relocated the same stable identities",
          receipt,"teleport-collected.json",release,"teleport-landed.json",source_final)
    # Spatial field observations are checked independently of successful UI input.
    collected=json.loads((session.stage_output/"teleport-collected.json").read_text(encoding="utf-8"))
    field_states=collected[commander["name"]].get("scene_fields",[])
    prove(session,"spell_fields",any(f.get("cast")==collected[commander["name"]]["scene_ui"]["teleport"].get("cast") and f.get("initialized") for f in field_states),
          "Actual persistent field entity belongs to the committed cast", "teleport-collected.json","teleport-landed.json")
    session.finish()


@preparation
def prepare_card_entry(session, commander):
    observed=set()
    def ready():
        state=session.states[commander['name']];ui=state['scene_ui']
        if ui.get('commander_can_issue_orders') is not True or ui.get('teleport',{}).get('phase',0) not in (0,5):
            observed.clear();return False
        observed.add(state['snapshot_finished_qpc'])
        return len(observed)>=2
    session.wait('card-entry-ready',ready)


def card_selection(session):
    session.begin("rogue-selection-submit-and-return")
    commander=entry(session,"Red","Commander")
    session.decision_start=session.last_decision
    prepare_card_entry(session,commander)
    _,opened=input_and_wait(session,"rogue_open",commander["player"])
    cards=session.states[commander["name"]]["scene_ui"].get("cards",[])
    if not cards: raise RuntimeError("The real card UI has no offered cards")
    for attempt in range(12):
        if "QA.101" in cards: break
        input_and_wait(session,"rogue_reroll",commander["player"])
        cards=session.states[commander["name"]]["scene_ui"].get("cards",[])
    if "QA.101" not in cards: raise RuntimeError("Data-only card was not available through the real offer/reroll UI")
    index=cards.index("QA.101")
    session.wait("rogue-offer-visible",lambda: session.states[commander["name"]]["scene_ui"].get("rogue_phase")==1)
    _,selected=input_and_wait(session,"rogue_select",commander["player"],index=index)
    session.wait("rogue-flip-finished",lambda: session.states[commander["name"]]["scene_ui"].get("rogue_phase")==3)
    confirmed,chosen=input_and_wait(session,"rogue_confirm",commander["player"],index=index)
    session.wait("rogue-commit",lambda: any(d.get("status")==0 and d.get("capability")=="army_state" for d in new_decisions(session)))
    # Committing the selected card and releasing the modal UI are different
    # milestones. A later skill must not be sent through an active card modal.
    session.wait("rogue-returned",lambda: not session.states[commander["name"]]["scene_ui"].get("rogue_open")
        and not session.states[commander["name"]]["scene_ui"].get("rogue_pending"),timeout=45)
    prove(session,"rogue",confirmed["status"]=="completed",
          "The offered card passed through actual flip/confirmation, committed and released modal input",
          opened,selected,"rogue-flip-finished.json",chosen,"rogue-commit.json","rogue-returned.json")
    session.variant_card_committed=True
    session.finish()


def card_cancel(session):
    session.begin("rogue-cancel-and-return")
    commander=entry(session,"Red","Commander")
    if not session.states[commander["name"]]["scene_ui"].get("rogue_open"):
        prepare_card_entry(session,commander)
        input_and_wait(session,"rogue_open",commander["player"])
    ui=session.states[commander["name"]]["scene_ui"]
    if ui.get("rogue_pending") or ui.get("rogue_committed"):
        raise RuntimeError("Card cancellation requires an uncommitted offer; previous selection has not returned")
    offered=ui.get("rogue_session")
    _,receipt=input_and_wait(session,"rogue_cancel",commander["player"])
    session.wait("rogue-cancel-returned",lambda: not session.states[commander["name"]]["scene_ui"].get("rogue_open")
        and not session.states[commander["name"]]["scene_ui"].get("rogue_pending"),timeout=45)
    prove(session,"rogue_cancel",bool(offered),"An actual uncommitted card offer was cancelled and released its modal input",
        "baseline.json",receipt,"rogue-cancel-returned.json")
    session.finish()


def relocation_body(obj):
    cap=obj['capabilities']['relocation_body']
    raw=base64.b64decode(cap['data'],validate=True)
    if cap['version']!=1 or len(raw)!=164:
        raise RuntimeError('Unsupported relocation body evidence contract')
    return {'cast':''.join(f'{w:08x}' for w in struct.unpack_from('<4I',raw,24)),
        'step':struct.unpack_from('<I',raw,40)[0],'ack':struct.unpack_from('<I',raw,44)[0],
        'accepted':bool(raw[64]),'locked':bool(raw[65]),'phased':bool(raw[66]),
        'original':struct.unpack_from('<3d',raw,68),'destination':struct.unpack_from('<3d',raw,116)}


def relocation_radius(obj):
    cap=obj['capabilities']['relocation_cast'];raw=base64.b64decode(cap['data'],validate=True);offset=0
    if cap['version']!=1: raise RuntimeError('Unsupported relocation cast evidence contract')
    def read(fmt):
        nonlocal offset
        values=struct.unpack_from('<'+fmt,raw,offset);offset+=struct.calcsize('<'+fmt);return values
    def text():
        nonlocal offset
        size=read('I')[0];value=raw[offset:offset+size].decode('utf-8');offset+=size;return value
    profiles={}
    for _ in range(read('I')[0]):
        key=text();text();read('I');profiles[key]=read('3d')[0];read('4QB')
    for fmt in ('Q','I'):
        for _ in range(read('I')[0]): text();read(fmt)
    read('8I');return profiles[text()]


def teleport_cancel(session):
    session.begin("teleport-cancel-and-return")
    commander=entry(session,"Red","Commander")
    # A second real cast covers cancellation after collection. Wait on the
    # caster object's age, never on another client's local world clock.
    def teleport_ready():
        local=session.states[commander['name']]
        ready=next((s['ready_at'] for s in local['scene_ui']['global_skills'] if s['skill']=='Teleport'),None)
        roots=[o for o in objects(session,True) if o['team']==1 and 'relocation_cast' in o.get('capabilities',{})]
        if ready is None or len(roots)!=1: return False
        owner=next(e for e in session.clients if e['player']==identity(roots[0]['simulator']))
        clock=next((o['logical_age'] for o in session.states[owner['name']]['objects'] if o['id']==roots[0]['id']),None)
        return clock is not None and clock/60>=ready and local['scene_ui']['teleport']['phase'] in (0,5)
    session.wait('teleport-ready-for-cancel',teleport_ready)
    army=[o for o in objects(session,True) if o['kind']==1 and o['team']==1 and o['life']==1]
    source_point=prepare_teleport_source(session,commander,army)
    original={o['id']:o for o in objects(session,True) if o['kind']==1 and o['team']==1 and o['life']==1}
    source_operation,cancel_source=begin_teleport(session,commander,source_point,'cancel-collected')
    returning={o['id'] for o in objects(session,True) if o['phased'] and o['id'] in original}
    cast=session.states[commander['name']]['scene_ui']['teleport']['cast']
    frozen={o['id']:relocation_body(o) for o in objects(session,True) if o['id'] in returning}
    root=next(o for o in objects(session,True) if o['team']==1 and 'relocation_cast' in o['capabilities'])
    radius=relocation_radius(root)
    source=session.states[commander['name']]['scene_ui']['teleport']['source']
    _,cancel_receipt=input_and_wait(session,'teleport_cancel',commander['player'])
    def returned():
        state=session.states[commander['name']]['scene_ui']['teleport']
        if state['cast']!=cast or state['phase']!=5: return False
        destinations={}
        for endpoint in session.clients:
            actual={o['id']:o for o in session.states[endpoint['name']]['scene_objects']}
            if not returning.issubset(actual): return False
            for key in returning:
                obj=actual[key];body=relocation_body(obj)
                if obj['phased'] or obj['life']!=1 or body['cast']!=identity(cast) or body['locked'] or body['phased'] \
                    or not body['accepted'] or body['step']<3 or body['ack']!=body['step'] \
                    or body['original']!=frozen[key]['original'] or math.dist(body['destination'][:2],source[:2])>radius*4+.01:
                    return False
                if key in destinations and destinations[key]!=body['destination']: return False
                destinations[key]=body['destination']
        return True
    session.wait('cancel-returned',returned)
    source_final=teleport_source_receipt(session,source_operation)
    session.write_json(session.stage_output/'cancel-placement-contract.json',{'cast':cast,'source':source,
        'profile_radius_cm':radius,'return_search_radius_cm':radius*4,'frozen':frozen,
        'returned':{o['id']:relocation_body(o) for o in objects(session,True) if o['id'] in returning},
        'contract':'Actual external-control application acknowledgements agree on six endpoints; existing tasks may move objects after release.'})
    prove(session,'teleport_cancel',bool(returning) and returned(),
          'Formal cancel applied and acknowledged collision-safe source-area placements, preserving every collected identity on six endpoints',
          cancel_source,'cancel-collected.json',cancel_receipt,'cancel-returned.json','cancel-placement-contract.json',source_final)
    session.finish()


def teleport_and_cards(session):
    teleport_landing(session)
    card_selection(session)
    card_cancel(session)
    teleport_cancel(session)


def mine_funds(session, desired):
    """Only resource-production checks need to wait for real mined income."""
    for color in ("blue","red"):
        session.refresh()
        if all(session.states[entry(session,t,"Commander")["name"]]["economy"][t][color]>=desired[color] for t in ("Red","Blue")):
            continue
        session.begin("resource-funding-"+color)
        threshold={"blue":0,"red":0};threshold[color]=desired[color]
        session.legacy("mining_acceptance","mining",required_balance=threshold)
        session.finish()


@preparation
def fund(session, desired, team="Red"):
    """Explicit development setup; credits are never proof of mined income."""
    from pathlib import Path
    manifest=json.loads(Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
    if float(manifest['numbers'].get('acceptance/resource_supply',0))!=1:
        raise RuntimeError('Resource preparation requires the opt-in development supply profile')
    session.refresh()
    commander=entry(session,team,'Commander')
    before=session.states[commander['name']]['economy'][team]
    missing={c:max(0,desired[c]-before[c]) for c in ('blue','red')}
    if getattr(session.args,'continue_remaining',False):
        # This collection has no later unfunded/upkeep baseline. A single
        # configured power-of-two credit is faster than exact small change.
        missing={c:(1<<(amount-1).bit_length()) if amount else 0 for c,amount in missing.items()}
    if not any(missing.values()): return
    blocked=getattr(session,'blocked_resource_supply',{}).get(team)
    if blocked:
        from integrated_remaining import PrerequisiteMissing
        raise PrerequisiteMissing('Resource preparation is blocked by its first failed request: '+blocked)
    session.begin('development-resource-supply-'+team.lower())
    # Keep the fixed Commander as caller. The native opt-in credit uses the
    # existing controlled request route if the resource-group simulator moves.
    definition=manifest['strings']['army/root_definition']
    source=next((o for o in session.states[commander['name']]['objects']
                 if o['definition']==definition and o['life']==1
                 and identity(o['controller'])==commander['player'] and not o.get('dormant')),None)
    if not source: raise RuntimeError('Development supply has no living team army source')
    supplier=commander
    receipts=[];events=[]
    for color,deficit in missing.items():
        while deficit:
            amount=1<<min(10,deficit.bit_length()-1)
            try:
                reply,evidence=input_and_wait(session,'resource_supply',supplier['player'],team=team,color=color,amount=amount)
            except RuntimeError as error:
                session.blocked_resource_supply=getattr(session,'blocked_resource_supply',{})
                session.blocked_resource_supply[team]=session.stage_id+': '+str(error)
                raise
            if not reply['result'].get('committed'):
                raise RuntimeError('Development supply was not committed by the team owner')
            receipts.append(evidence);events.append(identity(reply['request']));deficit-=amount
    def committed():
        return all(event in session.decisions and session.decisions[event]['kind']==1
                   and session.decisions[event]['status']==0
                   and session.observed_by_active(event) for event in events)
    session.wait('supply-committed',committed)
    session.write_json(session.stage_output/'supply-provenance.json',{
        'fixture':'development_resource_supply','team':team,'requested_minimum':desired,'amount_added':missing,
        'balance_before':before,'source':source,'supplier':supplier['player'],'inputs':receipts,'commits':[session.decisions[event] for event in events],
        'scope':'Preparation only. Not mining, processing, production, refund or population evidence.'})
    session.finish()
    # The next gameplay stage records a fresh post-supply baseline.


def place(session, definition, team="Red", offset=(1200,0), point=None):
    manifest=json.loads(__import__('pathlib').Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
    is_conversion=float(manifest['numbers'].get(f'building/{definition}/CompletionUnitTypeId',0))>0
    ground=entry(session,team,"Commander" if is_conversion else "Ground")
    pawn=next(o for o in objects(session,True) if o["definition"]=="player/ground" and o["team"]==(1 if team=="Red" else 2))
    if point is None:
        point=list(pawn["position"]);point[0]+=offset[0];point[1]+=offset[1]
    before={o["id"] for o in objects(session)}
    session.decision_start=session.last_decision
    result,receipt=input_and_wait(session,"building_place",ground["player"],definition=definition,point=point)
    if not result["result"].get("accepted"):
        raise RuntimeError("Production placement was rejected: "+json.dumps(result))
    session.wait("admitted-"+str(definition),lambda: any(o["id"] not in before and o["definition"]=="building/"+str(definition) for o in objects(session)))
    building=next(o for o in objects(session) if o["id"] not in before and o["definition"]=="building/"+str(definition))
    with session.preparation('admitted construction owner'):
        session.wait('admitted-owner-'+str(definition),lambda:(now:=current(session,building['id'])) is not None and now.get('entity'))
    return current(session,building['id']),receipt


def population(o):
    cap=o.get("capabilities",{}).get("population")
    if not cap: return None
    raw=base64.b64decode(cap["data"],validate=True)
    size=struct.unpack_from("<I",raw)[0]
    if cap["version"]!=1 or len(raw)!=4+size+8: raise RuntimeError("Unrecognized population capability")
    group=raw[4:4+size].decode();units,limit=struct.unpack_from("<II",raw,4+size)
    return group,units,limit


@preparation
def prepare_population(session, slots):
    """Prepare isolated targets, then release production slots through real fire."""
    def free():
        claims=[population(o) for o in objects(session) if o['team']==1 and o['life']<2 and population(o)]
        return claims[0][2]-sum(c[1] for c in claims) if claims else 0
    if free()>=slots: return
    session.begin('combat-population-preparation')
    from pathlib import Path
    manifest=json.loads(Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
    initial_population={identity(o['id']) for o in manifest['objects'] if o['kind']==1 and o['team']==1}
    profile=json.loads((Path(__file__).resolve().parents[2]/'Config/Hybrid/AcceptanceProfile.json').read_text(encoding='utf-8-sig'))
    speeds={f"unit/{r['Id']}":float(r['MovementSpeedCmPerSecond'])
            for asset in profile['assets'].values() for r in asset.get('rows',[])
            if 'MovementSpeedCmPerSecond' in r and 'Id' in r}
    eligible=[o for o in objects(session,True) if o['kind']==1 and o['team']==1 and o['life']==1 and population(o)
              and identity(o['id']) in initial_population]
    if not eligible: raise RuntimeError('Population preparation requires living Red population holders')
    commander=entry(session,'Red','Commander')
    session.decision_start=session.last_decision
    definition=min({o['definition'] for o in eligible},key=lambda d:min(o['health'] for o in eligible if o['definition']==d))
    if speeds.get(definition,0)<=0: raise RuntimeError('Movement definition is absent from the frozen asset readback')
    # Real producers can consume freed slots before the next shared readback.
    # Keep the original bounded target buffer; stop firing at actual headroom.
    group=sorted((o for o in eligible if o['definition']==definition),key=lambda o:(o['health'],o['ordinal']))[:25]
    weakest=[o for o in group if o['health']==group[0]['health']]
    if len(weakest)>=slots:
        # Use the whole remaining weak buffer, never just the required count.
        # Healthy front-row bodies otherwise block shots at the prepared weak
        # targets while real automatic production consumes the released slots.
        group=weakest
    input_and_wait(session,'selection_objects',commander['player'],objects=sorted(o['id'] for o in group))
    cohort={o['id'] for o in group}
    if len(cohort)<slots-free() or len(cohort)>25:
        raise RuntimeError('Explicit targets cannot prepare the required population headroom')
    session.wait('prepared-target-owners',lambda: all(identity(o['simulator'])==commander['player']
        for o in objects(session,True) if o['id'] in cohort) and len([o for o in objects(session,True) if o['id'] in cohort])==len(cohort))
    from population_combat_preparation import run as release_through_combat
    try:
        release_through_combat(session,cohort,free,slots)
    finally:
        deaths=[d for d in new_decisions(session) if d['kind']==0 and d['status']==0
                and d['state_domain'] in cohort and int(identity(d['death_event']),16)
                and session.observed_by_active(identity(d['event']))]
        if deaths:
            session.write_json(session.stage_output/'population-release-partial.json',{
                'scope':'Observed real deaths and current slots, including a short-budget preparation skip; not proof that all requested headroom was prepared.',
                'available_slots':free(),'requested_slots':slots,'death_decisions':deaths})
            session.population_death_evidence=session.stage_output/'population-release-partial.json'
    session.write_json(session.stage_output/'population-release.json',{'preparation':'development positions and temporary target action locks; native Ground fire and numeric death remain required',
        'available_slots':free(),'death_decisions':[d for d in new_decisions(session)
            if d['kind']==0 and d['status']==0 and d['state_domain'] in cohort and int(identity(d['death_event']),16)]})
    session.population_death_evidence=session.stage_output/'population-release.json'
    if getattr(session.args,'continue_remaining',False):
        # Combat and safety cleanup already produced the real death/headroom
        # evidence. Flight-source restoration is a separate setup operation,
        # rather than a late action against the expired combat setup clock.
        session.preparation_deadline=None
        session.finish()
        session.begin('post-population-flight-source-preparation')
        session.preparation_deadline=time.monotonic()+session.args.preparation_timeout
    # A killed cohort may have held every flight source at this Commander.
    # Obtain a living original source through the same real ownership input
    # used by the map entry; the native flight pool must refill on its own.
    survivors=[o for o in objects(session,True) if o['kind']==1 and o['team']==1 and o['life']==1
               and o['definition']==definition and identity(o['id']) in initial_population]
    if sum(session.states[e['name']]['flight_load']['live_additional'] for e in session.clients)!=500 and survivors:
        replacement=min(survivors,key=lambda o:o['ordinal'])
        result,receipt=input_and_wait(session,'selection_objects',commander['player'],objects=[replacement['id']])
        session.write_json(session.stage_output/'flight-source-preparation.json',{
            'source':replacement['id'],'input':receipt,'result':result,
            'scope':'Real selection and ownership input only; flight count is read from native entities'})
    def load_refilled():
        return sum(session.states[e['name']]['flight_load']['live_additional'] for e in session.clients)==500
    # Cleanup may use its bounded tail budget. Do not re-enter a wait after
    # that tail when the last actual native readback already proves the load.
    if not load_refilled(): session.wait('post-combat-load-refilled',load_refilled)
    session.finish()


@preparation
def prepare_construction_site(session):
    """Use the authorized position fixture to separate placement from the army."""
    session.begin('construction-site-input')
    pilot=entry(session,'Red','Ground')
    def ground():
        return next(g for g in session.states[pilot['name']]['ground_states'] if g['team']==1)
    start=list(ground()['position'])
    army=[o for o in objects(session,True) if o['kind']==1 and o['life']==1]
    candidates=[]
    for axis in (0,1):
        for extreme,margin in ((min,-4000),(max,4000)):
            point=list(start);point[axis]=extreme(o['position'][axis] for o in army)+margin
            candidates.append(point)
    candidates.sort(key=lambda p:math.dist(start[:2],p[:2]))
    from population_combat_preparation import position
    from acceptance_session import PreparationSkipped
    pawn=next(o for o in objects(session,True) if o['definition']=='player/ground' and o['team']==1 and o['life']==1)
    attempts=[]
    for destination in candidates:
        try:
            result,receipt=position(session,pawn,destination,find_nearby=True)
        except PreparationSkipped as error:
            if 'Prepared position is unsupported, unavailable or outside navigation' not in str(error):
                raise
            attempts.append({'destination':destination,'rejection':str(error)})
            session.write_json(session.stage_output/'construction-site-candidates.json',{'attempts':attempts})
            continue
        attempts.append({'destination':destination,'input':receipt,'result':result})
        session.write_json(session.stage_output/'construction-site-route.json',
            {'start':start,'destination':destination,'input':receipt,'result':result,'attempts':attempts,
             'purpose':'Authorized native position preparation; try at most four pads inside the original shared preparation budget. Placement and completion remain real gameplay.'})
        session.finish()
        return
    raise PreparationSkipped('All four native construction preparation pads were rejected; original requests are retained')


def placement_cancel_and_reject(session):
    session.begin('placement-preview-cancel-and-unfunded-rejection')
    commander=entry(session,'Red','Commander')
    manifest=json.loads(__import__('pathlib').Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
    candidates={k.removeprefix('population/definition/').removesuffix('/units') for k in manifest['numbers']
                if k.startswith('population/definition/building/') and k.endswith('/units')}
    balance=session.states[commander['name']]['economy']['Red']
    eligible=[d for d in candidates if any(float(manifest['numbers'][d+'/Cost/'+c.capitalize()])>balance[c] for c in ('blue','red'))]
    if not eligible: raise RuntimeError('The unfunded-placement stage has no applicable catalog definition')
    definition=int(sorted(eligible)[0].split('/')[1])
    def claims():
        return {o['id']:population(o) for o in objects(session) if o['team']==1 and o['life']<2 and population(o)}
    initial=claims();before_objects={o['id'] for o in objects(session)}
    preview,preview_receipt=input_and_wait(session,'building_preview',commander['player'],definition=definition)
    cancel,cancel_receipt=input_and_wait(session,'building_cancel',commander['player'])
    if not preview['result']['active'] or cancel['result']['active'] or claims()!=initial \
            or preview['result']['committed_count']!=cancel['result']['committed_count']:
        raise RuntimeError('Cancelling a catalog preview changed committed placement or population')
    pawn=next(o for o in objects(session,True) if o['definition']=='player/ground' and o['team']==1)
    point=list(pawn['position']);point[0]+=1200
    session.decision_start=session.last_decision
    result,rejection_receipt=input_and_wait(session,'building_place',commander['player'],definition=definition,point=point)
    if result['result'].get('accepted'): raise RuntimeError('Unfunded production placement was accepted')
    if result['result'].get('reject_reason')!=14:
        raise RuntimeError('Placement did not reach the required resource rejection: '+json.dumps(result))
    session.wait('rejected-work-retired',lambda: any(d['status']==11 for d in new_decisions(session)) and
        not any(o['id'] not in before_objects and o['definition']=='work/place/'+str(definition) and o['life']<2 for o in objects(session)))
    if claims()!=initial: raise RuntimeError('Rejected placement leaked a population claim')
    session.write_json(session.stage_output/'cancel-reject-claims.json',{'before':initial,'after':claims(),
        'preview':preview,'cancel':cancel,'rejected':result,'decisions':new_decisions(session)})
    session.population_branch_evidence=[session.stage_output/p for p in (preview_receipt,cancel_receipt,rejection_receipt,
        'rejected-work-retired.json','cancel-reject-claims.json')]
    session.finish()


def construct(session, definition, offset, admitted):
    """One real construction item; admission survives a later assertion failure."""
    manifest=json.loads(__import__('pathlib').Path(session.plan["manifest"]).read_text(encoding="utf-8-sig"))
    if manifest.get('strings',{}).get('acceptance/preparation')=='gf-fast-spatial-v2' and definition in (4,101,102,5):
        session.begin('prepared-building-'+str(definition))
        candidates=[o for o in objects(session,True) if o['team']==1 and o['life']==1
                    and o['definition']=='building/'+str(definition) and o['completed'] and o['entity']]
        if candidates:
            preferred=getattr(session,'supply_fixture',{}).get('facility',{}).get('id')
            actual=min(candidates,key=lambda o:(o['id']!=preferred,o['ordinal']))
            admitted[definition]=actual['id']
            session.write_json(session.stage_output/'prepared-building-readback.json',{'object':actual,
                'scope':'Existing prerequisite, not construction evidence. Real births, delivery and processing remain mandatory.'})
            session.finish()
            return
        session.finish()
    # Obtain the shared production headroom when first needed. A building
    # without a unit conversion does not require repeating the duel first.
    desired={c:int(float(manifest['numbers'][f'building/{definition}/Cost/{c.capitalize()}']))+128 for c in ('blue','red')}
    fund(session,desired)
    if float(manifest['numbers'].get(f'building/{definition}/CompletionUnitTypeId',0))>0:
        # Ongoing native producers can consume a single released slot before
        # admission. Release a small real combat reserve, never inject claims.
        prepare_population(session,8)
    session.begin("construction-"+str(definition))
    commander=entry(session,'Red','Commander')
    group={o['id']:o for o in session.states[commander['name']]['objects'] if o.get('ownership_policy')==4 and o['life']<2}
    owners={identity(o['simulation_owner']) for o in group.values()}
    if len(owners)!=1: raise RuntimeError('Construction requires one complete resource group')
    resource_owner=next(iter(owners))
    worker_keys={o['id'] for o in objects(session,True) if o['kind']==5 and o['team']==1 and o['life']==1}
    if not worker_keys: raise RuntimeError('Construction requires real engineering carriers')
    _,worker_selection=input_and_wait(session,'selection_objects',commander['player'],objects=sorted(worker_keys))
    if definition==1:
        session.wait('construction-workers-grouped',lambda: all(
            o['id'] in group and identity(o['simulator'])==resource_owner
            for o in objects(session,True) if o['id'] in worker_keys))
    if definition == 8 and getattr(session.args, 'continue_remaining', False):
        from acceptance_session import PreparationSkipped
        from population_combat_preparation import position
        with session.preparation('conversion footprint and nearby Ground position'):
            pawn=next(o for o in objects(session,True) if o['definition']=='player/ground' and o['team']==1 and o['life']==1)
            origin=list(pawn['position'])
            extent=[float(manifest['numbers'][f'building/8/CollisionExtent/{axis}']) for axis in 'XY']
            spacing=math.hypot(*extent)*2+2000
            attempts=[]
            for dx,dy in ((-1,0),(1,0),(0,-1),(0,1)):
                desired=[origin[0]+dx*spacing,origin[1]+dy*spacing,origin[2]]
                nearby=[desired[0]+extent[0]+400,desired[1],desired[2]]
                try:
                    prepared,prep_input=position(session,pawn,nearby,find_nearby=True)
                    building,receipt=place(session,definition,point=desired)
                except RuntimeError as error:
                    from runtime_evidence import RuntimeEvidenceUnavailable
                    if isinstance(error,(RuntimeEvidenceUnavailable,PreparationSkipped)): raise
                    if not any(text in str(error) for text in ('No valid placement location','Prepared position is unsupported')):
                        raise
                    attempts.append({'point':desired,'failure':str(error)})
                    session.write_json(session.stage_output/'conversion-footprint-preparation.json',{
                        'extent_xy':extent,'spacing':spacing,'attempts':attempts})
                    continue
                attempts.append({'point':desired,'position_input':prep_input,'position_result':prepared,'placement_input':receipt})
                session.write_json(session.stage_output/'conversion-footprint-preparation.json',{
                    'extent_xy':extent,'spacing':spacing,'attempts':attempts,
                    'scope':'Footprint and position preparation only; placement, replacement and population claims remain formal gameplay.'})
                break
            else:
                raise PreparationSkipped('No clear conversion footprint within the four-pad short preparation budget')
    elif definition in (101,102):
        from acceptance_session import PreparationSkipped
        with session.preparation('prerequisite building placement'):
            from population_combat_preparation import position
            pilot=next(o for o in objects(session,True) if o['definition']=='player/ground' and o['team']==1 and o['life']==1)
            origin=list(pilot['position'])
            errors=[]
            for candidate in (offset,(0,3600),(-3600,0),(4000,2400),(-4000,2400),(8000,0),(-8000,0)):
                try:
                    desired=[origin[0]+candidate[0],origin[1]+candidate[1],origin[2]]
                    prepared,prep_input=position(session,pilot,[desired[0]-1200,desired[1],desired[2]],find_nearby=True)
                    session.write_json(session.stage_output/('placement-pawn-preparation-'+prepared['id']+'.json'),{
                        'input':prep_input,'result':prepared,'requested_building_point':desired,
                        'scope':'Prerequisite setup only. Move the real Ground pawn near the requested area; original range, line-of-sight, footprint and admitted-birth checks remain.'})
                    building,receipt=place(session,definition,point=desired)
                    break
                except RuntimeError as error:
                    from runtime_evidence import RuntimeEvidenceUnavailable
                    if isinstance(error,(RuntimeEvidenceUnavailable,PreparationSkipped)): raise
                    errors.append(str(error))
            else:
                session.write_json(session.stage_output/'placement-preparation-skipped.json',{'attempts':errors})
                raise PreparationSkipped('No valid prerequisite building footprint within the short preparation budget')
    else:
        building,receipt=place(session,definition,offset=offset)
    key=building['id'];admitted[definition]=key
    if definition==1:
        session.wait('construction-work-binding',lambda:any(v['stable_id']==key and v['local_simulation']
            for e in session.clients for v in session.states[e['name']].get('building_states',[])))
        before_work=next(v for e in session.clients for v in session.states[e['name']]['building_states']
                         if v['stable_id']==key and v['local_simulation'])
    # Remove the long approach from every preparation. The original building
    # work capability, slot allocation and completion still run unchanged.
    if definition==1:
        with session.preparation('engineer positions around the admitted building'):
            from population_combat_preparation import positions
            workers=[o for o in objects(session,True) if o['id'] in worker_keys]
            requests=[]
            for index,worker in enumerate(workers):
                angle=math.tau*index/max(1,len(workers));point=list(building['position'])
                point[0]+=1000*math.cos(angle);point[1]+=1000*math.sin(angle)
                requests.append((worker,point,False,None))
            prepared=positions(session,requests)
            session.write_json(session.stage_output/'engineer-position-preparation.json',{'positions':prepared,
                'scope':'User-authorized spatial setup only; construction work, slots and completion remain real gameplay.'})
    def construction_progress():
        return max((v['work_done'] for e in session.clients
                    for v in session.states[e['name']].get('building_states',[])
                    if v['stable_id']==key and v['local_simulation']),default=None)
    if definition==1:
        if building['ownership_policy']!=4 or identity(building['simulator'])!=resource_owner:
            raise RuntimeError('New resource building escaped the indivisible resource group')
        input_and_wait(session,'selection_objects',commander['player'],objects=sorted(worker_keys|{key}))
        def grouped_work():
            state=next((v for e in session.clients for v in session.states[e['name']]['building_states']
                        if v['stable_id']==key and v['local_simulation']),None)
            if not state: return False
            view={o['id']:o for o in objects(session,True)}
            if key not in view or identity(view[key]['simulator'])!=resource_owner or view[key]['owner_version']!=building['owner_version']:
                raise RuntimeError('Selecting a resource building split its ownership group')
            return state['work_done']>before_work['work_done'] and all(
                source in view and identity(view[source]['simulator'])==resource_owner for source in state.get('contributors',[]))
        session.wait('group-construction-work',grouped_work)
    if definition==8:
        claim=population(building)
        with session.preparation('conversion trigger work'):
            owner=next(e for e in session.clients if e['player']==identity(current(session,key)['simulator']))
            prepared,prepared_input=input_and_wait(session,'fixture_construction_progress',owner['player'],object=key)
            session.write_json(session.stage_output/'conversion-work-preparation.json',{'input':prepared_input,'result':prepared,
                'scope':'Supply prerequisite work directly. Committed completion and atomic population conversion remain required; full construction duration is not measured.'})
            session.wait("replacement",lambda:all(
                any(o['id']==key and o['life']==3 for o in session.states[e['name']].get('scene_objects',[]))
                and any(o['parent']==key and o['definition']=='unit/6' and o['life']==1 and o['entity']
                        for o in session.states[e['name']].get('scene_objects',[])) for e in session.clients)
                and sum(o['parent']==key and o['definition']=='unit/6' and o['life']==1
                        for o in objects(session,True))==1)
        child=next(o for o in objects(session,True) if o["parent"]==key and o["definition"]=="unit/6" and o["life"]==1)
        previous=next(o for o in objects(session) if o["id"]==key)
        commits=[d for d in new_decisions(session) if d["status"]==0 and d.get("capability")=="replacement"]
        atomic=any({identity(o["id"]) for o in d["objects"]}.issuperset({identity(key),identity(child["id"])}) for d in commits)
        prove(session,"replacement_population",bool(claim) and population(child)==claim and previous["life"]==3 and atomic,
              "One client transaction retires the construction claim and creates its equally sized population claim",receipt,"replacement.json")
        released=getattr(session,'population_death_evidence',None)
        session.prove('population_cancel_reject_death',bool(released) and bool(getattr(session,'population_branch_evidence',[])) and bool(claim) and atomic,
            'Preview cancellation and final resource rejection leaked no claim; real death released slots and conversion transferred its existing claim atomically',
            getattr(session,'population_branch_evidence',[])+([released] if released else [])+[session.stage_output/'replacement.json'])
    else:
        def completed():
            return all(any(o['id']==key and o['completed'] and o['life']==1
                for o in session.states[e['name']].get('scene_objects',[])) for e in session.clients)
        if definition in (4,101,102,5):
            with session.preparation('prerequisite construction completion'):
                if not completed():
                    owner=next(e for e in session.clients if e['player']==identity(current(session,key)['simulator']))
                    prepared,prepared_input=input_and_wait(session,'fixture_construction_progress',owner['player'],object=key)
                    session.write_json(session.stage_output/'work-preparation-readback.json',{'input':prepared_input,'result':prepared})
                session.wait('completed',completed)
        else:
            session.wait('completed',completed,progress=construction_progress)
        if definition==1:
            prove(session,"construction",any(d["status"]==0 and d["kind"]==6 for d in new_decisions(session)),
                  "Real engineering work and its selected building stayed on the resource owner through committed completion",receipt,
                  worker_selection,"construction-workers-grouped.json","admitted-1.json",
                  "group-construction-work.json","completed.json")
    session.finish()


def production(session, admitted):
    manifest=json.loads(__import__('pathlib').Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
    if manifest.get('strings',{}).get('acceptance/preparation')=='gf-fast-spatial-v2':
        live={o['id']:o for o in objects(session,True) if o['life']==1 and o['entity']}
        existing={o['parent'] for o in live.values() if o['definition']=='unit/101' and o.get('parent') in live
                  and live[o['parent']]['definition']=='building/4' and o['team']==1}
        if existing: admitted[4]=min(existing)
    session.begin("production-births")
    producers={admitted[4],admitted[101]}
    with session.preparation('completed production hosts'):
        session.wait('production-hosts-ready',lambda:all((host:=current(session,p)) is not None and host['completed'] for p in producers))
    session.wait("production-births",lambda: all(any(o["parent"]==p and o["life"]==1 and o["entity"] for o in objects(session,True)) for p in producers))
    prove(session,"production",True,"Both ordinary and data-only producer definitions created real children through lifecycle commits","production-births.json")
    # These factories were manually placed. Their production policy does not
    # require territory supply, so their births cannot prove the separate
    # territory_supply/supply_loss_resume assertions. Both remain mandatory.
    session.write_json(session.stage_output/"production-power-scope.json",{
        "manual_producers":sorted(producers),
        "verified":"Manual factories create children through their production capability",
        "unverified":["territory_supply","supply_loss_resume"]})
    session.finish()


def inventory_combination(session, definition):
    session.begin("inventory-combination-"+definition.replace("/","-"))
    candidates=[o for o in objects(session,True) if o["definition"]==definition and o["team"]==1 and o["life"]==1]
    if definition.startswith('building/'):
        candidates=[o for o in candidates if o['completed']]
    if not candidates:
        from acceptance_session import PreparationSkipped
        raise PreparationSkipped("No ready inventory carrier for "+definition)
    carriers=[o for o in objects(session,True) if o['definition']=='unit/3' and o['team']==1 and o['life']==1]
    if not carriers: raise RuntimeError('Inventory delivery has no living extraction carrier')
    target=min(candidates,key=lambda o:min(math.dist(o['position'],c['position']) for c in carriers))
    if not all(target.get("inventory",{}).get(c) for c in ("cargo","processing")):
        raise RuntimeError("Carrier lacks its configured cargo/processing combination: "+definition)
    # Keep the host at its valid native spawn. A loaded flying miner can be
    # over a navigation gap; using that position as a Mass host destination
    # made an otherwise ready inventory fixture impossible to prepare.
    # deliver_inventory relocates the real loaded miner beside this host and
    # still requires the original public delivery and processing decisions.
    deliver_inventory(session,target)
    session.inventory_combination_evidence=getattr(session,"inventory_combination_evidence",{})
    session.inventory_combination_evidence[definition]=session.stage_output/("inventory-process-"+target["id"]+".json")
    required={"unit/101","unit/102","building/102"}
    if required.issubset(session.inventory_combination_evidence):
        session.prove("inventory_composition",True,
            "Actor and Mass combinations each received real mined cargo and processed it through owner commits",
            list(session.inventory_combination_evidence.values()))
    session.finish()


def buildings(session):
    prepare_construction_site(session)
    placement_cancel_and_reject(session)
    prepare_population(session,25)
    admitted={}
    for definition,offset in zip((1,8,4,101,102,5),((1200,0),(-1200,0),(0,1200),(0,-1200),(1200,1100),(-1200,-1100))):
        construct(session,definition,offset,admitted)
    production(session,admitted)
    for definition in ("unit/101","unit/102","building/102"):
        inventory_combination(session,definition)
    return admitted


def deliver_inventory(session,target,wait_for_processing=True):
    """A real loaded vehicle navigates to a capability host, then uses its public interaction API."""
    continuing=bool(getattr(session.args,'resume_summary',None))
    original_start=session.last_decision
    original_processed=sum(session.states[entry(session,'Red','Commander')['name']]['resources']['processed_'+c] for c in ('blue','red'))
    session.control("gameplay",enable_mining=True)
    with session.preparation('enable mining before querying live deposits'):
        session.wait('inventory-mining-ready',lambda:all(session.states[e['name']].get('mining_ready') for e in session.clients),timeout=15)
    prepared_source=getattr(session,'shared_inventory_sources',{}).get(target['definition'])
    def loaded():
        return [o for o in objects(session,True) if o["definition"]=="unit/3" and o["team"]==target["team"] and o["life"]==1
                and (not prepared_source or identity(o['id'])==prepared_source)
                and o.get("inventory",{}).get("blue",0)+o.get("inventory",{}).get("red",0)>0]
    if not loaded():
        if getattr(session.args,'resume_summary',None):
            from acceptance_session import PreparationSkipped
            raise PreparationSkipped('The shared prepared cargo is no longer available for '+target['definition'])
        # Enabling the subsystem does not issue a player mining command.
        # Choose a real legal nearby deposit and use the normal task API.
        from pathlib import Path
        manifest=json.loads(Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
        numbers=manifest['numbers'];commander=entry(session,'Red' if target['team']==1 else 'Blue','Commander')
        live={int(v['id']):v for v in session.states[commander['name']]['mining_clusters']}
        carriers=[o for o in objects(session,True) if o['definition']=='unit/3' and o['team']==target['team'] and o['life']==1]
        choices=[]
        for i in range(int(float(numbers['resource_map/Clusters/count']))):
            prefix=f'resource_map/Clusters[{i}]/';cluster=int(float(numbers[prefix+'ClusterId']));state=live.get(cluster)
            if not state or state['remaining']<=0 or not state[commander['team'].lower()+'_allowed']: continue
            point=[float(numbers[prefix+'Center/'+a]) for a in 'XYZ']
            for carrier in carriers:
                choices.append((math.dist(carrier['position'],point)+math.dist(point,target['position']),carrier,cluster,point))
        if not choices: raise RuntimeError('No legal deposit and living miner can prepare cargo')
        _,carrier,cluster,point=min(choices,key=lambda v:v[0])
        with session.preparation('miner beside the legal cargo preparation deposit'):
            from population_combat_preparation import position
            start=list(point);start[0]+=1800
            prepared,prepared_input=position(session,carrier,start)
            session.write_json(session.stage_output/'cargo-position-preparation.json',{'input':prepared_input,'result':prepared,
                'scope':'Spatial setup only; ore extraction and cargo are still committed by the real mining rule.'})
        session.input_ordinal=getattr(session,'input_ordinal',0)+1
        mining={'player':commander['player'],'command':session.stage_number*100000+session.input_ordinal,
                'actors':[carrier['ordinal']],'intent':'Mine','cluster':cluster,'x':point[0],'y':point[1],'z':point[2]}
        session.write_json(session.stage_output/'cargo-preparation-input.json',{'source':carrier['id'],'target':target['id'],'input':mining})
        session.control('gameplay',enable_mining=True,unit_orders=[mining])
        if manifest.get('strings',{}).get('acceptance/preparation')=='gf-fast-spatial-v2':
            from population_combat_preparation import prepare_mining_approach
            prepare_mining_approach(session,carrier['id'])
    suffix=target["id"]
    with session.preparation('actual cargo before inventory interaction'):
        session.wait("cargo-"+suffix,lambda:bool(loaded()))
    source=min(loaded(),key=lambda o:math.dist(o["position"],target["position"]))
    commander=entry(session,"Red" if target["team"]==1 else "Blue","Commander")
    session.input_ordinal=getattr(session,"input_ordinal",0)+1
    task={"player":commander["player"],"command":session.stage_number*100000+session.input_ordinal,
          "actors":[source["ordinal"]],"intent":"Move",
          "x":target["position"][0]-760,"y":target["position"][1],"z":target["position"][2]}
    session.control("gameplay",enable_mining=True,unit_orders=[task])
    with session.preparation('loaded miner position beside its inventory target'):
        from population_combat_preparation import position
        point=list(target['position']);point[0]-=760
        prepared,prepared_input=position(session,current(session,source['id']),point)
        session.write_json(session.stage_output/'delivery-position-preparation.json',{'input':prepared_input,'result':prepared,
            'scope':'Spatial setup only; the original cargo is retained and delivery/processing still require real committed results.'})
    session.control('gameplay',enable_mining=True,unit_orders=[task])
    session.wait("docked-"+suffix,lambda: (carrier:=current(session,source["id"])) is not None
        and (host:=current(session,target["id"])) is not None
        and math.dist(carrier["position"][:2],host["position"][:2])<=790)
    # Direct input can transfer the vehicle to Commander before docking.
    # Route the interaction to the actual committed simulator at that point.
    owner=next(e for e in session.clients if e["player"]==identity(current(session,source["id"])["simulator"]))
    before=session.states[entry(session,"Red","Commander")["name"]]["resources"]
    processed=original_processed if continuing else before["processed_blue"]+before["processed_red"]
    decision_start=original_start if continuing else session.last_decision
    def automatic_delivery():
        return next(((event,d) for event,d in session.decisions.items() if continuing and d['sequence']>decision_start
                     and d['kind']==10 and d['status']==0 and identity(d['target'])==identity(target['id'])
                     and identity(d.get('work_source','0'))==identity(source['id'])),None)
    automatic=automatic_delivery();result=None;rejected=None
    if not automatic:
        try:
            result,receipt=input_and_wait(session,"inventory_delivery",owner["player"],source=source["id"],target=target["id"])
        except RuntimeError as error:
            automatic=automatic_delivery()
            if not automatic or 'Inventory delivery input was not retained' not in str(error): raise
            rejected=str(error)
    if automatic:
        delivery_event,delivery=automatic
        receipt='inventory-native-trigger-'+target['id']+'.json'
        session.write_json(session.stage_output/receipt,{'mode':'native automatic delivery after real docking input',
            'move':task,'source':source['id'],'target':target['id'],'delivery':delivery,'manual_rejection':rejected,
            'scope':'Original committed public inventory event, not a fabricated manual completion. Processing and all-endpoint observation remain required.'})
    else:
        if not result["result"].get("committed"):
            raise RuntimeError("Public inventory delivery did not commit")
        delivery_event=identity(result['request'])
    session.control('gameplay',enable_mining=True)
    if not wait_for_processing:
        session.wait('actual-delivery-before-handoff',lambda:delivery_event in session.decisions
            and session.observed_by_active(delivery_event))
        delivery=session.decisions[delivery_event]
        if delivery['kind']!=10 or delivery['status']!=0 or identity(delivery['target'])!=identity(target['id']):
            raise RuntimeError('Pending processing fixture lacks its original committed cargo delivery')
        session.write_json(session.stage_output/'pending-processing-delivery-preparation.json',{
            'source':source,'target':target,'delivery':delivery,'input':receipt,
            'scope':'Actual extracted cargo and committed delivery. Processing receipt is deliberately left pending for the unchanged resource-group handoff assertion.'})
        return
    def target_commits():
        return [d for event,d in session.decisions.items() if d['sequence']>decision_start
                and d['kind']==11 and d['status']==0 and identity(d['target'])==identity(target['id'])
                and session.observed_by_active(event)]
    session.wait("processed-"+suffix,lambda: (host:=current(session,target["id"])) is not None
        and host["inventory"]["revision"]>target["inventory"]["revision"]
        and sum(session.states[entry(session,"Red","Commander")["name"]]["resources"]["processed_"+c] for c in ("blue","red"))>processed
        and bool(target_commits()) and session.observed_by_active(delivery_event))
    delivery=session.decisions.get(delivery_event)
    if not delivery or delivery['kind']!=10 or delivery['status']!=0 or identity(delivery['target'])!=identity(target['id']):
        raise RuntimeError('Inventory interaction has no matching committed delivery to its actual target')
    session.write_json(session.stage_output/('inventory-process-'+suffix+'.json'),
        {'input':receipt,'source':source['id'],'target':target['id'],'delivery':delivery,'processing':target_commits(),
         'inventory_before':target['inventory'],'inventory_after':current(session,target['id'])['inventory']})


def normal_roles(session):
    from integrated_roles import fixed_reconnect
    fixed_reconnect(session)


def battle(session):
    session.begin("combined-mass-wm01-and-summon-battle")
    session.decision_start=session.last_decision
    previous_hits=set(session.hits)
    # Test the four real weapon families in short, separated lanes. The full
    # 600-unit/500-flight room remains running; setup need not march every
    # unit across the map or wait for a formation slot to reach the enemy.
    for team,number in (("Red",1),("Blue",2)):
        commander=entry(session,team,"Commander")
        if not any(o['definition']=='unit/5' and o['team']==number and o['life']==1 for o in objects(session,True)):
            with session.preparation('real summon for the weapon lane '+team):
                caster=max((o for o in objects(session,True) if o['definition']=='unit/1' and o['team']==number and o['life']==1),key=lambda o:o['health'])
                select(session,commander,[caster])
                input_and_wait(session,'unit_skill',commander['player'],point=caster['position'])
                session.wait('battle-summons-'+team.lower(),lambda:any(o['kind']==1 and o['team']==number and o['definition']=='unit/5'
                    and o['life']==1 and o['entity'] for o in objects(session,True)))
    from population_combat_preparation import positions
    attacker_keys=[];requests=[];lanes=[];used=set()
    lane_y=0
    with session.preparation('four actual weapon lanes'):
        for index,definition in enumerate(('unit/1','unit/2','unit/5','unit/101')):
            attacker=max((o for o in objects(session,True) if o['definition']==definition and o['team']==1 and o['life']==1),key=lambda o:o['health'])
            target_definition=definition if definition!='unit/101' else 'unit/2'
            target=max((o for o in objects(session,True) if o['definition']==target_definition and o['team']==2 and o['life']==1
                        and o['id'] not in used),key=lambda o:o['health'])
            used.add(target['id']);x=-9000+index*6000
            distance=200 if definition=='unit/5' else 900
            a=[x,lane_y-distance,attacker['position'][2]];b=[x,lane_y+distance,target['position'][2]]
            requests.extend(((target,b,True,None),(attacker,a,False,None)))
            attacker_keys.append(attacker['id']);lanes.append({'definition':definition,'source':attacker['id'],'target':target['id']})
        session.write_json(session.stage_output/'weapon-lane-preparation-plan.json',{'lanes':lanes,
            'lives':[{'object':o['id'],'generation':o.get('generation',1),'point':p,'locked':locked} for o,p,locked,_ in requests]})
        # Relocate passive targets immediately; the ownership wait can otherwise
        # let the ongoing battle kill a chosen target before setup starts.
        prepared=positions(session,requests,find_nearby=True)
        commander=entry(session,'Red','Commander')
        controlled=[o for o,_,_,_ in requests if o['id'] in attacker_keys and o['definition'] in ('unit/1','unit/2')]
        input_and_wait(session,'selection_objects',commander['player'],objects=[o['id'] for o in controlled])
        session.wait('exact-weapon-source-owners',lambda:all((o:=current(session,v['id'])) is not None
            and identity(o['simulator'])==commander['player'] and not any(q.get('handoff_pending')
                for e in session.clients for q in session.states[e['name']].get('objects',[]) if q['id']==o['id']) for v in controlled))
        session.write_json(session.stage_output/'weapon-lane-preparation.json',{'lanes':lanes,'positions':prepared,
            'scope':'User-authorized positions and passive target action locks only; actual native fire, geometry, speed, damage and death are still required.'})
    commander=entry(session,'Red','Commander')
    # Native automatic targeting uses the nearby passive opponents. An empty
    # task intent is not a valid player command for WM01 and is unnecessary
    # preparation for the weapon speed and hit/decision correspondence checks.
    def effects():
        aggregate={}
        for e in session.clients:
            for key,v in session.states[e["name"]].get("observed_effects",{}).items():
                aggregate.setdefault(key,[]).append(v)
        return aggregate
    def linked_hits():
        definitions={identity(o['id']):o['definition'] for o in objects(session)}
        found={d:[] for d in ('unit/1','unit/2','unit/5','unit/101')}
        for event,hit in session.hits.items():
            definition=definitions.get(identity(hit['source']))
            if event in previous_hits or not hit['source_known'] or hit['source_kind']!=1 or definition not in found: continue
            decision=session.decisions.get(event)
            if decision and decision['kind']==0 and decision['status']==0 and identity(decision['target'])==identity(hit['target']) \
                    and decision['applied_damage']+decision['absorbed_damage']>0 \
                    and session.observed_by_active(event):
                found[definition].append({'hit':hit,'decision':decision})
        return found
    observed_definitions={o['definition'] for o in objects(session,True) if o['kind']==1 and o['life']==1}
    required={'unit/1','unit/2','unit/5','unit/101'}
    waiting=required & observed_definitions
    errors=[]
    try:
        session.wait("swept-combat",lambda: all(any(k.startswith(d+"/") and any(v["launched"] and v["terminated"] for v in vs)
            for k,vs in effects().items()) for d in waiting)
            and all(linked_hits()[d] for d in waiting)
            and any(d["kind"]==0 and d["status"]==0 and int(identity(d["death_event"]),16) for d in new_decisions(session)))
    except RuntimeError as error:
        errors.append(str(error))
    session.write_json(session.stage_output/'combat-observed.json',session.states)
    deaths=[d for d in new_decisions(session) if d["kind"]==0 and d["status"]==0 and int(identity(d["death_event"]),16)]
    links=linked_hits()
    session.write_json(session.stage_output/'causal-combat.json',{'by_definition':links,'death_decisions':deaths})
    try:
        prove(session,"mass_combat",bool(deaths),"Native attacks and swept projectile terminations produced authoritative terminal damage","combat-observed.json")
    except RuntimeError as error:
        errors.append(str(error))
    try:
        prove(session,"wm01",bool(links['unit/2']),"WM01 geometry and target-owner damage are linked independently of Ground","combat-observed.json","causal-combat.json")
    except RuntimeError as error:
        errors.append(str(error))
    try:
        prove(session,'combat_event_correspondence',all(links.values()),
              'Each tested Mass weapon links actual geometric hits to the identical target-owner damage decision on all six clients',
              'causal-combat.json','swept-combat.json')
    except RuntimeError as error:
        errors.append(str(error))
    guns=[v for k,vs in effects().items() if k.casefold() in {d+"/basicattack" for d in ("unit/1","unit/2","unit/5")} for v in vs if v["launched"]]
    try:
        prove(session,"machinegun_speed",len(guns)>=3 and all(abs(v["minimum_speed"]-4800)<.1 and abs(v["maximum_speed"]-4800)<.1 for v in guns),
              "All three production basic-machinegun recipes observed 4800 cm/s launch velocity", "combat-observed.json")
    except RuntimeError as error:
        errors.append(str(error))
    restore=[]
    for value in prepared:
        original=value['object'];living=current(session,original['id'])
        if original['id'] in used and living:
            restore.append((living,living['position'],False,value['result']['result']['token']))
    if restore: positions(session,restore,find_nearby=True,allow_terminal=True)
    if errors:
        session.write_json(session.stage_output/'combat-failures.json',{'failures':errors,'missing_entities':sorted(required-observed_definitions)})
        raise RuntimeError('; '.join(errors))
    session.finish()


def run(session):
    formation(session)
    buildings(session)
    from integrated_supply import run as territory_and_supply
    territory_and_supply(session)
    teleport_and_cards(session)


def authored_closure(session, only=None):
    # Content and Blueprint verdicts require their readback as well as runtime
    # executions. Keep them unverified if the suite never exercised those assets.
    session.begin("authored-content-and-blueprint-readback"+("-"+only if only else ""))
    from pathlib import Path
    root=Path(__file__).resolve().parents[2]
    profile=json.loads((root/"Config/Hybrid/AcceptanceProfile.json").read_text(encoding="utf-8"))
    migration=json.loads((root/"Config/Hybrid/ProductionBlueprintMigration.json").read_text(encoding="utf-8"))
    classes={o.get("actor_class") for o in objects(session)}
    classes.update(c for e in session.clients for c in session.states[e["name"]].get("authored_actor_classes",[]))
    rendered=next(e for e in session.clients if e['rendered'])
    if only in (None,'map_entry'):
        map_action,map_receipt=input_and_wait(session,'review_entry',rendered['player'])
        map_view=map_action['result']
        prove(session,'map_entry',map_view['units']==600 and map_view['additional_flights']==500
              and map_view['object_count']>=600 and map_view.get('fixed_roles') is True
              and all({'selected','idle_seconds','handoff_pending','simulation_owner','ownership_group','group_version'}.issubset(v)
                      for v in map_view['ownership_samples']),
              'Saved original-map entry produced launch configuration, queried fixed seats and indivisible ownership groups',map_receipt)
    session.write_json(session.stage_output/"runtime-bindings.json",{"classes":sorted(c for c in classes if c),"profile":profile,
        "migration":migration,"states":session.states})
    spawned={o["definition"] for o in objects(session)}
    if only in (None,'generic_content'):
        if getattr(session.args,'collect_only',False) and (not {"unit/101","unit/102","building/101","building/102"}.issubset(spawned)
                or session.checks.get("inventory_composition",{}).get("status")!="passed"):
            from integrated_remaining import PrerequisiteMissing
            raise PrerequisiteMissing('Generic content requires the production/inventory fixtures skipped in the original G collection')
        prove(session,"generic_content",{"unit/101","unit/102","building/101","building/102"}.issubset(spawned)
            and getattr(session,"variant_card_committed",False) and session.checks.get("inventory_composition",{}).get("status")=="passed",
            "Isolated units/buildings/skills/cards/presentation use exported capability profiles", "runtime-bindings.json")
    required={v['asset']+'_C' for v in migration['operations']}
    if only in (None,'production_blueprints'):
        prove(session,"production_blueprints",migration.get("migration_complete") and required.issubset(classes),
              "Every exact migrated class instantiated during production work", "runtime-bindings.json")
    def called(client,cls,fn):
        return next((r for r in session.states[client['name']].get('blueprint_executions',[])
                     if r['class']==cls and r['function']==fn),{})
    # Check the exact migrated call in each asset, not an unrelated mining
    # interface that a native construction presentation deliberately disables.
    migrated_calls={(v['asset']+'_C',v['replacement_function'].rsplit(':',1)[1]) for v in migration['operations']}
    if only in (None,'production_blueprint_execution'):
        prove(session,'production_blueprint_execution',all(
              called(rendered,c,f).get('enabled',0)>0 and all(
                  called(e,c,f).get('disabled',0)>0 and called(e,c,f).get('enabled',0)==0
                  for e in session.clients if not e['rendered']) for c,f in migrated_calls),
              'Each exact production class executed its migrated presentation-policy call; all LogicOnly copies returned disabled',
              'runtime-bindings.json')
    session.finish()
