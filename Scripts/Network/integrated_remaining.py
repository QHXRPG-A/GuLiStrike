"""Collect remaining G branches in one real room before changing gameplay."""
import time
import traceback
from runtime_evidence import RuntimeEvidenceUnavailable


def attempt(session, label, execute, failures):
    previous = session.stage_id
    try:
        execute(session)
    except Exception as error:
        if session.stage_id == previous:
            session.stage_number += 1
            session.stage_id = f"{session.stage_number:02d}-{label}-prerequisite"
            session.stage_output = session.output / "stages" / session.stage_id
            session.stage_output.mkdir(parents=True, exist_ok=False)
        failure = {"item": label, "stage": session.stage_id, "failure": str(error) or type(error).__name__,
                   "exception_type": type(error).__name__, "traceback": traceback.format_exc()}
        path = session.stage_output / (label + "-failure.json")
        session.write_json(path, {**failure, "states": session.states})
        failure["evidence"] = str(path)
        failures.append(failure)
        if session.stages and session.stages[-1]["id"] == session.stage_id:
            session.stages[-1].update(status="failed", failure=failure["failure"], finished=time.monotonic())
        else:
            session.stages.append({"id": session.stage_id, "status": "failed", "failure": failure["failure"],
                                   "started": time.monotonic(), "finished": time.monotonic()})
        session.publish()
        if isinstance(error, RuntimeEvidenceUnavailable):
            # Continue independent gameplay failures, but not without the shared
            # state required to judge every subsequent input and result.
            raise
    finally:
        session.control()
        session.write_json(session.output / "sweep-failures.json", {
            "failures": failures, "policy": "Attempt independent remaining G branches before batch gameplay fixes"})


def run(session):
    from integrated_acceptance import common, air_equipment, no_echo, role_ui, ground_combat, movement
    from integrated_commander import (formation, prepare_construction_site, placement_cancel_and_reject,
        prepare_population, construct, production, inventory_combination, teleport_landing, teleport_cancel,
        card_cancel, card_selection, battle, authored_closure)
    from integrated_air import run as air_attack, ground_attack
    from integrated_supply import capture, unpaid, resume
    from integrated_roles import switch_rendered, fixed_reconnect
    from integrated_ownership import run_selection, resource_group
    failures=[]
    admitted={}
    phases=[("common-startup",common),("resource-group",resource_group),("commander-ui",lambda s:role_ui(s,"Commander","commander_ui")),("selection-priority",run_selection),
            ("air-equipment-motion",air_equipment),("air-member-attack",air_attack),("member-ground-attack",ground_attack),
            ("unpaid-production",unpaid),("construction-site",prepare_construction_site),
            ("placement-cancel-reject",placement_cancel_and_reject),
            ("population-headroom",lambda s:prepare_population(s,25))]
    for definition,offset in zip((1,8,4,101,102,5),((1200,0),(-1200,0),(0,1200),(0,-1200),(3600,-2400),(-1200,-1100))):
        phases.append((f"construction-{definition}",lambda s,d=definition,o=offset:construct(s,d,o,admitted)))
    phases.extend([("production",lambda s:production(s,admitted)),("supply-resume",resume),("territory-capture",capture)])
    for definition in ("unit/101","unit/102","building/102"):
        phases.append(("inventory-"+definition.replace("/","-"),lambda s,d=definition:inventory_combination(s,d)))
    phases.extend([("formation-and-skills",formation),("teleport-landing",teleport_landing),("card-cancel-return",card_cancel),
                   ("teleport-cancel-return",teleport_cancel),("card-submit-return",card_selection),
                   ("mass-wm01-summon-battle",battle)])
    for check in ("generic_content","production_blueprints","production_blueprint_execution"):
        phases.append((check,lambda s,c=check:authored_closure(s,c)))
    phases.extend([("no-receive-echo",no_echo),("fixed-seat-admission",fixed_reconnect),
                   # Known reconnect defects must not prevent combat/economy observations.
                   ("rendered-air-seat",lambda s:switch_rendered(s,"Air")),
                   ("air-ui",lambda s:role_ui(s,"Air","air_ui")),
                   ("map-entry",lambda s:authored_closure(s,"map_entry")),
                   ("rendered-ground-seat",lambda s:switch_rendered(s,"Ground")),
                   ("ground-ui",lambda s:role_ui(s,"Ground","ground_ui")),("ground-movement",lambda s:movement(s,"ground")),("ground-combat",ground_combat)])
    session.write_json(session.output/'planned-sweep.json',{'items':[label for label,_ in phases],
        'ground_combat':'Run native Ground attack again on the current build; prior evidence is historical',
        'preparation':'Only explicit resource credits and manifest health fixture; no state or population injection'})
    for index,(label,execute) in enumerate(phases):
        try:
            attempt(session,label,execute,failures)
        except RuntimeEvidenceUnavailable as error:
            session.write_json(session.output/'blocked-by-evidence.json',{
                'after':label,'remaining':[name for name,_ in phases[index+1:]],
                'reason':str(error)})
            break
        # An explicit room stop invalidates every remaining stage. Preserve the
        # first failure instead of manufacturing a failure for each dead PID.
        if not any(p.poll() is None for e,p in session.records if e['name']!='server'):
            session.write_json(session.output/'unattempted-after-room-stop.json',{
                'after':label,'remaining':[name for name,_ in phases[index+1:]],
                'reason':'The owned room processes have stopped; dependent stages were not executed.'})
            break
    return failures
