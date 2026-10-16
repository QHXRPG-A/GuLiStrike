"""Shared seven-process lifetime, isolated phase identities and evidence."""
from __future__ import annotations

import copy
from contextlib import contextmanager
from functools import wraps
import json
from pathlib import Path
import time
import uuid

from acceptance_contract import required_checks, digest, SCENARIOS, COMMON_GAMEPLAY


class AcceptanceTimeout(RuntimeError):
    """An observation window expired; this alone is not a gameplay defect."""


class PreparationSkipped(RuntimeError):
    """The authorized setup could not be completed within its short budget."""


def preparation(execute):
    @wraps(execute)
    def wrapped(session, *args, **kwargs):
        with session.preparation(execute.__name__):
            return execute(session, *args, **kwargs)
    return wrapped


class AcceptanceSession:
    def __init__(self, plan, args, records, logs, read_state, write_json, initial):
        self.plan, self.args, self.records, self.logs = plan, args, records, logs
        self.read_state, self.write_json = read_state, write_json
        self.output, self.states = Path(plan["output"]), initial
        self.stage_id, self.stage_number, self.stage_output = "prepare", 0, self.output
        self.checks, self.stages = {}, []
        self.test_items, self.active_test_item = {}, None
        self.case = args.case
        self.last_decision = 0
        self.decisions = {}
        self.control_sequence = 0
        self.result_cursors = {}
        self.result_observers = {}
        self.hit_cursors, self.hits = {}, {}
        self.expected_stopped = set()
        self.restarting = {}
        self.preparation_deadline = None
        self.affinity_observed = set()
        self.clients = [e for e, _ in records if e["name"] != "server" and e["participating"]]

    def enforce_affinities(self):
        from windows_cpu_topology import bind_process, process_affinity
        for item, process in self.records:
            value = self.states.get(item['name'])
            if not value or value.get('pid') != process.pid or process.poll() is not None:
                continue
            before = process_affinity(process.pid)
            actual = bind_process(process.pid, item['mask']) if before != item['mask'] else before
            if process.pid not in self.affinity_observed or before != actual:
                with (self.output / 'affinity-readbacks.jsonl').open('a', encoding='utf-8') as stream:
                    stream.write(json.dumps({'run_id': self.plan['run_id'], 'name': item['name'],
                        'pid': process.pid, 'stage': self.stage_id, 'monotonic_ns': time.monotonic_ns(),
                        'expected_mask': item['mask'], 'before_mask': before, 'observed_mask': actual,
                        'scope': 'Owned process after its first native publication; startup/restart or subsequent drift corrected and read back'}) + '\n')
                self.affinity_observed.add(process.pid)

    def refresh(self):
        values = {}
        complete = True
        for entry, process in self.records:
            if entry["name"] in self.expected_stopped and process.poll() is not None:
                continue
            if process.poll() is not None:
                from runtime_evidence import RuntimeEvidenceUnavailable
                raise RuntimeEvidenceUnavailable(f"{entry['name']} exited in stage {self.stage_id}")
            if entry["name"] in self.restarting:
                from runtime_evidence import read_json_shared
                try:
                    raw=read_json_shared(Path(entry["directory"])/"state.json")
                except (OSError,ValueError):
                    raw={}
                if raw.get("pid")!=process.pid:
                    if time.monotonic()>self.restarting[entry["name"]]: raise RuntimeError("Replacement process did not publish its own identity")
                    complete = False
                    continue
                del self.restarting[entry["name"]]
            value = self.read_state(Path(entry["directory"]) / "state.json", self.plan["run_id"], process.pid)
            if value is None:
                complete = False
                continue
            values[entry["name"]] = value
        self.states = values
        self.enforce_affinities()
        for name, value in values.items():
            if name != "server":
                self.collect_decisions(value)
                self.collect_hits(value)
        # A replacement can spend seconds loading its world. The other five
        # clients still commit work, so drain their bounded evidence streams
        # even while the complete-room predicate must continue waiting.
        return complete

    def collect_decisions(self, client):
        """Record client commit evidence; the coordinator never owns gameplay results."""
        pid = client.get("pid")
        if not pid: return
        cursor = self.result_cursors.get(pid, 0)
        fresh = sorted(client.get("client_results", []), key=lambda item: item["sequence"])
        unseen = [item for item in fresh if item["sequence"] > cursor]
        if not unseen: return
        if unseen[0]["sequence"] != cursor + 1:
            from runtime_evidence import RuntimeEvidenceUnavailable
            raise RuntimeEvidenceUnavailable("Client result evidence overflowed between observations")
        with (self.output / "client-results.jsonl").open("a", encoding="utf-8") as stream:
            for item in unseen:
                event = uuid.UUID(item["event"]).hex
                previous = self.decisions.get(event)
                # The wire hash also contains Committed/Duplicate delivery
                # status. The decision hash preserves every settlement field.
                if previous and previous["decision_hash"] != item["decision_hash"]:
                    self.write_json(self.output / "divergent-result.json", {
                        "event": event, "previous": previous, "received": item, "client_pid": pid})
                    raise RuntimeError("The same event has divergent client commit results: " + event)
                stream.write(json.dumps({**item, "client_pid": pid}, ensure_ascii=False) + "\n")
                self.result_observers.setdefault(event, set()).add(pid)
                if not previous:
                    self.last_decision += 1
                    self.decisions[event] = {**item, "sequence": self.last_decision,
                        "delivery_status": item["status"], "status": 0 if item["status"] == 1 else item["status"]}
        self.result_cursors[pid] = unseen[-1]["sequence"]

    def collect_hits(self, client):
        """Link geometry reports to the submitting PID and immutable result identity."""
        pid = client.get("pid")
        if not pid: return
        cursor = self.hit_cursors.get(pid, 0)
        fresh = sorted((v for v in client.get("damage_requests", []) if v["sequence"] > cursor),
                       key=lambda v: v["sequence"])
        if not fresh: return
        if fresh[0]["sequence"] != cursor + 1:
            from runtime_evidence import RuntimeEvidenceUnavailable
            raise RuntimeEvidenceUnavailable("Damage request evidence overflowed between observations")
        with (self.output / "damage-requests.jsonl").open("a", encoding="utf-8") as stream:
            for item in fresh:
                event = uuid.UUID(item["event"]).hex
                record = {**item, "client_pid": pid}
                self.hits.setdefault(event, record)
                stream.write(json.dumps(record, ensure_ascii=False) + "\n")
        self.hit_cursors[pid] = fresh[-1]["sequence"]

    def observed_by_active(self, event):
        active = {s['pid'] for name, s in self.states.items() if name != 'server'}
        return bool(active) and active.issubset(self.result_observers.get(event, set()))

    def control(self, phase="prepare", **values):
        self.control_sequence += 1
        control = {
            "schema": 2, "run_id": self.plan["run_id"], "scenario": self.case,
            "stage_id": self.stage_id, "stage_number": self.stage_number,
            "phase": phase, **values}
        if self.active_test_item:
            control["test_item_id"] = self.active_test_item
        with (self.output / "inputs.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps({"sequence": self.control_sequence, "monotonic_ns": time.monotonic_ns(),
                                     "control": control}, ensure_ascii=False) + "\n")
        self.write_json(self.output / "control.json", control)

    def wait(self, label, predicate, timeout=None, progress=None):
        window = timeout or self.args.gameplay_timeout
        started = time.monotonic()
        deadline = started + window
        stalled_deadline, best, noted = deadline, None, False
        while time.monotonic() < (min(stalled_deadline, started + window * 4) if progress else deadline):
            if self.preparation_deadline is not None and time.monotonic() >= self.preparation_deadline:
                target = self.stage_output / (label + "-preparation-skipped.json")
                if len(str(target.resolve())) + len('.pending') >= 260:
                    target = self.stage_output / ('preparation-skipped-' + uuid.uuid5(uuid.NAMESPACE_URL, label).hex[:16] + '.json')
                self.write_json(target, self.states)
                raise PreparationSkipped("Preparation budget expired: " + label)
            fresh_complete=self.refresh()
            if fresh_complete and predicate():
                self.write_json(self.stage_output / (label + ".json"), self.states)
                return
            if progress and fresh_complete:
                value = progress()
                if value is not None and (best is None or value > best):
                    best, stalled_deadline = value, time.monotonic() + window
                if not noted and time.monotonic() >= deadline:
                    self.write_json(self.stage_output / (label + "-timing-observation.json"), {
                        "wall_seconds": time.monotonic() - started, "progress": best,
                        "classification": "timing_observation", "gameplay_defect": False,
                        "policy": "G functional completion may continue while real construction progresses; F timing limits are unchanged"})
                    noted = True
            time.sleep(.15)
        self.write_json(self.stage_output / (label + "-timeout.json"), self.states)
        raise AcceptanceTimeout(f"Stage {self.stage_id} observation timed out: {label}")

    @contextmanager
    def preparation(self, label):
        previous = self.preparation_deadline
        deadline = time.monotonic() + getattr(self.args, "preparation_timeout", 45)
        self.preparation_deadline = min(previous, deadline) if previous is not None else deadline
        try:
            yield
        except PreparationSkipped:
            raise
        except Exception as error:
            from runtime_evidence import RuntimeEvidenceUnavailable
            if isinstance(error, RuntimeEvidenceUnavailable):
                raise
            raise PreparationSkipped(label + ": " + (str(error) or type(error).__name__)) from error
        finally:
            self.preparation_deadline = previous

    def settled(self):
        from runtime_evidence import ownership_views_agree
        clients = [self.states[e["name"]] for e in self.clients if e["name"] not in self.expected_stopped]
        if not clients or any(s is None for s in clients): return False
        return (all(s.get("state_ready") and not s.get("paused") for s in clients)
                and all(not any(o.get("handoff_pending") for o in s.get("objects", [])) for s in clients)
                and ownership_views_agree(clients))

    def operational(self):
        """Independent stages require a running room, not idle unrelated objects.

        Each assertion checks its own cohort, receipts and authority versions.
        Strict directory/handoff convergence remains explicit in membership and
        ownership tests; it cannot block unrelated UI, combat or resource input.
        """
        clients = [self.states[e['name']] for e in self.clients if e['name'] not in self.expected_stopped]
        return bool(clients) and all(s and s.get('state_ready') and not s.get('paused')
                                    and not s.get('failure') for s in clients)

    def sync_seats(self):
        """Process identity is stable; gameplay authority follows the current roster."""
        view = next((self.states[e['name']] for e in self.clients if e['name'] in self.states), {})
        roles = {uuid.UUID(m['player']).hex: m['role'] for m in view.get('role_members', []) if m['online']}
        for e in self.clients:
            e.setdefault('launch_seat', e['seat'])
            current=roles.get(e['player'])
            if current and current!=e['launch_seat']:
                raise RuntimeError('Fixed match role changed: '+e['name'])

    def begin(self, label):
        self.stage_number += 1
        self.stage_id = f"{self.stage_number:02d}-{label}"
        self.stage_output = self.output / "stages" / self.stage_id
        self.stage_output.mkdir(parents=True, exist_ok=False)
        self.control()
        try:
            self.wait("baseline", lambda: self.operational() and all(
                self.states[e["name"]].get("stage_id") == self.stage_id for e in self.clients if e["name"] not in self.expected_stopped), timeout=45)
        except RuntimeError as error:
            from runtime_evidence import RuntimeEvidenceUnavailable
            if isinstance(error, (RuntimeEvidenceUnavailable, PreparationSkipped)): raise
            raise RuntimeEvidenceUnavailable("Shared room baseline unavailable before " + self.stage_id + ": " + str(error)) from error
        self.baseline = copy.deepcopy(self.states)
        self.stages.append({"id": self.stage_id, "status": "running", "started": time.monotonic(),
                            "test_item": self.active_test_item})
        self.publish()

    def configure_test_items(self, items):
        self.test_items = {key: {"id": key, "title": title, "required_checks": list(checks),
                                "status": "not_executed", "stages": []}
                           for key, title, checks in items}
        self.publish()

    def begin_test_item(self, key):
        if self.active_test_item or key not in self.test_items:
            raise RuntimeError("Invalid or nested gameplay test item: " + key)
        self.active_test_item = key
        self.test_items[key].update(status="running", started=time.monotonic())
        self.publish()

    def finish_test_item(self, blocked=False):
        key = self.active_test_item
        self.test_items[key].update(status="blocked" if blocked else "complete", finished=time.monotonic())
        self.active_test_item = None
        self.publish()

    def finish(self):
        self.seal_evidence("settled")
        self.stages[-1].update(status="passed", finished=time.monotonic())
        self.publish()

    def seal_evidence(self, label, **values):
        """Record each endpoint after stage inputs; assertions own convergence checks."""
        self.control(**values)
        self.wait(label, self.operational, timeout=45)
        return self.states

    def prove(self, check, condition, detail, evidence):
        known = set(COMMON_GAMEPLAY).union(*(set(v[3]) for v in SCENARIOS.values()))
        if check not in known:
            raise RuntimeError("Undeclared acceptance assertion: " + check)
        if not condition:
            raise RuntimeError(f"{check}: {detail}")
        if not evidence:
            raise RuntimeError("An assertion requires observed evidence: " + check)
        resolve = getattr(self.write_json, 'evidence_path', Path)
        files = [resolve(path) for path in evidence]
        self.checks[check] = {"status": "passed", "stage": self.stage_id, "detail": detail,
                              "evidence": [{"path": str(path.resolve()), "sha256": digest(path)} for path in files]}
        self.publish()

    def publish(self):
        missing = sorted(set(required_checks(self.case)) - self.checks.keys())
        items = []
        for key, record in self.test_items.items():
            stages = [stage for stage in self.stages if stage.get("test_item") == key]
            absent = [check for check in record["required_checks"] if check not in self.checks]
            status = record["status"]
            if status == "complete":
                status = "passed" if stages and not absent and all(stage["status"] == "passed" for stage in stages) else "failed"
            items.append({**record, "status": status, "missing_checks": absent,
                          "passed_checks": len(record["required_checks"]) - len(absent),
                          "stages": [stage["id"] for stage in stages]})
        items_passed = not items or all(item["status"] == "passed" for item in items)
        value = {"schema": "guli-scenario/v1", "run_id": self.plan["run_id"], "case": self.case,
                 "baseline": self.plan.get("baseline", {}).get("identity"),
                 "status": "passed" if not missing and items_passed and self.stages and all(s["status"] == "passed" for s in self.stages) else "incomplete", "checks": self.checks,
                 "missing": missing, "stages": self.stages, "measurement_valid": False}
        if items:
            value["test_items"] = items
            value["test_item_summary"] = {"total": len(items), "passed": sum(item["status"] == "passed" for item in items)}
        self.write_json(self.output / "scenario.json", value)
        return value

    def legacy(self, module, case, arguments=None, **kwargs):
        """Reuse real-input drivers without letting them own process lifetime."""
        import importlib
        self.sync_seats()
        args = copy.copy(self.args)
        args.case = case
        args.command_base = self.stage_number * 100000
        args.seal_evidence = self.seal_evidence
        for key, value in (arguments or {}).items():
            setattr(args, key, value)
        plan = {**self.plan, "case": case, "output": str(self.stage_output)}

        def emit(path, value):
            if path.name == "control.json":
                self.control(**{key: item for key, item in value.items() if key not in ("schema", "run_id")})
            else:
                self.write_json(path, value)

        def observe(path, run_id, pid):
            state = self.read_state(path, run_id, pid)
            if state and path.parent != Path(self.plan["processes"][0]["directory"]):
                self.collect_decisions(state)
                self.collect_hits(state)
            return state

        result = importlib.import_module(module).run(plan, args, self.records, observe,
                                                    emit, self.states, **kwargs)
        self.write_json(self.stage_output / "driver-result.json", result)
        self.refresh()
        return result

    def operation(self, kind, player, **values):
        operation = uuid.uuid4().hex
        self.control("gameplay", actions=[{"id": operation, "kind": kind, "player": player, **values}])
        return operation

    def action(self, operation):
        found = [item for state in self.states.values() for item in state.get("review_actions", [])
                 if uuid.UUID(item["id"]) == uuid.UUID(operation) and item.get("status") != "queued"]
        if any(item.get("status") == "rejected" for item in found):
            raise RuntimeError("Formal review action rejected: " + json.dumps(found))
        return found[0] if found else None

    def stop_owned(self, name):
        item,process=next((e,p) for e,p in self.records if e['name']==name)
        self.expected_stopped.add(name)
        process.terminate()
        process.wait(timeout=10)
        return process.pid

    def restart_owned(self, name, rendered=None):
        import subprocess
        from windows_cpu_topology import bind_process
        index=next(i for i,(e,p) in enumerate(self.records) if e['name']==name)
        item,previous=self.records[index]
        if previous.poll() is None: raise RuntimeError('Restart requires an already stopped owned process')
        if rendered is not None:
            command=[a for a in item['command'] if not a.lower().startswith(('-guliclientrunmode=', '-guliperfcapture=', '-resx=', '-resy=', '-trace', '-statnamedevents'))
                     and a.lower() not in ('-nullrhi','-windowed','-forceres')]
            command[0]=str(Path(command[0]).with_name('UnrealEditor.exe' if rendered else 'UnrealEditor-Cmd.exe'))
            command += (['-GuLiClientRunMode=Rendered','-GuLiPerfCapture=RenderedClient','-windowed','-ResX=1920','-ResY=1080','-ForceRes']
                        if rendered else ['-GuLiClientRunMode=LogicOnly','-GuLiPerfCapture=None','-NullRHI'])
            import re
            command=[re.sub(r't.MaxFPS \d+', 't.MaxFPS '+('0' if rendered else '60'), a) if a.startswith('-ExecCmds=') else a for a in command]
            item.update(command=command,rendered=rendered,sample=rendered)
        stream=(Path(item['directory'])/('stdout-restart-'+str(previous.pid)+'.log')).open('wb')
        self.logs.append(stream)
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
        command=[('-abslog='+str(Path(item['directory'])/('engine-restart-'+str(previous.pid)+'.log')))
                 if arg.lower().startswith('-abslog=') else arg for arg in item['command']]
        if item.get('rendered'): startup.wShowWindow=1
        item['command']=command
        process=subprocess.Popen(command,cwd=Path(__file__).resolve().parents[2],stdout=stream,stderr=subprocess.STDOUT,
            startupinfo=startup,creationflags=subprocess.CREATE_NEW_PROCESS_GROUP|subprocess.CREATE_NO_WINDOW)
        bind_process(process.pid,item['mask'])
        item['pid'] = process.pid
        self.records[index]=(item,process)
        self.write_json(self.output / 'launch.json', self.plan)
        self.expected_stopped.discard(name)
        self.restarting[name]=time.monotonic()+self.args.startup_timeout
        return process.pid
