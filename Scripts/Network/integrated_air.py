"""Combined native Ship input, ability activation and owner-simulated combat."""
import math
import time
from pathlib import Path

from integrated_acceptance import entry, identity, input_and_wait, objects, prove


def prepare_ship_combat(session):
    from population_combat_preparation import position
    from acceptance_session import PreparationSkipped
    with session.preparation('occupied Ship positions for actual combat'):
        session.begin('ship-combat-position-preparation')
        ships={o['team']:o for o in objects(session,True) if o['definition']=='player/ship' and o['life']==1}
        if set(ships)!={1,2}: raise PreparationSkipped('Two living occupied Ships are needed before combat')
        session.ship_combat_homes={team:list(obj['position']) for team,obj in ships.items()}
        receipts=[]
        for team,y in ((1,3000),(2,-3000)):
            reply,path=position(session,ships[team],[8000,y,6500])
            receipts.append({'team':team,'input':path,'result':reply})
        session.write_json(session.stage_output/'ship-combat-position-readback.json',{
            'receipts':receipts,'scope':'Positions only, through the existing authoritative displacement path. Equipment, members, targeting and committed damage are unchanged.'})
        session.finish()


def restore_ship(session,team):
    from population_combat_preparation import position
    homes=getattr(session,'ship_combat_homes',{})
    ship=next((o for o in objects(session,True) if o['definition']=='player/ship' and o['team']==team and o['life']==1),None)
    if ship is None or team not in homes: return
    with session.preparation('return Ship after its recorded combat proof'):
        session.begin('ship-'+str(team)+'-return-preparation')
        result,path=position(session,ship,homes[team])
        session.write_json(session.stage_output/'ship-return-readback.json',{'input':path,'result':result,
            'scope':'Position restoration after real combat evidence. No healing, resurrection, cooldown, target receipt or ownership mutation.'})
        session.finish()


def run(session):
    if getattr(session.args,'continue_remaining',False): prepare_ship_combat(session)
    session.begin("ship-weapon-and-member-combat")
    pilots={team:entry(session,team,"Air") for team in ("Red","Blue")}
    def ships():
        return {o["team"]:o for o in objects(session,True) if o["definition"]=="player/ship" and o["life"]==1}
    initial=ships()
    if set(initial)!={1,2}: raise RuntimeError("Two occupied Ship entities are required")
    destination=[sum(o["position"][i] for o in initial.values())/2 for i in range(3)]
    session.write_json(session.stage_output/"flight-input-route.json",{"destination":destination,"separation_cm":4000,
        "source":"frozen occupied Ship poses", "controller":"local thrust/turn/boost inputs"})
    route=[]
    def drive():
        current=ships()
        if set(current)!={1,2}: return
        inputs=[]
        for team,number in (("Red",1),("Blue",2)):
            ship=current[number];goal=list(destination);goal[1]+=2000 if number==1 else -2000
            dx,dy,dz=[a-b for a,b in zip(goal,ship["position"])]
            angle=math.radians(ship["yaw"])
            offset=(dx*math.cos(angle)+dy*math.sin(angle),-dx*math.sin(angle)+dy*math.cos(angle),dz)
            length=max(1,math.sqrt(sum(v*v for v in offset)))
            facing=math.degrees(math.atan2(dy,dx));turn=((facing-ship["yaw"]+180)%360-180)/30
            inputs.append({"player":pilots[team]["player"],"thrust":[v/length*min(1,length/3000) for v in offset],"turn":max(-1,min(1,turn)),"boost":length>3000})
        session.control("gameplay",ship_inputs=inputs)
        route.append({"time":time.monotonic(),"poses":{n:o['position'] for n,o in current.items()},"inputs":inputs})
    try:
        # Target input precedes approach, so real automatic attacks during
        # movement count. An arbitrary distance is not a combat prerequisite.
        focus_combat(session,initial[2],"air",drive)
    finally:
        session.control()
        session.write_json(session.stage_output/'flight-input-timeline.json',route)
    session.finish()
    if getattr(session.args,'continue_remaining',False): restore_ship(session,2)


