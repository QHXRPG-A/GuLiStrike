"""Combined real-input scenarios. Every stage shares the launcher's processes.

No result here follows from sending an input alone: task receipts, native
entities and client commit results are checked separately. Unimplemented or
missing evidence is a failed stage, never a synthetic full-gameplay flag.
"""
from __future__ import annotations

import copy
import json
import math
from pathlib import Path
import time
import uuid

from acceptance_session import AcceptanceSession, preparation


def identity(value):
    return value.replace("-", "").lower()


def entry(session, team, seat):
    reference = next((session.states[e['name']] for e in session.clients if e['name'] in session.states), {})
    members = reference.get("role_members", [])
    player = next((identity(m["player"]) for m in members if m["online"] and m["team"] == team and m["role"] == seat), None)
    return next(e for e in session.clients if e["player"] == player)


def objects(session, owner=False):
    if not owner:
        reference = next((session.states[e['name']] for e in session.clients if e['name'] in session.states), {})
        return reference.get("scene_objects", [])
    return [o for e in session.clients if e['name'] in session.states
            for o in session.states[e["name"]].get("scene_objects", []) if o["local_simulation"]]


def prove(session, name, condition, detail, *files):
    session.prove(name, condition, detail, [session.stage_output / file for file in files])


def input_and_wait(session, kind, player, **values):
    operation = session.operation(kind, player, **values)
    session.wait("input-" + operation, lambda: bool(session.action(operation)
                 and session.action(operation)["status"] in ("completed", "rejected")))
    result=session.action(operation)
    if result['status']=='rejected' or (kind.startswith('teleport_') and result.get('result',{}).get('code')!=0):
        raise RuntimeError('Formal input returned a final rejection: '+json.dumps(result))
    return result, "input-" + operation + ".json"


@preparation
def prepare_cohort(session):
    # Measure the sustained cohort under a fixed real input before deliberately
    # testing handoff. Prefer each Commander's present objects. A Commander can
    # legitimately offload all ordinary Mass while another process loads; use a
    # small real selection to obtain a cohort in that case, leaving other owners
    # represented for the following three-owner stage.
    from integrated_ownership import directory, select
    session.begin("prepare-load-cohort")
    session.control('gameplay',enable_mining=True)
    for team in ("Red", "Blue"):
        commander = entry(session, team, "Commander")
        keys = {key for key, obj in directory(session).items()
                if obj["policy"] == 1 and identity(obj["owner"]) == commander["player"]
                and identity(obj["controller"]) == commander["player"]}
        if not keys:
            eligible = sorted(key for key, obj in directory(session).items()
                              if obj['policy'] == 1 and obj.get('life', 1) == 1
                              and identity(obj['controller']) == commander['player'])
            if not eligible:
                raise RuntimeError("Commander has no live selectable load preparation objects")
            keys = set(eligible[:min(8, len(eligible))])
        select(session, commander, keys)
        session.wait(team.lower()+"-load-sources-selected", lambda: all(
            directory(session)[key]["selected"] and identity(directory(session)[key]["owner"]) == commander["player"]
            for key in keys))
    session.wait("prepared-cohort", lambda: session.settled() and sum(
        session.states[e["name"]].get("flight_load", {}).get("live_additional", 0)
        for e in session.clients) == 500 and all(
        session.states[e["name"]]["flight_load"]["live_additional"] ==
        session.states[e["name"]]["flight_load"]["local_goal"] for e in session.clients))
    session.finish()


