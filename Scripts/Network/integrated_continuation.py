"""One bounded collection task for the existing remaining G branches.

Rooms never share live state or pass flags. Only immutable evidence and the
queue of unattempted actions survive a failed room. This cannot issue a gate.
"""
from __future__ import annotations

import copy
import gzip
import json
from pathlib import Path
import re
import subprocess

from acceptance_contract import GAMEPLAY_ITEMS, digest
from acceptance_session import PreparationSkipped
from runtime_evidence import RuntimeEvidenceUnavailable

ROOT = Path(__file__).resolve().parents[2]
GROUPS = ("G03", "G08", "G09", "G10")
REQUIRED = {key: tuple(checks) for key, _, checks in GAMEPLAY_ITEMS if key in GROUPS}
# The ordinary building and engineer branches already had independent results;
# this task covers the two population branches listed as still outstanding.
REQUIRED["G03"] = ("replacement_population", "population_cancel_reject_death")
STEPS = (
    ("G03", "construction-site"), ("G03", "placement-cancel-reject"),
    ("G03", "population-headroom"), ("G03", "construction-8"),
    ("G08", "ground-ui"), ("G08", "ground-spatial-preparation"),
    ("G08", "ground-movement"), ("G08", "ground-combat"),
    ("G09", "rendered-air-seat"), ("G09", "air-ui"),
    # Let real member births progress while preparing the already-required
    # generic hosts. Its actual production/inventory verdicts remain later.
    ("G10", "generic-production-hosts"),
    ("G09", "air-equipment-motion"), ("G09", "air-member-attack"),
    ("G09", "member-ground-attack"),
    ("G10", "generic-production"),
    ("G10", "generic-inventory-unit-101"), ("G10", "generic-inventory-unit-102"),
    ("G10", "generic-inventory-building-102"), ("G10", "generic-card-submit"),
    ("G10", "generic-content"), ("G10", "no-receive-echo"),
    ("G10", "fixed-seat-admission"),
)


class CompactEvidenceWriter:
    """Losslessly compress full snapshots; controls and small receipts stay JSON."""
    def __init__(self, writer):
        self.writer = writer

    @staticmethod
    def evidence_path(path):
        path = Path(path)
        compressed = path.with_name(path.name + ".gz")
        return compressed if compressed.is_file() else path

    def __call__(self, path, value):
        path = Path(path)
        snapshots = value.get("states", value) if isinstance(value, dict) else {}
        large = isinstance(snapshots, dict) and any(
            isinstance(v, dict) and "pid" in v and (k == "server" or k.startswith("client"))
            for k, v in snapshots.items())
        if not large or path.name in ("control.json", "launch.json", "scenario.json"):
            self.writer(path, value)
            return
        target = path.with_name(path.name + ".gz")
        temporary = target.with_name(target.name + ".pending")
        # dumps uses the C encoder; streaming hundreds of thousands of tiny
        # writes through TextIO/gzip was consuming the setup observation budget.
        payload=json.dumps(value,ensure_ascii=False,separators=(",",":")).encode('utf-8')
        temporary.write_bytes(gzip.compress(payload,compresslevel=1,mtime=0))
        temporary.replace(target)


