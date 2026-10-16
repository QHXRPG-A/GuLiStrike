"""Collect remaining G branches in one real room before changing gameplay."""
import time
import traceback
from pathlib import Path
from runtime_evidence import RuntimeEvidenceUnavailable
from acceptance_contract import GAMEPLAY_ITEMS
from acceptance_session import AcceptanceTimeout, PreparationSkipped


class PrerequisiteMissing(RuntimeError):
    """A dependent branch has no real object/input fixture to test yet."""


def require(condition, description, execute):
    def run(session):
        if not condition(session):
            raise PrerequisiteMissing(description)
        execute(session)
    return run


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
        status = "skipped" if isinstance(error, PreparationSkipped) else "not_executed" if isinstance(error, PrerequisiteMissing) else "failed"
        failure = {"item": label, "test_item": session.active_test_item, "stage": session.stage_id,
                   "status": status, "failure": str(error) or type(error).__name__,
                   "failure_kind": "preparation" if isinstance(error, PreparationSkipped) else
                        "shared_runtime" if isinstance(error, RuntimeEvidenceUnavailable) else
                        "observation_timeout" if isinstance(error, AcceptanceTimeout) else
                       "prerequisite" if isinstance(error, PrerequisiteMissing) else
                       "preparation" if any(v in session.stage_id for v in ('development-resource-supply','combat-population-preparation')) else "gameplay",
                   "exception_type": type(error).__name__, "traceback": traceback.format_exc()}
        path = session.stage_output / (label + "-failure.json")
        observed, read_errors = {}, {}
        if isinstance(error, RuntimeEvidenceUnavailable):
            from runtime_evidence import read_json_shared
            for endpoint, process in session.records:
                try:
                    value=read_json_shared(Path(endpoint['directory'])/'state.json')
                    if value.get('run_id')!=session.plan['run_id'] or value.get('pid')!=process.pid:
                        raise RuntimeError('Publication belongs to another run or PID')
                    observed[endpoint['name']]=value
                except (OSError,ValueError,RuntimeError) as capture_error:
                    read_errors[endpoint['name']]=str(capture_error)
        session.write_json(path, {**failure, "states": {} if isinstance(error, PrerequisiteMissing) else observed or session.states,
                                  "upstream_preparation": getattr(session, 'shared_inventory_failure', None) if isinstance(error, PrerequisiteMissing) else None,
                                  "native_read_errors":read_errors})
        failure["evidence"] = str(getattr(session.write_json, 'evidence_path', lambda p:p)(path))
        from acceptance_contract import digest
        failure['evidence_sha256']=digest(Path(failure['evidence']))
        failures.append(failure)
        if session.stages and session.stages[-1]["id"] == session.stage_id:
            session.stages[-1].update(status=status, test_item=session.active_test_item,
                                      failure=failure["failure"], finished=time.monotonic())
        else:
            session.stages.append({"id": session.stage_id, "status": status, "test_item": session.active_test_item,
                                   "failure": failure["failure"],
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
    if getattr(session.args, 'continue_remaining', False):
        from integrated_continuation import run_session
        return run_session(session)
    from integrated_acceptance import (common, air_equipment, no_echo, role_ui, objects,
                                      ground_combat, movement, engineering, mining_transport)
    from integrated_commander import (formation, prepare_construction_site, placement_cancel_and_reject,
        prepare_population, construct, production, inventory_combination, teleport_landing, teleport_cancel,
        card_cancel, card_selection, battle, authored_closure)
    from integrated_air import run as air_attack, ground_attack
    from integrated_supply import capture, unpaid, resume
    from integrated_roles import switch_rendered, fixed_reconnect
    from integrated_ownership import run_selection, resource_group
    failures=[]
    admitted={}
    phases={
        "G01": [("common-startup",common),("map-entry",lambda s:authored_closure(s,"map_entry"))],
        "G02": [("resource-group",resource_group),("commander-ui",lambda s:role_ui(s,"Commander","commander_ui")),
                ("selection-priority",run_selection)],
        "G03": [("unpaid-production-baseline",unpaid),("construction-site",prepare_construction_site),
                ("placement-cancel-reject",placement_cancel_and_reject),
                ("population-headroom",lambda s:prepare_population(s,2))],
        "G04": [],
        "G05": [("supply-resume",require(lambda s:bool(getattr(s,"supply_fixture",None)),
                    "Paid recovery requires the recorded unpaid facility",resume)),
                ("mining-transport",mining_transport),("territory-capture",capture)],
        "G06": [("rendered-commander-seat",lambda s:switch_rendered(s,"Commander")),
                ("formation-and-skills",formation),("teleport-landing",teleport_landing),
                ("card-cancel-return",card_cancel),("teleport-cancel-return",teleport_cancel),
                ("card-submit-return",card_selection)],
        "G07": [("mass-wm01-summon-battle",battle)],
        "G08": [("rendered-ground-seat",lambda s:switch_rendered(s,"Ground")),
                ("ground-ui",lambda s:role_ui(s,"Ground","ground_ui")),
                ("ground-movement",lambda s:movement(s,"ground")),("ground-combat",ground_combat)],
        "G09": [("rendered-air-seat",lambda s:switch_rendered(s,"Air")),
                ("air-equipment-motion",air_equipment),("air-member-attack",air_attack),
                ("member-ground-attack",ground_attack),("air-ui",lambda s:role_ui(s,"Air","air_ui"))],
        "G10": [("generic-content",lambda s:authored_closure(s,"generic_content")),
                ("no-receive-echo",no_echo),("fixed-seat-admission",fixed_reconnect)],
    }
    for definition,offset in zip((1,8,4,101,102,5),((1200,0),(-1200,0),(0,1200),(0,-1200),(3600,-2400),(-1200,-1100))):
        phases['G03' if definition in (1,8) else 'G04'].append((f"construction-{definition}",lambda s,d=definition,o=offset:construct(s,d,o,admitted)))
    phases["G03"].append(("engineering",engineering))
    phases["G04"].append(("production",require(lambda s:all(d in admitted for d in (4,101)),
                            "Production requires the two admitted factories (4 and 101)",lambda s:production(s,admitted))))
    for definition in ("unit/101","unit/102","building/102"):
        phases["G04"].append(("inventory-"+definition.replace("/","-"),require(
            lambda s,d=definition:any(o['definition']==d and o['team']==1 and o['life']==1 for o in objects(s,True)),
            "Inventory combination requires a real living carrier: "+definition,
            lambda s,d=definition:inventory_combination(s,d))))
    for check in ("production_blueprints","production_blueprint_execution"):
        phases["G04"].append((check,lambda s,c=check:authored_closure(s,c)))
    # Collect the two other rendered roles before relocation can suspend the
    # shared room. Roles stay fixed; only their owned processes are restarted.
    execution_order=("G01","G02","G03","G05","G04","G06","G07","G08","G09","G10")
    requested=getattr(session.args, 'collect_groups', None)
    if requested:
        execution_order=tuple(key for key in execution_order if key in requested)
    selected_steps=getattr(session.args, 'collect_steps', None)
    if selected_steps:
        available={label for key in execution_order for label,_ in phases[key]}
        if set(selected_steps)-available:
            raise RuntimeError('Unknown existing G steps: '+str(sorted(set(selected_steps)-available)))
        phases={key:[step for step in steps if step[0] in selected_steps] for key,steps in phases.items()}
    session.configure_test_items(GAMEPLAY_ITEMS)
    if requested and 'G01' not in requested:
        from integrated_acceptance import prepare_cohort
        attempt(session, 'load-preparation', prepare_cohort, failures)
    session.write_json(session.output/'planned-sweep.json',{'items':[
        {'id':key,'title':title,'assertions':list(checks),'steps':[name for name,_ in phases[key]]}
        for key,title,checks in GAMEPLAY_ITEMS], 'execution_order':list(execution_order),
        'ground_combat':'Run native Ground attack again on the current build; prior evidence is historical',
        'preparation':'Explicit resource credits, manifest target health and user-authorized development positions/action locks with readback; population, damage, production and power assertions require actual gameplay'})
    for group_index,key in enumerate(execution_order):
        session.begin_test_item(key)
        try:
            for step_index,(label,execute) in enumerate(phases[key]):
                attempt(session,label,execute,failures)
        except RuntimeEvidenceUnavailable as error:
            session.finish_test_item(blocked=True)
            session.write_json(session.output/'blocked-by-evidence.json',{
                'after':label,'test_item':key,'remaining_steps':[name for name,_ in phases[key][step_index+1:]],
                'remaining_items':list(execution_order[group_index+1:]),
                'reason':str(error)})
            break
        else:
            session.finish_test_item()
        # An explicit room stop invalidates every remaining stage. Preserve the
        # first failure instead of manufacturing a failure for each dead PID.
        if not any(p.poll() is None for e,p in session.records if e['name']!='server'):
            session.write_json(session.output/'unattempted-after-room-stop.json',{
                'after':key,'remaining_items':list(execution_order[group_index+1:]),
                'reason':'The owned room processes have stopped; dependent stages were not executed.'})
            break
    return failures