def common(session):
    prepare_cohort(session)
    session.begin("startup-flight-and-authority")
    session.legacy("flight_acceptance", "flights")
    prove(session, "additional_flights", True, "500 native pool flights survived three complete lifetimes at their unique owners",
          "flight-baseline.json", "flight-final.json", "flight-timeline.json", "flight-delivery.json")
    # New gameplay objects can become idle during the lifecycle observation.
    # The exact-one assertion describes committed ownership, not the permitted
    # per-object freeze between final export and handoff commit. Settle those
    # transfers without pausing any simulator before capturing this assertion.
    session.seal_evidence("authority-settled")
    from run_hybrid_acceptance import verify_player_simulation
    verify_player_simulation(session.plan, session.states)
    initial = session.baseline
    views = [{(o["id"], o["generation"]): o for o in session.states[e["name"]].get("scene_objects", [])
              if o["life"] < 2 and o.get("ownership_policy") not in (3,5)} for e in session.clients]
    unique = bool(views and views[0]) and all(set(v) == set(views[0]) for v in views)
    for key in views[0]:
        copies = [view[key] for view in views]
        unique &= len({(c["simulator"], c["owner_version"], c["controller"], c["control_version"]) for c in copies}) == 1
        unique &= sum(c["local_simulation"] for c in copies) == (0 if copies[0]["dormant"] else 1)
    prove(session, "unique_simulation", unique, "Full stable-life directory and controller/simulator grants agree across six endpoints",
          "baseline.json", "authority-settled.json")
    before = {(o["id"],o["generation"]): (o["simulator"],o["owner_version"]) for o in initial[session.clients[0]["name"]]["scene_objects"]}
    transfers=[t for e in session.clients for t in session.states[e["name"]].get("handoff_history",[])]
    stable = all(before[key] == (o["simulator"],o["owner_version"]) or any(
        t['object']==o['id'] and t['reason']==2 and t['owner_version']==o['owner_version']
        and identity(t['source'])==identity(o['controller']) for t in transfers)
        for key,o in views[0].items() if key in before)
    prove(session,"stable_ownership",stable,"Ordinary load reports only move eligible Commander-owned idle objects through a committed handoff",
          "baseline.json","authority-settled.json")
    logic = all(not session.states[e["name"]].get("capturing") and
                session.states[e["name"]].get("scene_ui",{}).get("can_present") == e["rendered"] and
                (e["rendered"] or session.states[e["name"]].get("resource_query",{}).get("visual_components") == 0)
                for e in session.clients)
    prove(session,"logic_only",logic,"Helpers retain real query/navigation entities and have no presentation or active performance capture",
          "authority-settled.json")
    session.finish()


def no_echo(session):
    session.begin("receiver-publication-audit")
    session.control("gameplay")
    start = time.monotonic()
    session.wait("receiver-audit",lambda: time.monotonic()-start >= 3 and session.settled())
    ok = True
    for e in session.clients:
        current = session.states[e["name"]]
        directory = {(o["id"],o["generation"]):o for o in current["scene_objects"]}
        before = {(m["id"],m["generation"]):m["accepted"] for m in session.baseline[e["name"]].get("motion_publishers",[])}
        for motion in current.get("motion_publishers",[]):
            key = motion["id"],motion["generation"]
            if key in directory and identity(directory[key]["simulator"]) != e["player"]:
                ok &= motion["accepted"] == before.get(key,0)
        for w in current.get("wingman_states",[]):
            if not w["local_simulation"]:
                ok &= w.get("published",0) == 0 and w.get("simulation_samples",0) == 0
    prove(session,"no_receive_republish",ok,"Query copies do not publish movement or run member simulation", "baseline.json","receiver-audit.json")
    session.finish()


def movement(session, prefix):
    session.begin(prefix+"-movement-and-query")
    session.control(prefix+"_movement")
    role = "Ground" if prefix == "ground" else "Air"
    drivers = [entry(session,team,role) for team in ("Red","Blue")]
    session.wait(prefix+"-movement",lambda: all(session.states[e["name"]].get(prefix+"_complete") for e in drivers))
    check = "ground_player" if prefix == "ground" else "ship"
    prove(session,prefix+"_movement",all(session.states[e["name"]]["checks"].get(check) for e in drivers),
          "Both controllers executed the native movement/boost input and changed actual position", prefix+"-movement.json")
    session.control()
    session.wait(prefix+"-query",session.settled)
    views = [{o["stable_id"]:o for o in session.states[e["name"]].get(prefix+"_states",[])} for e in session.clients]
    for key in views[0]:
        if not all(key in v for v in views) or sum(v[key]["local_simulation"] for v in views) != 1:
            raise RuntimeError(prefix+" query authority differs across clients")
        for v in views[1:]:
            age=abs(v[key].get("pose_tick",0)-views[0][key].get("pose_tick",0))
            tolerance=300 if prefix=="ground" else 50+max(v[key]["speed_limit"],views[0][key]["speed_limit"])*age/60
            if age>120 or math.dist(v[key]["position"],views[0][key]["position"])>tolerance:
                raise RuntimeError(prefix+" query position is stale or divergent")
    prove(session,"ground_query_presentation" if prefix=="ground" else "air_query_presentation",True,
          "Real controller movement converged to the five query copies within their sample-age bound",prefix+"-query.json")
    session.finish()


