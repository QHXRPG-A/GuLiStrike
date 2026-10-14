"""Commander phases driven through selection, orders and production UI requests."""
from __future__ import annotations

import base64
import json
import math
import struct
import time

from integrated_acceptance import entry, identity, input_and_wait, objects, prove


def current(session, key):
    """A handoff may temporarily freeze every copy of this object."""
    return next((o for o in objects(session, True) if o["id"] == key and o["life"] == 1), None)


def center(values):
    return [sum(o["position"][i] for o in values)/len(values) for i in range(3)]


def select(session, commander, values):
    if not values:
        raise RuntimeError("Selection has no living gameplay entities")
    minimum = [min(o["position"][i] for o in values)-20 for i in range(3)]
    maximum = [max(o["position"][i] for o in values)+20 for i in range(3)]
    action, evidence = input_and_wait(session,"selection_box",commander["player"],min=minimum,max=maximum)
    expected={o["ordinal"] for o in values}
    session.wait("selection-"+action["id"],lambda: expected.issubset(set(session.states[commander["name"]]["scene_ui"]["selected_soldiers"])))
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
    selected,_=select(session,commander,army)
    chosen=[o for o in army if o["ordinal"] in selected]
    chosen_keys={v['id'] for v in chosen}
    def selected_owners_committed():
        active=[o for o in objects(session,True) if o['id'] in chosen_keys]
        return len(active)==len(chosen_keys) and {o['id'] for o in active}==chosen_keys and all(
            identity(o['simulator'])==commander['player'] for o in active if o['ownership_policy']==1)
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


def teleport_landing(session):
    session.begin("teleport-collection-and-landing")
    commander=entry(session,"Red","Commander")
    army=[o for o in objects(session,True) if o["kind"]==1 and o["team"]==1 and o["life"]==1 and o["health"]>0]
    source=center(army)
    # The two-point tactical input performs collection, phase-out and relocation.
    reply,receipt=input_and_wait(session,"teleport_source",commander["player"],point=source)
    session.wait("teleport-collected",lambda: session.states[commander["name"]]["scene_ui"]["teleport"].get("phase")==2
        and session.states[commander["name"]]["scene_ui"]["teleport"].get("members",0)>0)
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
    prove(session,"teleport",bool(phased),"Two formal inputs collected, phased and relocated the same stable identities",
          receipt,"teleport-collected.json",release,"teleport-landed.json")
    # Spatial field observations are checked independently of successful UI input.
    collected=json.loads((session.stage_output/"teleport-collected.json").read_text(encoding="utf-8"))
    field_states=collected[commander["name"]].get("scene_fields",[])
    prove(session,"spell_fields",any(f.get("cast")==collected[commander["name"]]["scene_ui"]["teleport"].get("cast") and f.get("initialized") for f in field_states),
          "Actual persistent field entity belongs to the committed cast", "teleport-collected.json","teleport-landed.json")
    session.finish()


def card_selection(session):
    session.begin("rogue-selection-submit-and-return")
    commander=entry(session,"Red","Commander")
    session.decision_start=session.last_decision
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
    original={o['id']:o for o in army}
    _,cancel_source=input_and_wait(session,'teleport_source',commander['player'],point=center(army))
    session.wait('cancel-collected',lambda: session.states[commander['name']]['scene_ui']['teleport']['phase']==2
        and any(o['phased'] and o['id'] in original for o in objects(session,True)))
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
    session.write_json(session.stage_output/'cancel-placement-contract.json',{'cast':cast,'source':source,
        'profile_radius_cm':radius,'return_search_radius_cm':radius*4,'frozen':frozen,
        'returned':{o['id']:relocation_body(o) for o in objects(session,True) if o['id'] in returning},
        'contract':'Actual external-control application acknowledgements agree on six endpoints; existing tasks may move objects after release.'})
    prove(session,'teleport_cancel',bool(returning) and returned(),
          'Formal cancel applied and acknowledged collision-safe source-area placements, preserving every collected identity on six endpoints',
          cancel_source,'cancel-collected.json',cancel_receipt,'cancel-returned.json','cancel-placement-contract.json')
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
    if not any(missing.values()): return
    session.begin('development-resource-supply-'+team.lower())
    receipts=[];events=[]
    for color,deficit in missing.items():
        while deficit:
            amount=1<<min(10,deficit.bit_length()-1)
            reply,evidence=input_and_wait(session,'resource_supply',commander['player'],color=color,amount=amount)
            if not reply['result'].get('committed'):
                raise RuntimeError('Development supply was not committed by the team owner')
            receipts.append(evidence);events.append(identity(reply['request']));deficit-=amount
    def committed():
        return all(event in session.decisions and session.decisions[event]['kind']==1
                   and session.decisions[event]['status']==0
                   and len(session.result_observers.get(event,set()))==len(session.clients) for event in events)
    session.wait('supply-committed',committed)
    session.write_json(session.stage_output/'supply-provenance.json',{
        'fixture':'development_resource_supply','team':team,'requested_minimum':desired,'amount_added':missing,
        'balance_before':before,'inputs':receipts,'commits':[session.decisions[event] for event in events],
        'scope':'Preparation only. Not mining, processing, production, refund or population evidence.'})
    session.finish()
    # The next gameplay stage records a fresh post-supply baseline.