def focus_combat(session,enemy,domain,drive=None):
    """Observe the installed Hangar's real input contract, including automatic skills."""
    pilot=entry(session,"Red","Air")
    ship=next(o for o in objects(session,True) if o["definition"]=="player/ship" and o["team"]==1 and o["life"]==1)
    members={identity(o["id"]):o for o in objects(session,True)
             if o["kind"]==2 and o["life"]==1 and identity(o["parent"])==identity(ship["id"])}
    if not members or any(o["simulator"]!=ship["controller"] for o in members.values()):
        raise RuntimeError("Ship-controlled combat lacks uniquely owned member entities")
    channels=[c for c in session.states[pilot["name"]]["scene_ui"].get("weapon_channels",[]) if c["enabled"]]
    if not channels or any("manual_input" not in c for c in channels):
        raise RuntimeError("The installed equipment has no explicit weapon input contract")
    previous_hits=set(session.hits)
    result,input_path=input_and_wait(session,"ship_target",pilot["player"],target=enemy["id"])
    if identity(result["result"]["target"])!=identity(enemy["id"]) or result["result"]["target_revision"]<=0:
        raise RuntimeError("The controller did not retain the requested target")
    # Automatic channels are driven by the real targeting/integrator path.
    # An authored manual channel still has to accept its actual press/release.
    for channel in channels:
        if not channel["manual_input"]: continue
        input_and_wait(session,"weapon_input",pilot["player"],subject=channel["subject"],slot=channel["slot"],pressed=True)
        input_and_wait(session,"weapon_input",pilot["player"],subject=channel["subject"],slot=channel["slot"],pressed=False)
    bindings={(c["subject"],c["slot"],c["skill"]) for c in channels}
    def linked_hits():
        out=[]
        for event,hit in session.hits.items():
            if event in previous_hits or not hit.get("source_known") or hit["source_kind"]!=2 \
                    or identity(hit["source"]) not in members or identity(hit["target"])!=identity(enemy["id"]) \
                    or (hit["subject"],hit["slot"],hit["skill"]) not in bindings:
                continue
            decision=session.decisions.get(event)
            if decision and decision["kind"]==0 and decision["status"] in (0,1) \
                    and identity(decision["target"])==identity(hit["target"]) \
                    and decision["applied_damage"]+decision["absorbed_damage"]>0 \
                    and session.observed_by_active(event):
                out.append({"hit":hit,"decision":decision})
        return out
    label=domain+"-member-commits"
    def observe():
        if linked_hits(): return True
        if drive: drive()
        return False
    session.wait(label,observe)
    causal=domain+"-causal-combat.json"
    session.write_json(session.stage_output/causal,{"controller":pilot["player"],"ship":ship["id"],
        "target":enemy["id"],"input":result,"channels":channels,"members":members,"hits":linked_hits()})
    check="wingman_"+domain+"_combat"
    prove(session,check,True,"Ship target input led to owned member geometry and target-owner damage observed by all six endpoints",
          input_path,label+".json",causal)
    if domain=="air":
        prove(session,"ship_combat",True,"The current production Hangar uses Ship target control and automatic member weapons; no retired manual-salvo input",
              input_path,label+".json",causal)
    if all(k in session.checks for k in ("wingman_air_combat","wingman_ground_combat")):
        evidence=[Path(e["path"]) for k in ("wingman_air_combat","wingman_ground_combat") for e in session.checks[k]["evidence"]]
        session.prove("wingman_combat",True,"Both air and ground attack chains produced committed damage from Ship-owned members",evidence)


def ground_attack(session):
    session.begin("ship-member-ground-combat")
    ship=next(o for o in objects(session,True) if o["definition"]=="player/ship" and o["team"]==1 and o["life"]==1)
    targets=[o for o in objects(session,True) if (o["kind"]==1 or o["definition"]=="player/ground") and o["team"]==2 and o["life"]==1]
    if not targets: raise RuntimeError("No live enemy ground target remains for the installed ground attack")
    # The production targeting API accepts both actual ground carriers. Prefer
    # the large player target when present, retaining its real health/collision.
    enemy=min(targets,key=lambda o:(o['definition']!='player/ground',math.dist(o["position"],ship["position"])))
    if getattr(session.args,'continue_remaining',False):
        from population_combat_preparation import position
        with session.preparation('ground target beside the actual Air combat position'):
            result,receipt=position(session,enemy,[ship['position'][0]+2500,ship['position'][1],enemy['position'][2]],find_nearby=True)
            session.write_json(session.stage_output/'air-ground-target-preparation.json',{
                'target':enemy['id'],'input':receipt,'result':result,
                'scope':'Target position only; native target selection, member weapons, geometry and committed damage remain required.'})
            enemy=next(o for o in objects(session,True) if o['id']==enemy['id'] and o['life']==1)
    pilot=entry(session,'Red','Air');route=[]
    def drive():
        own=next((o for o in objects(session,True) if o['id']==ship['id'] and o['life']==1),None)
        target=next((o for o in objects(session,True) if o['id']==enemy['id'] and o['life']==1),None)
        if not own or not target: return
        goal=[target['position'][0]+3000,target['position'][1],target['position'][2]+5000]
        dx,dy,dz=[a-b for a,b in zip(goal,own['position'])]
        angle=math.radians(own['yaw']);length=max(1,math.sqrt(dx*dx+dy*dy+dz*dz))
        offset=(dx*math.cos(angle)+dy*math.sin(angle),-dx*math.sin(angle)+dy*math.cos(angle),dz)
        turn=((math.degrees(math.atan2(dy,dx))-own['yaw']+180)%360-180)/30
        control={'player':pilot['player'],'thrust':[v/length*min(1,length/3000) for v in offset],
                 'turn':max(-1,min(1,turn)),'boost':length>3000}
        session.control('gameplay',ship_inputs=[control])
        route.append({'time':time.monotonic(),'ship':own['position'],'target':target['position'],'input':control})
    try:
        focus_combat(session,enemy,"ground",drive)
    finally:
        session.control()
        session.write_json(session.stage_output/'ground-target-flight-inputs.json',route)
    session.finish()
    if getattr(session.args,'continue_remaining',False): restore_ship(session,1)