def rendered_ui(session, check):
    session.begin("rendered-ui-and-bindings")
    rendered=next(e for e in session.clients if e["rendered"])
    value,path=input_and_wait(session,"capture_view",rendered["player"])
    image=Path(value["result"]["image"])
    session.wait("ui",lambda: image.is_file() and image.stat().st_size>0)
    ui=session.states[rendered["name"]].get("scene_ui",{})
    prove(session,check,ui.get("can_present") and ui.get("widgets") and ui.get("possessed"),
          "Rendered role owns its pawn and live UI; frame image retained for visual readback",path,"ui.json")
    session.finish()


def engineering(session):
    session.begin("engineer-cross-owner-orders")
    session.legacy("engineer_acceptance","engineer")
    prove(session,"engineering",True,"Two formal selections moved all sixteen engineers under their unique current simulators exactly once",
          "engineer-baseline.json","engineer-final.json","engineer-timeline.json")
    session.finish()


def mining_transport(session):
    from population_combat_preparation import prepare_mining
    prepare_mining(session)
    session.begin("mining-transport-and-processing")
    from runtime_evidence import additional_flight_counts
    with session.preparation('real additional-flight cohort before formal mining'):
        session.wait('mining-flight-cohort', lambda: sum(additional_flight_counts({
            e['name']: session.states[e['name']] for e in session.clients}).values()) == session.plan['extra_flights'])
    session.legacy("mining_acceptance","mining")
    prove(session,"mining_transport",True,"Formal orders extracted, transported, queued and processed ore; owner and receiving clients conserve it",
          "mining-baseline.json","mining-final.json","mining-timeline.json")
    session.finish()


def commander(session, selection=True):
    from integrated_ownership import run_selection
    if selection: run_selection(session)
    engineering(session)
    mining_transport(session)
    from integrated_commander import run
    run(session)


def ground(session):
    movement(session,"ground")
    rendered_ui(session,"ground_ui")
    ground_combat(session)


def ground_combat(session):
    from population_combat_preparation import prepare_ground_duel
    prepare_ground_duel(session)
    session.begin("ground-formal-combat")
    session.legacy("ground_combat_acceptance","ground_combat")
    prove(session,"ground_combat",True,"Native aim/fire, swept hits, authoritative damage and one terminal death agree on six clients",
          "ground-combat-baseline.json","ground-combat-final.json","ground-combat-timeline.json")
    session.finish()


def air(session):
    air_equipment(session)
    rendered_ui(session,"air_ui")
    from integrated_air import run, ground_attack
    run(session)
    ground_attack(session)


def air_equipment(session):
    session.begin("ship-equipment-members-and-movement")
    session.legacy("wingman_acceptance","ship_wingmen",arguments={
        'prepared_equipment':getattr(session,'prepared_equipment',None)})
    paths=[p.name for p in sorted(session.stage_output.glob("wingman-*.json*"))
           if p.suffix in ('.json', '.gz')]
    for check in ("ship_equipment","wingman_cadence"):
        prove(session,check,True,"Native assembly request committed; fifty members follow Ship controller grants with 30 Hz simulation/10 Hz publication",*paths)
    # The reused member driver also runs the ordinary eight-second flight input.
    prove(session,"ship_movement",all(session.states[entry(session,t,"Air")["name"]]["checks"].get("ship") for t in ("Red","Blue")),
          "Native movement/turn/boost input completed while members retained their Ship owner",*paths)
    prove(session,"air_query_presentation",True,"Member driver checked receiver-only simulation and remote motion sample bounds",*paths)
    session.finish()