def place(session, definition, team="Red", offset=(1200,0)):
    manifest=json.loads(__import__('pathlib').Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
    is_conversion=float(manifest['numbers'].get(f'building/{definition}/CompletionUnitTypeId',0))>0
    ground=entry(session,team,"Commander" if is_conversion else "Ground")
    pawn=next(o for o in objects(session,True) if o["definition"]=="player/ground" and o["team"]==(1 if team=="Red" else 2))
    point=list(pawn["position"]);point[0]+=offset[0];point[1]+=offset[1]
    before={o["id"] for o in objects(session)}
    session.decision_start=session.last_decision
    result,receipt=input_and_wait(session,"building_place",ground["player"],definition=definition,point=point)
    if not result["result"].get("accepted"):
        raise RuntimeError("Production placement was rejected: "+json.dumps(result))
    session.wait("admitted-"+str(definition),lambda: any(o["id"] not in before and o["definition"]=="building/"+str(definition) for o in objects(session)))
    building=next(o for o in objects(session) if o["id"] not in before and o["definition"]=="building/"+str(definition))
    return building,receipt


def population(o):
    cap=o.get("capabilities",{}).get("population")
    if not cap: return None
    raw=base64.b64decode(cap["data"],validate=True)
    size=struct.unpack_from("<I",raw)[0]
    if cap["version"]!=1 or len(raw)!=4+size+8: raise RuntimeError("Unrecognized population capability")
    group=raw[4:4+size].decode();units,limit=struct.unpack_from("<II",raw,4+size)
    return group,units,limit


def prepare_population(session, slots):
    """Use a small formal duel to create the production slots required by this room."""
    def free():
        claims=[population(o) for o in objects(session) if o['team']==1 and o['life']<2 and population(o)]
        return claims[0][2]-sum(c[1] for c in claims) if claims else 0
    if free()>=slots: return
    session.begin('combat-population-preparation')
    from pathlib import Path
    profile=json.loads((Path(__file__).resolve().parents[2]/'Config/Hybrid/AcceptanceProfile.json').read_text(encoding='utf-8-sig'))
    speeds={f"unit/{r['Id']}":float(r['MovementSpeedCmPerSecond'])
            for asset in profile['assets'].values() for r in asset.get('rows',[])
            if 'MovementSpeedCmPerSecond' in r and 'Id' in r}
    armies={n:[o for o in objects(session,True) if o['kind']==1 and o['team']==n and o['life']==1 and population(o)] for n in (1,2)}
    if not all(armies.values()): raise RuntimeError('Population preparation requires two real armies')
    homes={n:center(a) for n,a in armies.items()}
    center_y=sum(v[1] for v in homes.values())/2
    # Stay between the two authored armies. The former outside-array route
    # could leave the navigable arena and never reach its low-health targets.
    corridor_x=sum(v[0] for v in homes.values())/2
    cohorts={};inputs=[]
    session.decision_start=session.last_decision
    for team,number in (('Red',1),('Blue',2)):
        commander=entry(session,team,'Commander')
        eligible=armies[number]
        definition=max({o['definition'] for o in eligible},key=lambda d:speeds.get(d,0))
        if speeds.get(definition,0)<=0: raise RuntimeError('Movement definition is absent from the frozen asset readback')
        typed=sorted((o for o in eligible if o['definition']==definition),key=lambda o:o['ordinal'])
        # Match the real 25-member portrait grouping, then use that UI input.
        groups=[typed[i:i+25] for i in range(0,len(typed),25)]
        goal=[corridor_x,center_y+(1400 if number==1 else -1400),homes[number][2]]
        group=min(groups,key=lambda g:(sum(o['health'] for o in g)/len(g),math.dist(center(g),goal)))
        # Portraits narrow a confirmed selection; they cannot select an unseen
        # member. Use the real same-type input first when this seat has no tile.
        # Its ray is evaluated at the current query position on the input frame.
        if group[0]['ordinal'] not in session.states[commander['name']]['scene_ui']['selected_soldiers']:
            input_and_wait(session,'selection_type',commander['player'],soldier=group[0]['ordinal'])
        input_and_wait(session,'selection_group',commander['player'],soldier=group[0]['ordinal'])
        selected=set(session.states[commander['name']]['scene_ui']['selected_soldiers'])
        cohort={o['id'] for o in eligible if o['ordinal'] in selected}
        if not cohort or len(cohort)>25: raise RuntimeError('Portrait did not select its ordinary production cohort')
        cohorts[number]=cohort
        _,receipt=order(session,commander,goal);inputs.append(receipt)
    session.write_json(session.stage_output/'duel-route.json',{'homes':homes,'corridor_x':corridor_x,'center_y':center_y,
        'cohorts':{n:sorted(c) for n,c in cohorts.items()},'source_inputs':inputs,'required_slots':slots})
    session.wait('population-released-by-combat',lambda: free()>=slots and any(
        d['kind']==0 and d['status']==0 and d['state_domain'] in cohorts[1] and int(identity(d['death_event']),16)
        for d in new_decisions(session)))
    session.write_json(session.stage_output/'population-release.json',{'source_inputs':inputs,
        'available_slots':free(),'death_decisions':[d for d in new_decisions(session)
            if d['kind']==0 and d['status']==0 and d['state_domain'] in cohorts[1] and int(identity(d['death_event']),16)]})
    session.population_death_evidence=session.stage_output/'population-release.json'
    for team,number in (('Red',1),('Blue',2)):
        survivors=[o for o in objects(session,True) if o['id'] in cohorts[number] and o['life']==1]
        if survivors:
            commander=entry(session,team,'Commander')
            # The duel never changed this seat's confirmed selection. A new
            # rectangle from moving/dead members is a different player input.
            selected=set(session.states[commander['name']]['scene_ui']['selected_soldiers'])
            if any(o['ordinal'] not in selected for o in survivors):
                raise RuntimeError('The confirmed combat selection lost a living retreat member')
            order(session,commander,homes[number])
    session.wait('post-combat-load-refilled',lambda: sum(session.states[e['name']]['flight_load']['live_additional']
        for e in session.clients)==500)
    session.finish()


def prepare_construction_site(session):
    """Walk the real Ground pawn out of the moving army before placing buildings."""
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
    destination=min(candidates,key=lambda p:math.dist(start[:2],p[:2]))
    input_start=session.states[pilot['name']]['logical_tick']
    timeline=[];reached=False
    def approach():
        nonlocal reached
        actor=ground();dx=destination[0]-actor['position'][0];dy=destination[1]-actor['position'][1]
        distance=math.hypot(dx,dy);reached|=distance<=150
        arrived=reached and actor['movement_mode']==1 and actor['velocity_cm_s']<30
        scale=min(1,distance/3000)
        movement=(0,0) if reached else (dy/distance*scale,dx/distance*scale)
        elapsed=max(0,session.states[pilot['name']]['logical_tick']-input_start)
        # Use the production jump/thrust input to clear the dense allied army,
        # then release it before the plot and wait for an actual landing.
        rocket=not reached and distance>3000 and elapsed%600<180
        inputs={'player':pilot['player'],'move_x':movement[0],'move_y':movement[1],
                'aim_x':destination[0],'aim_y':destination[1],'aim_z':destination[2],
                'sprint':True,'fire':False,'rocket':rocket}
        session.control('ground_combat',ground_inputs=[inputs])
        timeline.append({'time':time.monotonic(),'position':actor['position'],'distance_cm':distance,
                         'reached':reached,'speed_cm_s':actor['velocity_cm_s'],'input':inputs})
        if arrived and distance>1500: raise RuntimeError('Ground stopping distance left the prepared construction area')
        return arrived
    try:
        session.wait('ground-at-construction-site',approach)
    finally:
        session.control()
        session.write_json(session.stage_output/'construction-site-route.json',
            {'start':start,'destination':destination,'samples':timeline,
             'purpose':'Separate placement from moving Mass footprints using ordinary Ground movement and thrust input'})
    session.finish()


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
    # Obtain the shared production headroom when first needed. A building
    # without a unit conversion does not require repeating the duel first.
    if float(manifest['numbers'].get(f'building/{definition}/CompletionUnitTypeId',0))>0:
        prepare_population(session,1)
    desired={c:int(float(manifest['numbers'][f'building/{definition}/Cost/{c.capitalize()}']))+128 for c in ('blue','red')}
    fund(session,desired)
    session.begin("construction-"+str(definition))
    commander=entry(session,'Red','Commander')
    group={o['id']:o for o in session.states[commander['name']]['objects'] if o.get('ownership_policy')==4 and o['life']<2}
    owners={identity(o['simulation_owner']) for o in group.values()}
    if len(owners)!=1: raise RuntimeError('Construction requires one complete resource group')
    resource_owner=next(iter(owners))
    if definition==1:
        worker_keys={o['id'] for o in objects(session,True) if o['kind']==5 and o['team']==1 and o['life']==1}
        if not worker_keys: raise RuntimeError('Construction requires real engineering carriers')
        _,worker_selection=input_and_wait(session,'selection_objects',commander['player'],objects=sorted(worker_keys))
        session.wait('construction-workers-grouped',lambda: all(
            o['id'] in group and identity(o['simulator'])==resource_owner
            for o in objects(session,True) if o['id'] in worker_keys))
    building,receipt=place(session,definition,offset=offset)
    key=building['id'];admitted[definition]=key
    if definition==1:
        if building['ownership_policy']!=4 or identity(building['simulator'])!=resource_owner:
            raise RuntimeError('New resource building escaped the indivisible resource group')
        before_work=next(v for e in session.clients for v in session.states[e['name']]['building_states']
                         if v['stable_id']==key and v['local_simulation'])
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
        session.wait("replacement",lambda: any(o["parent"]==key and o["definition"]=="unit/6" and o["life"]==1 and o["entity"] for o in objects(session,True)))
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
        session.wait("completed",lambda: all(any(o["id"]==key and o["completed"] and o["life"]==1
            for o in session.states[e["name"]].get("scene_objects",[])) for e in session.clients))
        if definition==1:
            prove(session,"construction",any(d["status"]==0 and d["kind"]==6 for d in new_decisions(session)),
                  "Real engineering work and its selected building stayed on the resource owner through committed completion",receipt,
                  worker_selection,"construction-workers-grouped.json","admitted-1.json",
                  "group-construction-work.json","completed.json")
    session.finish()


def production(session, admitted):
    session.begin("production-births")
    producers={admitted[4],admitted[101]}
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
    if not candidates:
        raise RuntimeError("No actual carrier was produced for "+definition)
    carriers=[o for o in objects(session,True) if o['definition']=='unit/3' and o['team']==1 and o['life']==1]
    if not carriers: raise RuntimeError('Inventory delivery has no living extraction carrier')
    target=min(candidates,key=lambda o:min(math.dist(o['position'],c['position']) for c in carriers))
    if not all(target.get("inventory",{}).get(c) for c in ("cargo","processing")):
        raise RuntimeError("Carrier lacks its configured cargo/processing combination: "+definition)
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


def deliver_inventory(session,target):
    """A real loaded vehicle navigates to a capability host, then uses its public interaction API."""
    session.control("gameplay",enable_mining=True)
    def loaded():
        return [o for o in objects(session,True) if o["definition"]=="unit/3" and o["team"]==target["team"] and o["life"]==1
                and o.get("inventory",{}).get("blue",0)+o.get("inventory",{}).get("red",0)>0]
    if not loaded():
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
        session.input_ordinal=getattr(session,'input_ordinal',0)+1
        mining={'player':commander['player'],'command':session.stage_number*100000+session.input_ordinal,
                'actors':[carrier['ordinal']],'intent':'Mine','cluster':cluster,'x':point[0],'y':point[1],'z':point[2]}
        session.write_json(session.stage_output/'cargo-preparation-input.json',{'source':carrier['id'],'target':target['id'],'input':mining})
        session.control('gameplay',enable_mining=True,unit_orders=[mining])
    suffix=target["id"]
    session.wait("cargo-"+suffix,lambda:bool(loaded()))
    source=min(loaded(),key=lambda o:math.dist(o["position"],target["position"]))
    commander=entry(session,"Red" if target["team"]==1 else "Blue","Commander")
    session.input_ordinal=getattr(session,"input_ordinal",0)+1
    task={"player":commander["player"],"command":session.stage_number*100000+session.input_ordinal,
          "actors":[source["ordinal"]],"intent":"Move",
          "x":target["position"][0]+650,"y":target["position"][1],"z":target["position"][2]}
    session.control("gameplay",enable_mining=True,unit_orders=[task])
    session.wait("docked-"+suffix,lambda: (carrier:=current(session,source["id"])) is not None
        and (host:=current(session,target["id"])) is not None
        and math.dist(carrier["position"][:2],host["position"][:2])<=790)
    # Direct input can transfer the vehicle to Commander before docking.
    # Route the interaction to the actual committed simulator at that point.
    owner=next(e for e in session.clients if e["player"]==identity(current(session,source["id"])["simulator"]))
    before=session.states[entry(session,"Red","Commander")["name"]]["resources"]
    processed=before["processed_blue"]+before["processed_red"]
    decision_start=session.last_decision
    result,receipt=input_and_wait(session,"inventory_delivery",owner["player"],source=source["id"],target=target["id"])
    if not result["result"].get("committed"):
        raise RuntimeError("Public inventory delivery did not commit")
    delivery_event=identity(result['request'])
    def target_commits():
        return [d for event,d in session.decisions.items() if d['sequence']>decision_start
                and d['kind']==11 and d['status']==0 and identity(d['target'])==identity(target['id'])
                and len(session.result_observers.get(event,set()))==len(session.clients)]
    session.wait("processed-"+suffix,lambda: (host:=current(session,target["id"])) is not None
        and host["inventory"]["revision"]>target["inventory"]["revision"]
        and sum(session.states[entry(session,"Red","Commander")["name"]]["resources"]["processed_"+c] for c in ("blue","red"))>processed
        and bool(target_commits()) and len(session.result_observers.get(delivery_event,set()))==len(session.clients))
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
    for team,number in (("Red",1),("Blue",2)):
        commander=entry(session,team,"Commander")
        army=[o for o in objects(session,True) if o["kind"]==1 and o["team"]==number and o["life"]==1]
        select(session,commander,army)
        target=center(army);target[1]=1000 if number==1 else -1000
        input_and_wait(session,'unit_skill',commander['player'],point=target)
        order(session,commander,target,intent="")
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
                    and len(session.result_observers.get(event,set()))==len(session.clients):
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
    guns=[v for k,vs in effects().items() if k in {d+"/BasicAttack" for d in ("unit/1","unit/2","unit/5")} for v in vs if v["launched"]]
    try:
        prove(session,"machinegun_speed",len(guns)>=3 and all(abs(v["minimum_speed"]-4800)<.1 and abs(v["maximum_speed"]-4800)<.1 for v in guns),
              "All three production basic-machinegun recipes observed 4800 cm/s launch velocity", "combat-observed.json")
    except RuntimeError as error:
        errors.append(str(error))
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
