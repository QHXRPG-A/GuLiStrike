"""Shared seven-process lifetime, isolated phase identities and evidence."""
from __future__ import annotations

import copy
import json
from pathlib import Path
import time
import uuid

from acceptance_contract import required_checks, digest, SCENARIOS, COMMON_GAMEPLAY


class AcceptanceSession:
    def __init__(self, plan, args, records, logs, read_state, write_json, initial):
        self.plan, self.args, self.records, self.logs = plan, args, records, logs
        self.read_state, self.write_json = read_state, write_json
        self.output, self.states = Path(plan["output"]), initial
        self.stage_id, self.stage_number, self.stage_output = "prepare", 0, self.output
        self.checks, self.stages = {}, []
        self.case = args.case
        self.last_decision = 0
        self.decisions = {}
        self.control_sequence = 0
        self.result_cursors = {}
        self.result_observers = {}
        self.hit_cursors, self.hits = {}, {}
        self.expected_stopped = set()
        self.restarting = {}
        self.clients = [e for e, _ in records if e["name"] != "server" and e["participating"]]

    def refresh(self):
        values = {}
        for entry, process in self.records:
            if entry["name"] in self.expected_stopped:
                continue
            if process.poll() is not None:
                raise RuntimeError(f"{entry['name']} exited in stage {self.stage_id}")
            if entry["name"] in self.restarting:
                from runtime_evidence import read_json_shared
                try:
                    raw=read_json_shared(Path(entry["directory"])/"state.json")
                except (OSError,ValueError):
                    raw={}
                if raw.get("pid")!=process.pid:
                    if time.monotonic()>self.restarting[entry["name"]]: raise RuntimeError("Replacement process did not publish its own identity")
                    return False
                del self.restarting[entry["name"]]
            value = self.read_state(Path(entry["directory"]) / "state.json", self.plan["run_id"], process.pid)
            if value is None:
                return False
            values[entry["name"]] = value
        self.states = values
        for name, value in values.items():
            if name != "server":
                self.collect_decisions(value)
                self.collect_hits(value)
        return True

    def collect_decisions(self, client):
        """Record client commit evidence; the coordinator never owns gameplay results."""
        pid = client.get("pid")
        if not pid: return
        cursor = self.result_cursors.get(pid, 0)
        fresh = sorted(client.get("client_results", []), key=lambda item: item["sequence"])
        unseen = [item for item in fresh if item["sequence"] > cursor]
        if not unseen: return
        if unseen[0]["sequence"] != cursor + 1:
            raise RuntimeError("Client result evidence overflowed between observations")
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
            raise RuntimeError("Damage request evidence overflowed between observations")
        with (self.output / "damage-requests.jsonl").open("a", encoding="utf-8") as stream:
            for item in fresh:
                event = uuid.UUID(item["event"]).hex
                record = {**item, "client_pid": pid}
                self.hits.setdefault(event, record)
                stream.write(json.dumps(record, ensure_ascii=False) + "\n")
        self.hit_cursors[pid] = fresh[-1]["sequence"]

    def control(self, phase="prepare", **values):
        self.control_sequence += 1
        control = {
            "schema": 2, "run_id": self.plan["run_id"], "scenario": self.case,
            "stage_id": self.stage_id, "stage_number": self.stage_number,
            "phase": phase, **values}
        with (self.output / "inputs.jsonl").open("a", encoding="utf-8") as stream:
            stream.write(json.dumps({"sequence": self.control_sequence, "monotonic_ns": time.monotonic_ns(),
                                     "control": control}, ensure_ascii=False) + "\n")
        self.write_json(self.output / "control.json", control)

    def wait(self, label, predicate, timeout=None):
        deadline = time.monotonic() + (timeout or self.args.gameplay_timeout)
        while time.monotonic() < deadline:
            if self.refresh() and predicate():
                self.write_json(self.stage_output / (label + ".json"), self.states)
                return
            time.sleep(.15)
        self.write_json(self.stage_output / (label + "-timeout.json"), self.states)
        raise RuntimeError(f"Stage {self.stage_id} timed out: {label}")

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
        self.wait("baseline", lambda: self.operational() and all(
            self.states[e["name"]].get("stage_id") == self.stage_id for e in self.clients if e["name"] not in self.expected_stopped), timeout=45)
        self.baseline = copy.deepcopy(self.states)
        self.stages.append({"id": self.stage_id, "status": "running", "started": time.monotonic()})
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
        files = [Path(path) for path in evidence]
        self.checks[check] = {"status": "passed", "stage": self.stage_id, "detail": detail,
                              "evidence": [{"path": str(path.resolve()), "sha256": digest(path)} for path in files]}
        self.publish()

    def publish(self):
        missing = sorted(set(required_checks(self.case)) - self.checks.keys())
        value = {"schema": "guli-scenario/v1", "run_id": self.plan["run_id"], "case": self.case,
                 "baseline": self.plan.get("baseline", {}).get("identity"),
                 "status": "passed" if not missing and self.stages and all(s["status"] == "passed" for s in self.stages) else "incomplete", "checks": self.checks,
                 "missing": missing, "stages": self.stages, "measurement_valid": False}
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
        self.records[index]=(item,process)
        self.expected_stopped.discard(name)
        self.restarting[name]=time.monotonic()+self.args.startup_timeout
        return process.pid