def gameplay(session):
    from integrated_remaining import run as sweep
    sweep(session)


def remaining_gameplay(session, selection=True, repeat_logistics=True):
    if repeat_logistics:
        commander(session, selection)
    else:
        from integrated_commander import run as commander_remaining
        commander_remaining(session)
    from integrated_roles import switch_rendered
    switch_rendered(session,"Air")
    air(session)
    switch_rendered(session,"Ground")
    ground(session)
    from integrated_commander import battle, authored_closure
    battle(session)
    authored_closure(session)
    no_echo(session)


def abilities_combat(session):
    """Collect independent failures before repairs, retaining a real-input room."""
    from integrated_commander import formation, teleport_landing, card_selection, card_cancel, teleport_cancel
    from integrated_roles import switch_rendered
    from integrated_air import run as air_combat, ground_attack as member_ground_combat
    failures=[]
    phases=(
        ("load-preparation",prepare_cohort),
        ("formation-and-skills",formation),
        ("teleport-landing",teleport_landing),
        ("card-cancel-return",card_cancel),
        ("teleport-cancel-return",teleport_cancel),
        ("card-submit-return",card_selection),
        ("rendered-ground-seat",lambda s:switch_rendered(s,"Ground")),
        ("ground-movement",lambda s:movement(s,"ground")),
        ("ground-ui",lambda s:role_ui(s,"Ground","ground_ui")),
        ("ground-combat",ground_combat),
        ("rendered-air-seat",lambda s:switch_rendered(s,"Air")),
        ("air-equipment-motion-cadence",air_equipment),
        ("air-ui",lambda s:role_ui(s,"Air","air_ui")),
        ("air-and-member-combat",air_combat),
        ("member-ground-combat",member_ground_combat),
        ("receive-only-publication",no_echo))
    if getattr(session.args,"regression_stage",None)=="vehicle-combat":
        # Collect independent vehicle results even when membership/UI defects
        # block the full role-rotation scenario. Omitted checks remain missing.
        selected={"load-preparation","air-equipment-motion-cadence","air-ui","air-and-member-combat","member-ground-combat",
                  "ground-movement","ground-combat","receive-only-publication"}
        phases=tuple(p for p in phases if p[0] in selected)
    elif getattr(session.args,"regression_stage",None)=="ground-combat":
        selected={"load-preparation","ground-movement","ground-ui","ground-combat","receive-only-publication"}
        phases=tuple(p for p in phases if p[0] in selected)
    elif getattr(session.args,"regression_stage",None)=="air-combat":
        selected={"load-preparation","air-equipment-motion-cadence","air-ui","air-and-member-combat","member-ground-combat","receive-only-publication"}
        phases=tuple(p for p in phases if p[0] in selected)
    for label,execute in phases:
        previous_stage=session.stage_id
        try:
            execute(session)
        except Exception as error:
            import traceback
            if session.stage_id==previous_stage:
                session.stage_number+=1
                session.stage_id=f"{session.stage_number:02d}-{label}-prerequisite"
                session.stage_output=session.output/"stages"/session.stage_id
                session.stage_output.mkdir(parents=True,exist_ok=False)
            failure={"item":label,"stage":session.stage_id,"failure":str(error) or type(error).__name__,
                     "exception_type":type(error).__name__,"traceback":traceback.format_exc()}
            path=session.stage_output/(label+"-failure.json")
            session.write_json(path,{**failure,"states":session.states})
            failure["evidence"]=str(path)
            failures.append(failure)
            if session.stages and session.stages[-1]["id"]==session.stage_id:
                session.stages[-1].update(status="failed",failure=failure["failure"],finished=time.monotonic())
            else:
                session.stages.append({"id":session.stage_id,"status":"failed","failure":failure["failure"],
                                       "started":time.monotonic(),"finished":time.monotonic()})
            session.publish()
        finally:
            # Clear held inputs only. Do not reset health, resources, roles,
            # modal UI, clocks or ability state to manufacture the next result.
            session.control()
            session.write_json(session.output/"sweep-failures.json",{"failures":failures,
                "policy":"Attempt all independent requested checks before gameplay fixes"})
    return failures