def prepare_manifest(folder, source, write_json):
    """Rebuild a small, reproducible fixture from the surviving formal catalog."""
    manifest = json.loads(source.read_text(encoding="utf-8-sig"))
    original = copy.deepcopy(manifest)
    weak = sorted((o for o in manifest["objects"] if o["definition"] == "unit/1" and o["team"] == 1),
                  key=lambda o: o["ordinal"])[:25]
    for obj in weak:
        obj["health"] = "10"
    for obj in manifest["objects"]:
        if obj["definition"] == "player/ground" and obj["team"] == 2:
            obj["health"] = "10"
        if obj["definition"] == "player/ship":
            # Keep the authored separated staging positions until equipment
            # readback completes; native displacement prepares combat later.
            obj["health"] = "100000"
    manifest["strings"]["acceptance/preparation"] = "gf-fast-spatial-v2"
    target = folder / "AcceptanceManifest-600-prepared.json"
    write_json(target, manifest)
    coordinator = ROOT / "Tools/GuLiCoordinator/build/windows/Release/GuLiCoordinator.exe"
    validations = []
    for _ in range(2):
        result = subprocess.run([str(coordinator), "--validate-manifest", "--manifest", str(target)],
                                cwd=ROOT, capture_output=True, text=True, timeout=10,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        validations.append({"exit_code": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
        if result.returncode == 0:
            break
        hashes = re.search(r"configuration expected=[0-9a-f]+ actual=([0-9a-f]{64}); map expected=[0-9a-f]+ actual=([0-9a-f]{64})", result.stderr)
        if not hashes:
            raise RuntimeError("Prepared manifest rejected: "+result.stderr.strip())
        manifest["config_hash"], manifest["map_hash"] = hashes.groups()
        write_json(target, manifest)
    else:
        raise RuntimeError("Prepared manifest did not pass the native validator")
    changes = []
    originals = {o["id"]: o for o in original["objects"]}
    for obj in manifest["objects"]:
        old = originals.get(obj["id"])
        changes.append({"object": obj["id"], "definition": obj["definition"],
                        "changes": {k: {"before": old.get(k) if old else None, "after": v}
                                    for k, v in obj.items() if old is None or old.get(k) != v}})
    write_json(folder / "preparation-readback.json", {
        "source": str(source), "source_sha256": digest(source), "prepared": str(target),
        "prepared_sha256": digest(target), "changes": [c for c in changes if c["changes"]],
        "preparation_selector": manifest["strings"]["acceptance/preparation"], "validation": validations,
        "scope": "Target health preparation; authored separated Ship staging poses retained. Other positions use native fixture inputs. Prerequisite buildings use real placement and authorized work preparation. No injected population, births, damage, power, cargo or final receipts."})
    return target


def prepare_hosts(session, admitted):
    from integrated_commander import construct, prepare_construction_site, fund
    manifest=json.loads(Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
    # Resource top-up has its own existing short setup window. Do not spend a
    # placement window on a second, nested resource preparation first.
    fund(session,{color:max(int(float(manifest['numbers'][f'building/{d}/Cost/{color.capitalize()}']))
                            for d in (4,101,102))+128 for color in ('blue','red')})
    prepare_construction_site(session)
    for definition, offset in ((4,(0,1200)), (101,(0,-3600)), (102,(3600,-2400))):
        with session.preparation('generic prerequisite building '+str(definition)):
            construct(session, definition, offset, admitted)


def prime_equipment(session):
    """Submit actual Hangar choices early; allow native births during other work."""
    from integrated_acceptance import entry,input_and_wait
    with session.preparation('Air assembly request preparation'):
        session.begin('air-assembly-request-preparation')
        records=[]
        for team in ('Red','Blue'):
            pilot=entry(session,team,'Air')
            choice=next((c for c in session.states[pilot['name']]['scene_ui'].get('ship_choices',[])
                         if c['node']=='08' and c['available']),None)
            if choice is None: raise PreparationSkipped('The implemented Hangar choice is unavailable for '+team)
            reply,path=input_and_wait(session,'ship_equipment',pilot['player'],node=choice['node'])
            if not reply['result'].get('committed'): raise PreparationSkipped('Hangar preparation did not commit for '+team)
            records.append({'player':pilot['player'],'node':choice['node'],'reply':reply,
                            'evidence':str(session.write_json.evidence_path(session.stage_output/path))})
        session.prepared_equipment=records
        session.write_json(session.stage_output/'assembly-preparation-receipts.json',{'records':records,
            'scope':'Real assembly choices and final receipts only; no members or lifecycle outcomes are injected.'})
        session.finish()


def generic_card(session):
    """Only the real data-card commit required by generic content, without G06's rendered flip test."""
    from integrated_acceptance import entry, input_and_wait
    from integrated_commander import prepare_card_entry, new_decisions
    with session.preparation('generic data-card offer and commit'):
        session.begin('generic-data-card-commit')
        commander=entry(session,'Red','Commander')
        prepare_card_entry(session,commander)
        session.decision_start=session.last_decision
        opened,open_path=input_and_wait(session,'rogue_open',commander['player'])
        for index in range(13):
            cards=session.states[commander['name']]['scene_ui'].get('cards',[])
            if 'QA.101' in cards: break
            if index==12: raise PreparationSkipped('The data-only card was not offered within the existing reroll limit')
            input_and_wait(session,'rogue_reroll',commander['player'])
        confirmed,confirm_path=input_and_wait(session,'rogue_confirm',commander['player'],index=cards.index('QA.101'))
        session.wait('generic-card-committed',lambda:any(d.get('status')==0 and d.get('capability')=='army_state'
                     for d in new_decisions(session)))
        session.wait('generic-card-returned',lambda:not session.states[commander['name']]['scene_ui'].get('rogue_open')
                     and not session.states[commander['name']]['scene_ui'].get('rogue_pending'))
        session.write_json(session.stage_output/'generic-card-commit.json',{
            'card':'QA.101','offer':opened,'offer_input':open_path,'confirmation':confirmed,'confirmation_input':confirm_path,
            'commits':[d for d in new_decisions(session) if d.get('status')==0 and d.get('capability')=='army_state'],
            'scope':'Existing logical Commander offer/confirm/final receipt; no rendered G06 flip or UI coverage is claimed.'})
        session.variant_card_committed=True
        session.finish()


def run_session(session):
    if getattr(session.args, 'resume_summary', None):
        from integrated_remaining_closure import run_session as run_closure
        return run_closure(session)
    from integrated_remaining import attempt, require, PrerequisiteMissing
    from integrated_acceptance import prepare_cohort, role_ui, movement, ground_combat, no_echo, air_equipment, objects
    from integrated_commander import (prepare_construction_site, placement_cancel_and_reject, prepare_population,
                                      construct, production, inventory_combination, authored_closure)
    from population_combat_preparation import prepare_ground_duel
    from integrated_roles import switch_rendered, fixed_reconnect
    from integrated_air import run as air_attack, ground_attack
    failures, outcomes, admitted = [], [], {}
    pending = list(session.args.collect_steps or [label for _, label in STEPS])
    functions = {
        "construction-site": prepare_construction_site, "placement-cancel-reject": placement_cancel_and_reject,
        "population-headroom": lambda s: prepare_population(s, 25),
        "construction-8": lambda s: construct(s, 8, (-5000, 0), admitted),
        "ground-ui": lambda s: role_ui(s, "Ground", "ground_ui"),
        "ground-spatial-preparation": prepare_ground_duel,
        "ground-movement": lambda s: movement(s, "ground"), "ground-combat": ground_combat,
        "rendered-air-seat": lambda s: switch_rendered(s, "Air"),
        "air-ui": lambda s: role_ui(s, "Air", "air_ui"), "air-equipment-motion": air_equipment,
        "air-member-attack": air_attack, "member-ground-attack": ground_attack,
        "generic-production-hosts": lambda s: prepare_hosts(s, admitted),
        "generic-production": require(lambda s: all(d in admitted for d in (4, 101)),
            "Production requires this room's completed prepared hosts", lambda s: production(s, admitted)),
        "generic-card-submit": generic_card,
        "generic-content": lambda s: authored_closure(s, "generic_content"),
        "no-receive-echo": no_echo, "fixed-seat-admission": fixed_reconnect,
    }
    for definition in ("unit/101", "unit/102", "building/102"):
        functions["generic-inventory-"+definition.replace("/", "-")] = require(
            lambda s, d=definition: any(o["definition"] == d and o["team"] == 1 and o["life"] == 1 for o in objects(s, True)),
            "Inventory needs an actual living carrier: "+definition,
            lambda s, d=definition: inventory_combination(s, d))
    checks = tuple(check for group in GROUPS for check in REQUIRED[group])
    session.configure_test_items((("remaining", "剩余玩法联合回归", checks),))
    session.begin_test_item("remaining")
    blocked_kind = 'shared_runtime'

    def save(blocked=None):
        session.write_json(session.output/"remaining-progress.json", {
            "title": "剩余玩法联合回归", "pending": pending, "outcomes": outcomes,
            "blocked": blocked, "blocked_kind": blocked_kind, "checks": session.checks, "failures": failures,
            "run_id": session.plan["run_id"], "baseline": session.plan.get("baseline", {}).get("identity")})

    save()
    try:
        attempt(session, "load-preparation", prepare_cohort, failures)
        if failures:
            blocked_kind = failures[-1]['failure_kind']
            save('Actual 600 + 500 load preparation did not complete')
            session.finish_test_item(blocked=True)
            return failures
        attempt(session,'air-assembly-preparation',prime_equipment,failures)
        if 'generic-content' in pending and 'population-headroom' not in pending:
            # Recovered rooms do not inherit the old room's freed population.
            # Do this before Ground/Air combat can kill the real Blue shooter.
            attempt(session,'generic-population-preparation',lambda s:prepare_population(s,25),failures)
        # A recovered room rebuilds prerequisite state locally. No pass flags
        # or object identities from an old room are inserted into this session.
        if "construction-8" in pending and "placement-cancel-reject" not in pending:
            with session.preparation("population prerequisites in the replacement room"):
                placement_cancel_and_reject(session)
                prepare_population(session, 8)
        if "generic-content" in pending:
            prerequisites = [label for group, label in STEPS if group == 'G10'
                             and label not in ('generic-content', 'no-receive-echo', 'fixed-seat-admission')]
            pending = [label for _, label in STEPS if label in set(pending).union(prerequisites)]
        if any(label in pending for label in ('air-member-attack','member-ground-attack')):
            pending = [label for _, label in STEPS if label in set(pending).union(
                ('air-equipment-motion','air-member-attack'))]
        for group, label in STEPS:
            if label not in pending:
                continue
            save()
            before = len(failures)
            blocked = None
            print(json.dumps({'task':'剩余玩法联合回归','trace_group':group,'step':label,
                              'run_id':session.plan['run_id']},ensure_ascii=False),flush=True)
            try:
                attempt(session, label, functions[label], failures)
            except RuntimeEvidenceUnavailable as error:
                blocked = str(error)
            failure = failures[-1] if len(failures) != before else None
            outcome = {"group": group, "step": label, "status": failure["status"] if failure else "passed",
                       "stage": session.stage_id, "failure": failure}
            outcomes.append(outcome)
            if not blocked:
                pending.remove(label)
            save(blocked)
            if blocked:
                session.finish_test_item(blocked=True)
                return failures
        session.finish_test_item()
        save()
        return failures
    except (RuntimeEvidenceUnavailable, PreparationSkipped, PrerequisiteMissing) as error:
        if isinstance(error,(PreparationSkipped,PrerequisiteMissing)):
            blocked_kind = 'preparation'
        save(str(error))
        session.finish_test_item(blocked=True)
        return failures


def execute(args, cpu):
    from run_hybrid_acceptance import build_plan, check_preconditions, launch, write_json
    from acceptance_contract import freeze_baseline
    steps = STEPS
    required = tuple(check for group in GROUPS for check in REQUIRED[group])
    selection = None
    if getattr(args, 'resume_summary', None):
        from integrated_remaining_closure import select_remaining
        selection = select_remaining(Path(args.resume_summary).resolve())
        steps, required = selection['steps'], selection['required_checks']
        args.remaining_selection = selection
    all_labels = [label for _, label in steps]
    if args.dry_run:
        preview = copy.copy(args)
        preview.render_role = "Air" if selection else "Ground"
        plan = build_plan(preview, cpu, "Hybrid", "3v3", 600, 1)
        print(json.dumps({"title": "剩余玩法联合回归", "steps": list(steps), "selection": selection, "initial_plan": plan,
                          "recovery": "At most one replacement per rendered-role segment; unfinished steps only; no gate"}, ensure_ascii=False))
        return 0
    folder = Path(args.output).resolve()
    folder.mkdir(parents=True, exist_ok=False)
    source = Path(args.manifest).resolve() if args.manifest else ROOT/"Config/Hybrid/AcceptanceManifest-600.json"
    manifest = prepare_manifest(folder, source, write_json)
    pending, rooms, recovered, abandoned = all_labels[:], [], set(), []
    baseline = None
    startup_failure = None
    while pending:
        group = next(group for group, label in steps if label == pending[0])
        role = "Ground" if group in ("G03", "G08") else "Air"
        room_args = copy.copy(args)
        room_args.manifest, room_args.render_role = str(manifest), role
        room_args.collect_steps = pending[:]
        plan = build_plan(room_args, cpu, "Hybrid", "3v3", 600, len(rooms)+1)
        plan["collection"] = {"enabled": True, "continue_remaining": True, "steps": pending[:],
                              "title": "剩余玩法联合回归", "groups": list(GROUPS), "selection": selection}
        check_preconditions(room_args, plan)
        if baseline is not None and plan["baseline"]["identity"] != baseline:
            raise RuntimeError("The frozen baseline changed between continuation rooms")
        baseline = plan["baseline"]["identity"]
        writer = CompactEvidenceWriter(write_json)
        result = launch(plan, room_args, writer=writer)
        progress_path = Path(plan["output"])/"remaining-progress.json"
        progress = json.loads(progress_path.read_text(encoding="utf-8")) if progress_path.is_file() else {
            "pending": pending[:], "outcomes": [], "checks": {}, "blocked_kind": "preparation",
            "blocked": result.get("failure") or "Startup evidence unavailable"}
        rooms.append({"output": plan["output"], "run_id": plan["run_id"], "baseline": baseline,
                      "initial_render_role": role, "result": result, **progress})
        pending = progress["pending"]
        if not progress_path.is_file():
            signature = re.sub(r'id=[0-9a-f]+|client[1-6]|[0-9]{4,}', '', progress['blocked'])
            if startup_failure == signature:
                abandoned.extend({'step':label,'status':'not_executed',
                    'reason':'The same shared startup preparation failed twice; no valid room exists',
                    'after_room':plan['run_id']} for label in pending)
                pending = []
            startup_failure = signature
        if pending:
            next_group = next(group for group, label in steps if label == pending[0])
            next_role = "Ground" if next_group in ("G03", "G08") else "Air"
            if next_role in recovered:
                skipped = [label for group, label in steps if label in pending
                           and ("Ground" if group in ("G03", "G08") else "Air") == next_role]
                abandoned.extend({"step": label, "status": "not_executed", "reason": "The rendered-role segment exhausted its one automatic recovery", "after_room": plan["run_id"]} for label in skipped)
                pending = [label for label in pending if label not in skipped]
            else:
                recovered.add(next_role)
        summary = summarize(rooms, abandoned, pending, baseline, required)
        summary['selection'] = selection
        write_json(folder/"remaining-summary.json", summary)
        print(json.dumps({"title": summary["title"], "rooms": len(rooms), "pending_steps": len(pending),
                          "status": summary["status"], "summary": str(folder/"remaining-summary.json")}, ensure_ascii=False), flush=True)
    summary = summarize(rooms, abandoned, [], baseline, required)
    summary['selection'] = selection
    summary['baseline_unchanged']=freeze_baseline(ROOT,manifest)['identity']==baseline
    if not summary['baseline_unchanged']:
        summary['status']='incomplete_collection'
    write_json(folder/"remaining-summary.json", summary)
    return 0 if summary["status"] == "passed_collection" else 1


def summarize(rooms, abandoned, pending, baseline, required=None):
    checks, failures, outcomes, corrupt = {}, [], [], []
    for room in rooms:
        for label, record in room.get("checks", {}).items():
            checks[label] = {**record, "run_id": room["run_id"]}
        outcomes.extend({**outcome, "run_id": room["run_id"]} for outcome in room.get("outcomes", []))
        failures.extend({**failure, "run_id": room["run_id"]} for failure in room.get("failures", []))
        if room.get("blocked"):
            failures.append({"run_id": room["run_id"], "failure_kind": room.get('blocked_kind','shared_runtime'), "failure": room["blocked"]})
    for label, record in checks.items():
        for evidence in record.get("evidence", []):
            path = Path(evidence["path"])
            if not path.is_file() or digest(path) != evidence["sha256"]:
                corrupt.append({"check": label, "path": str(path)})
    for failure in failures:
        if failure.get('evidence_sha256'):
            path=Path(failure['evidence'])
            if not path.is_file() or digest(path)!=failure['evidence_sha256']:
                corrupt.append({'failure':failure.get('item'),'path':str(path)})
    required = tuple(required) if required is not None else tuple(check for group in GROUPS for check in REQUIRED[group])
    missing = [check for check in required if check not in checks]
    passed = not pending and not missing and not failures and not abandoned and not corrupt
    return {"title": "剩余玩法联合回归", "status": "passed_collection" if passed else "collecting" if pending else "incomplete_collection",
            "baseline": baseline, "requested_checks": required, "scope": "Existing remaining G03/G08/G09/G10 branches only; cross-room collection is supplemental and cannot grant a complete G/F gate",
            "rooms": rooms, "checks": checks, "missing_checks": missing, "outcomes": outcomes,
            "failures": failures, "not_executed": abandoned, "pending": pending,
            "evidence_hashes_valid": not corrupt, "evidence_errors": corrupt, "measurement_valid": False}