def role_ui(session,role,check):
    session.sync_seats()
    if next(e for e in session.clients if e["rendered"])["seat"]!=role:
        session.begin(role.lower()+"-ui-prerequisite")
        raise RuntimeError("Rendered process did not acquire the "+role+" seat; UI coverage remains unverified")
    rendered_ui(session,check)


def run(plan,args,records,logs,read_state,write_json,initial,session=None):
    session=session or AcceptanceSession(plan,args,records,logs,read_state,write_json,initial)
    session.states=initial
    try:
        if getattr(args,"regression_stage",None)=="g-remaining-sweep":
            from integrated_remaining import run as sweep
            failures=sweep(session)
            result=session.publish()
            return {"status":"failed_regression" if failures else "passed_regression","measurement_valid":False,
                    "regression_stage":args.regression_stage,"failures":failures,
                    "scenario":str(session.output/"scenario.json"),"checks":result["checks"],
                    "scope":"Remaining independent G checks collected before batch fixes; no full G/F or performance gate"}
        if getattr(args,"regression_stage",None) in ("abilities-combat","vehicle-combat","ground-combat","air-combat"):
            failures=abilities_combat(session)
            result=session.publish()
            return {"status":"failed_regression" if failures else "passed_regression","measurement_valid":False,
                    "regression_stage":args.regression_stage,"failures":failures,
                    "scenario":str(session.output/"scenario.json"),"checks":result["checks"],
                    "scope":"Only the selected regression stages and assertions with attached evidence passed. Full G/F and performance remain unverified"}
        if getattr(args,"regression_stage",None)=="ownership":
            prepare_cohort(session)
            from integrated_ownership import run_selection
            run_selection(session)
            result=session.publish()
            return {"status":"passed_regression","measurement_valid":False,"regression_stage":"ownership",
                    "scenario":str(session.output/"scenario.json"),"checks":result["checks"],
                    "scope":"Ownership combined-stage regression only; missing full gameplay/fault checks remain unverified"}
        if getattr(args,"regression_stage",None) in ("gameplay-remaining","commander-remaining"):
            prepare_cohort(session)
            remaining_gameplay(session, selection=False,repeat_logistics=args.regression_stage!="commander-remaining")
            result=session.publish()
            return {"status":"passed_regression","measurement_valid":False,"regression_stage":args.regression_stage,
                    "scenario":str(session.output/"scenario.json"),"checks":result["checks"],
                    "scope":"Remaining combined gameplay stages; does not replace full current-build G/F gates"}
        if args.case=="gameplay_merged": gameplay(session)
        else:
            from integrated_faults import run as faults
            faults(session)
        result=session.publish()
        if getattr(args, 'collect_only', False):
            return {"status":"collected", "measurement_valid":False,
                    "scenario":str(session.output/"scenario.json"), "checks":result["checks"],
                    "scope":"Independent collection only; passed, skipped and unresolved items remain distinct. No G/F gate granted."}
        if result["status"]!="passed":
            if result.get("test_items"):
                incomplete=[item["id"]+" "+item["title"] for item in result["test_items"] if item["status"]!="passed"]
                raise RuntimeError("G has incomplete test items: "+"; ".join(incomplete))
            raise RuntimeError("Scenario has unverified checks: "+", ".join(result["missing"]))
        return {"status":"passed","measurement_valid":False,"scenario":str(session.output/"scenario.json"),
                "checks":result["checks"],"test_items":result.get("test_items",[]),
                "scope":"Integrated production gameplay/fault correctness; performance is a separate gate"}
    except Exception as error:
        import traceback
        failure = str(error) or type(error).__name__
        if session.stages and session.stages[-1]["status"]=="running":
            session.stages[-1].update(status="failed",failure=failure)
        session.write_json(session.stage_output/"first-failure.json",{"failure":failure,
            "exception_type":type(error).__name__,"traceback":traceback.format_exc(),"states":session.states})
        session.publish()
        raise
