"""One server + six independent clients; profile only server and rendered client.

Use --dry-run to inspect the discovered CPU map and exact launch commands.
Runtime readiness and assertions come from the production HybridNetworkReview
entry. Missing or failed evidence stops the run; process survival is not a pass.
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import itertools
import json
import os
from pathlib import Path
import socket
import subprocess
import time
import uuid
from windows_cpu_topology import bind_process, discover, partition, sample_process
from runtime_evidence import read_json_shared, RuntimeEvidenceUnavailable
from acceptance_contract import SCENARIOS, freeze_baseline, validate_manifest_contract, validate_gate
ROOT = Path(__file__).resolve().parents[2]
ENGINE = Path('D:/UnrealEngine-5.7/Engine/Binaries/Win64')
MAP = '/Game/Maps/LVL_CommanderMassPrototype'

def write_json(path: Path, value: dict) -> None:
    temporary = path.with_suffix(path.suffix + '.pending')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')
    deadline = time.monotonic() + 2
    while True:
        try:
            temporary.replace(path)
            break
        except PermissionError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.01)

def read_state(path: Path, run_id: str, pid: int) -> dict | None:
    try:
        result = read_json_shared(path)
    except (FileNotFoundError, PermissionError):
        return None
    if result.get('schema') != 1 or result.get('run_id') != run_id or result.get('pid') != pid:
        raise RuntimeEvidenceUnavailable(f'Stale or incompatible runtime evidence: {path}')
    if result.get('failure'):
        raise RuntimeEvidenceUnavailable(f"{path.parent.name}: {result['failure']}")
    if result.get('scene_review',{}).get('failure'):
        raise RuntimeEvidenceUnavailable(f"{path.parent.name}: {result['scene_review']['failure']}")
    return result

def verify_additional_flights(plan: dict, states: dict) -> None:
    if plan['extra_flights'] != 500:
        return
    participants = [states[e['name']] for e in plan['processes'] if e['name'] != 'server' and e['participating']]
    if sum((s.get('flight_load', {}).get('live_additional', 0) for s in participants)) != 500:
        raise RuntimeError('Hybrid local owners do not account for 500 actual additional flights')

def verify_player_simulation(plan: dict, states: dict) -> None:
    """Player-controlled Ground/Air carriers are simulated by that same seat."""
    entries = [entry for entry in plan['processes'] if entry['name'] != 'server' and entry['participating']]
    players = {entry['player']: entry for entry in entries}
    roster = states[entries[0]['name']].get('role_members', [])
    current_roles = {member['player'].lower(): member for member in roster if member['online'] and member['role'] in ('Ground', 'Air')}
    expected = set(current_roles) if roster else {entry['player'] for entry in entries if entry['seat'] in ('Ground', 'Air')}
    directories = []
    for entry in entries:
        objects = [{**obj, 'stable_id': obj['stable_id'].lower(), 'controller': obj['controller'].lower(), 'simulation_owner': obj['simulation_owner'].lower()} for obj in states[entry['name']].get('player_objects', [])]
        directory = {obj['stable_id']: obj for obj in objects if obj['life_state'] < 2}
        active = {obj['controller'] for obj in directory.values() if not obj.get('dormant')}
        if len(directory) != 4 or len(directory) != len(objects) or active != expected:
            raise RuntimeError(f"{entry['name']}: active and dormant team carriers are not fully registered")
        for obj in directory.values():
            if obj.get('dormant'):
                if int(obj['controller'], 16) or int(obj['simulation_owner'], 16) or obj['local_simulation']:
                    raise RuntimeError(f"{entry['name']}: an empty {obj['seat']} carrier still has control/simulation authority")
                if obj.get('state_custodian', '').lower() not in players:
                    raise RuntimeError(f"{entry['name']}: dormant carrier has no participating state custodian")
                continue
            controller = players.get(obj['controller'])
            seat = current_roles.get(obj['controller'], {}).get('role', controller['seat'] if controller else None)
            if not controller or seat != obj['seat'] or obj['simulation_owner'] != obj['controller']:
                raise RuntimeError(f"{entry['name']}: {obj['seat']} simulation owner is not its controller")
            if bool(obj['local_simulation']) != (obj['controller'] == entry['player']):
                raise RuntimeError(f"{entry['name']}: {obj['seat']} native simulation runs on the wrong process")
        directories.append(directory)
    if any((set(directory) != set(directories[0]) for directory in directories[1:])):
        raise RuntimeError('Participating clients disagree on player carrier identities')
    for key in directories[0]:
        grants = {(d[key]['controller'], d[key]['simulation_owner'], d[key]['owner_epoch'], d[key]['life_generation'], d[key].get('dormant', False), d[key].get('state_custodian')) for d in directories}
        expected_simulators = 0 if directories[0][key].get('dormant') else 1
        if len(grants) != 1 or sum((d[key]['local_simulation'] for d in directories)) != expected_simulators:
            raise RuntimeError('Participating clients disagree on Ground/Air grants or simulate a carrier more than once')
        if directories[0][key].get('dormant') and sum((bool(d[key].get('local_private_custody')) for d in directories)) != 1:
            raise RuntimeError('Dormant carrier private state does not have exactly one custodian')

def build_plan(args: argparse.Namespace, cpu: dict, mode: str, formation: str, population: int, repeat: int) -> dict:
    if args.case in ('gameplay_merged', 'fault_merged'):
        # Functional acceptance shares the already allocated client CPUs.
        # Keep the service and OS reserve, total CPU pool, load and deadlines.
        # The isolated partition remains the input for performance cases.
        client_roles = ['rendered', *[f'logic{i}' for i in range(1, 6)]]
        client_mask = 0
        client_cores = []
        for role in client_roles:
            client_mask |= cpu['masks'][role]
            client_cores.extend(cpu['cores'][role])
        if client_mask & (cpu['masks']['server'] | cpu['masks']['system_reserve']):
            raise RuntimeError('Functional client pool overlaps the service or system reserve')
        cpu = {**cpu, 'policy': 'functional_shared_clients', 'original_partition': cpu,
               'masks': {**cpu['masks'], **{role: client_mask for role in client_roles}},
               'cores': {**cpu['cores'], **{role: client_cores for role in client_roles}},
               'basis': cpu['basis'] + '; functional G/F clients share their original union; service and system reserve unchanged'}
    red, blue = map(int, formation.split('v'))
    run_id = str(uuid.uuid4())
    output = Path(args.output).resolve() / f'{mode}-{formation}-n{population}-r{repeat}-{run_id[:8]}'
    room = uuid.uuid4().hex
    profile = getattr(args, 'profile', None)
    manifest = Path(args.manifest).resolve() if args.manifest else ROOT / f"Config/Hybrid/{('AcceptanceManifest' if profile else 'RuntimeManifest')}-{population}.json"
    common = ['-game', '-NoSound', '-NoSplash', '-NoLiveCoding', '-Unattended', '-NoScreenMessages', '-DisablePlugins=Fab', f'-GuLiNetMode={mode}', f'-GuLiAcceptanceRun={run_id}', f'-GuLiAcceptancePopulation={population}', f'-GuLiAcceptanceExtraFlights={(500 if population == 600 else 0)}', f'-GuLiAcceptanceFormation={formation}', f'-GuLiAcceptanceSeed={args.seed}', f'-GuLiAcceptanceCase={args.case}', f'-GuLiManifest={manifest}']
    common.extend([f'-GuLiHotspotMask={getattr(args,"hotspot_mask",31)}',
                   f'-GuLiGameplayWorkers={getattr(args,"gameplay_workers",2)}'])
    if profile:
        common.append('-CustomConfig=' + profile)
    cvars = 't.IdleWhenNotForeground 0,Slate.bAllowThrottling 0,r.VSync 0,r.ScreenPercentage 100,r.DynamicRes.OperationMode 0'
    cvars += ''.join((f',sg.{quality}Quality 3' for quality in ('ViewDistance', 'AntiAliasing', 'Shadow', 'GlobalIllumination', 'Reflection', 'PostProcess', 'Texture', 'Effects', 'Foliage', 'Shading')))
    server_dir = output / 'server'
    server_command = [str(ROOT / 'Tools/GuLiCoordinator/build/windows/Release/GuLiCoordinator.exe'), '--dev', '--manifest', str(manifest), '--bind', '127.0.0.1', '--port', str(args.coordinator_port), '--database', str(output / 'room-metadata.sqlite'), '--review-directory', str(server_dir), '--review-run', run_id, '--expected-red', str(red), '--expected-blue', str(blue)]
    processes = [{'name': 'server', 'participating': True, 'rendered': False, 'sample': True, 'mask': cpu['masks']['server'], 'command': server_command, 'directory': str(server_dir)}]
    logic_index = 0
    for index, (team, role) in enumerate(itertools.product(('Red', 'Blue'), ('Commander', 'Ground', 'Air'))):
        seat_index = index % 3
        participating = seat_index < (red if team == 'Red' else blue)
        rendered = team == 'Red' and role == args.render_role
        if rendered and (not participating):
            raise RuntimeError(f'Rendered seat {role} is absent from {formation}; use a separate 3v3 role review')
        directory = output / f'client{index + 1}-{team}-{role}'
        player = uuid.uuid5(uuid.NAMESPACE_URL, f'GuLiStrike/acceptance/{args.seed}/{team}/{role}').hex
        target = MAP
        if not participating:
            target = '/Engine/Maps/Entry?game=/Script/Engine.GameModeBase'
        if rendered:
            mask = cpu['masks']['rendered']
            visual = ['-GuLiClientRunMode=Rendered', '-GuLiPerfCapture=RenderedClient', '-windowed', '-ResX=1920', '-ResY=1080', '-ForceRes']
        else:
            logic_index += 1
            mask = cpu['masks'][f'logic{logic_index}']
            visual = ['-GuLiClientRunMode=LogicOnly', '-GuLiPerfCapture=None', '-NullRHI']
        command = [str(ENGINE / ('UnrealEditor.exe' if rendered else 'UnrealEditor-Cmd.exe')), str(ROOT / 'GuLiStrike.uproject'), target, *common, *visual, f'-GuLiAcceptanceDirectory={directory}', f'-GuLiAcceptanceIndex={index + 1}', f'-GuLiDevPlayerId={player}', f'-GuLiRoom={room}', f'-GuLiTeam={team}', f'-GuLiRole={role}', f'-GuLiUdpPort={args.udp_base + index}', '-GuLiTransport=LocalUdp', f'-GuLiCoordinatorUrl=ws://127.0.0.1:{args.coordinator_port}/v1/match', f"-abslog={directory / 'engine.log'}", f'-ExecCmds={cvars},t.MaxFPS {(0 if rendered else 60)}']
        if not participating:
            command.append('-GuLiAcceptanceStandby')
        if rendered and getattr(args, 'diagnostic_trace', False):
            command.extend(['-trace=cpu,frame,gpu,bookmark', '-statnamedevents',
                            '-tracefile='+str(directory/'frames.utrace')])
        processes.append({'name': f'client{index + 1}', 'team': team, 'seat': role, 'player': player, 'participating': participating, 'rendered': rendered, 'sample': rendered, 'mask': mask, 'command': command, 'directory': str(directory)})
    return {'schema': 1, 'run_id': run_id, 'room': room, 'mode': mode, 'transport': 'LocalUdp', 'formation': formation, 'population': population, 'extra_flights': 500 if population == 600 else 0, 'repeat': repeat, 'map': MAP, 'manifest': str(manifest), 'profile': profile, 'seed': args.seed, 'case': args.case, 'warmup_seconds': args.warmup, 'capture_seconds': args.capture, 'cpu_partition': cpu, 'output': str(output), 'processes': processes, 'performance_scope': 'Functional G/F uses a shared client CPU pool; no performance verdict' if cpu.get('policy') == 'functional_shared_clients' else 'Only server and rendered-client PIDs; never extrapolate six-client cost or double-count connection ends'}

def check_preconditions(args: argparse.Namespace, plan: dict) -> None:
    if os.name != 'nt':
        raise RuntimeError('This acceptance launcher requires Windows')
    for entry in plan['processes']:
        if not Path(entry['command'][0]).is_file():
            raise RuntimeError(f"Missing executable: {entry['command'][0]}")
    engine_id = json.loads((ENGINE / 'UnrealEditor.modules').read_text(encoding='utf-8-sig'))['BuildId']
    project_id = json.loads((ROOT / 'Binaries/Win64/UnrealEditor.modules').read_text(encoding='utf-8-sig'))['BuildId']
    if engine_id != project_id:
        raise RuntimeError('Project modules do not match the source engine')
    plan['engine_build_id'] = engine_id
    manifest_path = Path(plan['manifest'])
    if not manifest_path.is_file():
        raise RuntimeError('Export the production runtime manifest for this population before launching acceptance')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    coordinator = ROOT / 'Tools/GuLiCoordinator/build/windows/Release/GuLiCoordinator.exe'
    validation = subprocess.run([str(coordinator), '--validate-manifest', '--manifest', str(manifest_path)], cwd=ROOT, capture_output=True, text=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
    if validation.returncode:
        raise RuntimeError('Coordinator rejected the exported manifest: ' + validation.stderr.strip())
    if float(manifest.get('numbers', {}).get('acceptance/population', -1)) != plan['population']:
        raise RuntimeError('Manifest population does not match this acceptance run')
    if args.case in ('flights'):
        if plan['formation'] != '3v3' or plan['population'] != 600:
            raise RuntimeError('The focused flight cohort case requires 3v3 and 600 units')
        if 'LinearFlight' not in {value for key, value in manifest.get('strings', {}).items() if key.startswith('adapter/definition/')} or manifest.get('strings', {}).get('startup/acceptance_flight_adapter') != '2':
            raise RuntimeError('The focused flight case requires the versioned linear-flight factory')
    elif args.case in ('ground', 'ground_combat', 'ship', 'ship_wingmen', 'engineer', 'construction', 'factory_construction', 'mining'):
        coverage = set(manifest.get('strings', {}).get('startup/coverage', '').split(','))
        required = {'Mass', 'Engineer' if args.case.startswith(('engineer', 'construction', 'factory_construction')) else 'Ship' if args.case.startswith('ship') else 'Ground'}
        if not required.issubset(coverage):
            raise RuntimeError('Role review requires production startup data: ' + ', '.join(sorted(required)))
        if args.case.startswith('mining') and (plan['formation'] != '3v3' or not {'Miner', 'Factory'}.issubset(coverage)):
            raise RuntimeError('Formal mining review requires the six seats and production miner/factory adapters')
        if args.case.startswith('factory_construction') and (plan['formation'] != '3v3' or manifest.get('strings', {}).get('adapter/definition/building/6') != 'ResourceFactory' or any((float(manifest.get('numbers', {}).get(f'component/{capability}/state_version', 0)) <= 0 for capability in ('Construction', 'ResourceInventory')))):
            raise RuntimeError('Dynamic factory construction requires six seats and construction/inventory capabilities')
    elif args.case in SCENARIOS or args.case == 'steady':
        validate_manifest_contract(manifest, plan['population'])
        if plan['population'] != 600 or (args.case in SCENARIOS and plan['formation'] != '3v3'):
            raise RuntimeError('Integrated acceptance requires the approved 600 + 500 load')
        expected_rendered={"vehicle-combat":"Air","air-combat":"Air","ground-combat":"Ground"}.get(
            getattr(args,"regression_stage",None),SCENARIOS.get(args.case,(None,None,None))[2])
        if getattr(args, 'collect_groups', None):
            expected_rendered={'G08':'Ground','G09':'Air'}.get(args.collect_groups[0], 'Commander')
        if getattr(args, 'continue_remaining', False):
            expected_rendered = args.render_role
        if getattr(args, 'collect_ui_role', None):
            expected_rendered=args.collect_ui_role
        if args.case in SCENARIOS and args.render_role != expected_rendered:
            raise RuntimeError('Rendered role differs from the integrated scenario')
        plan['baseline'] = freeze_baseline(ROOT, manifest_path)
        plan['baseline']['launch_options']={'hotspot_mask':getattr(args,'hotspot_mask',31),
                                             'gameplay_workers':getattr(args,'gameplay_workers',2)}
        plan['baseline'].pop('identity')
        plan['baseline']['identity']=hashlib.sha256(json.dumps(plan['baseline'],sort_keys=True).encode()).hexdigest()
        if args.case == 'steady' or (args.case in SCENARIOS and SCENARIOS[args.case][1] == 'fault' and not getattr(args, 'collect_only', False)):
            gate_folder = getattr(args, 'gates', None)
            if not gate_folder:
                raise RuntimeError("This phase requires the current build's gameplay gate evidence")
            validate_gate(Path(gate_folder), 'gameplay', plan['baseline']['identity'])
            if args.case == 'steady':
                validate_gate(Path(gate_folder), 'fault', plan['baseline']['identity'])
    elif args.case != 'bootstrap':
        raise RuntimeError('Use the integrated fault scenarios after the gameplay gate')
    for plugin in ('GuLiFlightNavigation', 'GuLiMapAuthoring', 'UnrealMCP', 'UnrealMCPython', 'VibeUE', 'Tripo3DUEBridge-UE5.7-Win64'):
        module_path = ROOT / 'Plugins' / plugin / 'Binaries/Win64/UnrealEditor.modules'
        if json.loads(module_path.read_text(encoding='utf-8-sig'))['BuildId'] != engine_id:
            raise RuntimeError(f'Plugin {plugin} does not match the source engine')
    import ctypes
    from ctypes import wintypes
    user = ctypes.WinDLL('user32', use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.GetWindowTextLengthW.argtypes = [wintypes.HWND]
    user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    editors = []

    @callback_type
    def inspect(window, _):
        length = user.GetWindowTextLengthW(window)
        text = ctypes.create_unicode_buffer(length + 1)
        user.GetWindowTextW(window, text, length + 1)
        if user.IsWindowVisible(window) and ('Unreal Editor' in text.value or 'GuLiStrike - Unreal' in text.value):
            editors.append(text.value)
        return True
    user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user.EnumWindows(inspect, 0)
    if editors:
        raise RuntimeError('Save and close the project editor before performance capture; it was preserved')
    ports = [(args.coordinator_port, socket.SOCK_STREAM), *[(args.udp_base + i, socket.SOCK_DGRAM) for i in range(6)]]
    for port, carrier in ports:
        with socket.socket(socket.AF_INET, carrier) as probe:
            probe.bind(('127.0.0.1', port))

def launch(plan: dict, args: argparse.Namespace, writer=None) -> dict:
    write_json = writer or globals()['write_json']
    output = Path(plan['output'])
    output.mkdir(parents=True, exist_ok=False)
    records = []
    logs = []
    metrics = []
    states = {}
    result = {'run_id': plan['run_id'], 'status': 'failed', 'measurement_valid': False, 'failure': '', 'forced_cleanup': []}
    staggered = getattr(args, 'stagger_commander_registration', False)
    startup_deadline = time.monotonic() + args.startup_timeout if staggered else None
    registration_observed = False
    plan['startup_order'] = 'rendered-commander-registration-first' if staggered else 'concurrent'
    write_json(output / 'launch.json', plan)
    initial_control = {'schema': 1, 'run_id': plan['run_id'], 'phase': 'prepare'}
    if args.case in SCENARIOS or args.case == 'steady':
        initial_control.update(schema=2, scenario=args.case if args.case != 'steady' else 'performance', stage_id='prepare', stage_number=0)
    write_json(output / 'control.json', initial_control)
    try:
        for entry in plan['processes']:
            if staggered and not registration_observed and entry['name'] != 'server' and not entry['rendered']:
                server_entry, server_process = next((e, p) for e, p in records if e['name'] == 'server')
                rendered_entry, rendered_process = next((e, p) for e, p in records if e['rendered'])
                while time.monotonic() < startup_deadline:
                    if server_process.poll() is not None or rendered_process.poll() is not None:
                        raise RuntimeError('An owned process exited before Commander registration')
                    observation = read_state(Path(server_entry['directory']) / 'state.json', plan['run_id'], server_process.pid)
                    if observation and any(identity_member.get('online') and identity_member.get('seat') == 0
                            and identity_member.get('player', '').lower() == rendered_entry['player']
                            for room_state in observation.get('rooms', []) for identity_member in room_state.get('members', [])):
                        write_json(output / 'commander-registration-before-helpers.json', observation)
                        registration_observed = True
                        break
                    time.sleep(.15)
                else:
                    raise RuntimeError('Rendered Commander registration exceeded the original startup deadline')
            directory = Path(entry['directory'])
            directory.mkdir()
            stream = (directory / 'stdout.log').open('wb')
            logs.append(stream)
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 1 if entry['rendered'] else 0
            flags = subprocess.CREATE_NEW_PROCESS_GROUP | (0 if entry['rendered'] else subprocess.CREATE_NO_WINDOW)
            process = subprocess.Popen(entry['command'], cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, startupinfo=startup, creationflags=flags)
            records.append((entry, process))
            entry['observed_mask'] = bind_process(process.pid, entry['mask'])
            entry['pid'] = process.pid
            write_json(output / 'launch.json', plan)
        deadline = startup_deadline if staggered else time.monotonic() + args.startup_timeout
        states = {}
        integrated_session = None
        bootstrap_barrier = False
        if args.case in SCENARIOS:
            from acceptance_session import AcceptanceSession
            integrated_session = AcceptanceSession(plan, args, records, logs, read_state, write_json, states)
        while time.monotonic() < deadline:
            ready = True
            for entry, process in records:
                if process.poll() is not None:
                    raise RuntimeError(f"{entry['name']} exited before readiness: {process.returncode}")
                state = read_state(Path(entry['directory']) / 'state.json', plan['run_id'], process.pid)
                states[entry['name']] = state
                initialized = 'state_ready' if args.case in SCENARIOS and entry['name'] != 'server' else 'ready'
                ready &= bool(state and state.get(initialized) and (state.get('participating') == entry['participating']))
            if integrated_session:
                integrated_session.states = states
                integrated_session.enforce_affinities()
                clients = [states[e['name']] for e in integrated_session.clients]
                for state in clients:
                    if state: integrated_session.collect_decisions(state)
                if not bootstrap_barrier and clients and all((s and s.get('world_ready') and (s.get('waiting_adapters') == 0) and (s.get('pending_assignments') == 0) for s in clients)) and sum(s.get('flight_load',{}).get('live_additional',0) for s in clients)==plan['extra_flights']:
                    integrated_session.control()
                    bootstrap_barrier = True
                # The first barrier can start ownership transfers. Require
                # the actual current native flight count again at admission.
                ready &= bool(bootstrap_barrier and integrated_session.settled()
                    and sum(s.get('flight_load',{}).get('live_additional',0) for s in clients if s)==plan['extra_flights'])
            if ready:
                break
            time.sleep(0.5)
        else:
            write_json(output / 'readiness.json', states)
            raise RuntimeError('Runtime readiness timed out; missing factories, startup data or review entry are not a gameplay pass')
        if integrated_session:
            integrated_session.states = states
            integrated_session.enforce_affinities()
        write_json(output / 'readiness.json', states)
        verify_player_simulation(plan, states)
        verify_additional_flights(plan, states)
        if args.case in SCENARIOS:
            from integrated_acceptance import run
            result.update(run(plan, args, records, logs, read_state, write_json, states, integrated_session))
            return result
        if args.case == 'bootstrap':
            participants = [states[entry['name']] for entry, _ in records if entry['name'] != 'server' and entry['participating']]
            signatures = {(state.get('epoch'), state.get('directory_hash')) for state in participants}
            if len(signatures) != 1 or not participants[0].get('directory_hash'):
                raise RuntimeError('Participating clients disagree on object grants')
            manifest = json.loads(Path(plan['manifest']).read_text(encoding='utf-8-sig'))
            if manifest.get('strings', {}).get('startup/resource_ledger') == '1':
                expected_nodes = int(float(manifest['numbers']['resource_map/node_count']))
                expected_raw = sum((int(float(value)) for key, value in manifest['numbers'].items() if key.startswith('ore/') and key.endswith('/amount')))
                directories = [state.get('resource_query', {}) for state in participants]
                if any((view.get('catalog_committed') is not True or view.get('nodes') != expected_nodes or view.get('remaining_raw') != expected_raw for view in directories)):
                    raise RuntimeError('Peer ore directory is missing or differs from the formal manifest')
                if manifest.get('strings', {}).get('startup/mining_reservations') == '1' and any((view.get('mining_book_committed') is not True or view.get('mining_leases') != 0 for view in directories)):
                    raise RuntimeError('Initial mining reservation directory is missing or already occupied')
                expected_clusters = int(float(manifest['numbers']['resource_map/Clusters/count']))
                for entry, _ in records:
                    if entry['name'] == 'server' or not entry['participating']:
                        continue
                    view = states[entry['name']].get('resource_query', {})
                    if view.get('query_scene_ready') is not True or view.get('navigation_ready') is not True or view.get('collision_clusters') != expected_clusters or (view.get('enabled_clusters') != expected_clusters):
                        raise RuntimeError(f"{entry['name']}: baked collision/navigation query scene is incomplete")
                    if view.get('visual_components') != (24 if entry['rendered'] else 0):
                        raise RuntimeError(f"{entry['name']}: ore presentation does not match Rendered/LogicOnly")
            dormant = sum((bool(obj.get('dormant')) for obj in participants[0].get('player_objects', [])))
            if sum((state.get('local_objects', 0) for state in participants)) + dormant != participants[0].get('simulation_object_count', participants[0].get('object_count')):
                raise RuntimeError('Simulation ownership does not cover each registered object exactly once')
            result.update(status='passed_bootstrap', measurement_valid=False, scope='Seven-process startup, object grants and client state only; no full-gameplay or performance pass')
            return result
        if args.case in ('engineer'):
            from engineer_acceptance import run
            result.update(run(plan, args, records, read_state, write_json, states))
            return result
        if args.case in ('construction', 'factory_construction'):
            from construction_acceptance import run
            result.update(run(plan, args, records, read_state, write_json, states))
            return result
        if args.case in ('mining'):
            from mining_acceptance import run
            result.update(run(plan, args, records, read_state, write_json, states))
            return result
        if args.case in ('ground_combat'):
            from ground_combat_acceptance import run
            result.update(run(plan, args, records, read_state, write_json, states))
            return result
        if args.case in ('flights'):
            from flight_acceptance import run
            result.update(run(plan, args, records, read_state, write_json, states))
            return result
        if args.case in ('ship_wingmen'):
            from wingman_acceptance import run
            result.update(run(plan, args, records, read_state, write_json, states))
            return result
        if args.case in ('ground', 'ship'):
            prefix = 'ship' if args.case.startswith('ship') else 'ground'
            seat = 'Air' if prefix == 'ship' else 'Ground'
            check = 'ship' if prefix == 'ship' else 'ground_player'
            write_json(output / 'control.json', {'schema': 1, 'run_id': plan['run_id'], 'phase': prefix + '_movement'})
            role_endpoints = [(entry, process) for entry, process in records if entry['participating'] and entry.get('seat') == seat]
            if not role_endpoints:
                raise RuntimeError(f'Role review requires an active {seat} seat')
            participants = [entry for entry, _ in records if entry['participating'] and entry['name'] != 'server']
            initial_epochs = {entry['name']: states[entry['name']]['epoch'] for entry in participants}
            resumed_clock = None
            deadline = time.monotonic() + args.gameplay_timeout
            while time.monotonic() < deadline:
                for entry, process in records:
                    if process.poll() is not None:
                        raise RuntimeError(f"{entry['name']} exited during {seat} review")
                    states[entry['name']] = read_state(Path(entry['directory']) / 'state.json', plan['run_id'], process.pid)
                if all((states[entry['name']] and states[entry['name']].get(prefix + '_complete') for entry, _ in role_endpoints)):
                    break
                time.sleep(0.5)
            else:
                raise RuntimeError(f'Production {seat} input script did not finish')
            if any((states[entry['name']].get('checks', {}).get(check) is not True for entry, _ in role_endpoints)):
                raise RuntimeError(f'Production {seat} input assertions failed')
            verify_player_simulation(plan, states)
            clients = [states[entry['name']] for entry, _ in records if entry['name'] != 'server' and entry['participating']]
            poses = [{p['stable_id']: p for p in s.get(prefix + '_states', [])} for s in clients]
            if any((set(p) != set(poses[0]) for p in poses)) or len(poses[0]) != len(role_endpoints):
                raise RuntimeError(f'{seat} query objects are missing on a participating client')
            for key in poses[0]:
                if sum((p[key]['local_simulation'] for p in poses)) != 1:
                    raise RuntimeError(f'{seat} object does not have exactly one simulation owner')
                for p in poses[1:]:
                    tolerance = 300
                    if prefix == 'ship':
                        age = abs(p[key]['pose_tick'] - poses[0][key]['pose_tick'])
                        if age > 120:
                            raise RuntimeError('Air query motion is more than two logical seconds behind')
                        tolerance = 50 + max(p[key]['speed_limit'], poses[0][key]['speed_limit']) * age / 60
                    if sum(((a - b) ** 2 for a, b in zip(p[key]['position'], poses[0][key]['position']))) > tolerance ** 2:
                        raise RuntimeError(f'{seat} query positions disagree after accounting for their motion ticks')
            write_json(output / 'gameplay.json', states)
            scope = 'Production Air thrust, turn, velocity and boost' if prefix == 'ship' else 'Production Ground movement, rocket fuel/thrust'
            scope += ', unique owner and query positions'
            result.update(status='passed_' + args.case, measurement_valid=False, scope=scope + '' + ' only; no full-gameplay or performance pass')
            return result
        if args.case != 'steady':
            raise RuntimeError('No acceptance driver registered for ' + args.case)
        from integrated_performance import run
        result.update(run(plan, args, records, read_state, write_json, states))
    except Exception as error:
        import traceback
        result['failure'] = str(error) or type(error).__name__
        result['exception_type'] = type(error).__name__
        result['traceback'] = traceback.format_exc()
        # Preserve the failure-time publications before stop/cleanup overwrites
        # them. The launcher's startup dictionary is not a later stage snapshot.
        from runtime_evidence import read_json_shared
        observed, read_errors = {}, {}
        for entry, process in records:
            try:
                value = read_json_shared(Path(entry['directory']) / 'state.json')
                if value.get('run_id') != plan['run_id'] or value.get('pid') != process.pid:
                    raise RuntimeError('Evidence belongs to another run or process')
                observed[entry['name']] = value
            except (OSError, ValueError, RuntimeError) as capture_error:
                read_errors[entry['name']] = str(capture_error)
        write_json(output / 'failure_states.json', observed)
        write_json(output / 'failure_observation.json', {'monotonic_seconds': time.monotonic(),
                   'scope': 'Latest independent process publications before cleanup; not simultaneous logical ticks',
                   'read_errors': read_errors})
    finally:
        write_json(output / 'control.json', {'schema': 1, 'run_id': plan['run_id'], 'phase': 'stop'})
        stopped_at = time.monotonic()
        deadline = stopped_at + 60
        result['shutdown'] = {}
        while time.monotonic() < deadline and any((process.poll() is None for _, process in records)):
            for entry, process in records:
                if process.poll() is not None and entry['name'] not in result['shutdown']:
                    elapsed = time.monotonic() - stopped_at
                    result['shutdown'][entry['name']] = {'seconds': elapsed, 'exit_code': process.returncode,
                        'exceeded_previous_8s_grace': elapsed > 8, 'forced': False}
            time.sleep(0.2)
        for entry, process in records:
            if process.poll() is None:
                result['shutdown'][entry['name']] = {'seconds': time.monotonic() - stopped_at,
                    'exit_code': None, 'exceeded_previous_8s_grace': True, 'forced': True}
                process.terminate()
                result['forced_cleanup'].append(entry['name'])
            elif entry['name'] not in result['shutdown']:
                elapsed = time.monotonic() - stopped_at
                result['shutdown'][entry['name']] = {'seconds': elapsed, 'exit_code': process.returncode,
                    'exceeded_previous_8s_grace': elapsed > 8, 'forced': False}
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        for stream in logs:
            stream.close()
        with (output / 'processes.csv').open('w', newline='', encoding='utf-8') as stream:
            fields = ['role', 'pid', 'monotonic_seconds', 'seconds', 'cpu_core_equivalents', 'cpu_seconds', 'working_set_bytes', 'private_bytes']
            writer = csv.DictWriter(stream, fields)
            writer.writeheader()
            writer.writerows(metrics)
        write_json(output / 'result.json', result)
    return result

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--mode', choices=('Hybrid',), default='Hybrid')
    parser.add_argument('--formation', choices=('3v3', '2v3', '1v3', '1v1', 'all'), default='all', help='all selects 3v3/1v1 for steady performance; functional cases retain all formations')
    parser.add_argument('--population', choices=('200', '600', 'all'), default='all', help='all selects 600 units + 500 flights for steady performance; functional cases retain both loads')
    parser.add_argument('--render-role', choices=('Commander', 'Ground', 'Air'), default='Commander')
    parser.add_argument('--repeat', type=int, default=None)
    parser.add_argument('--warmup', type=float, default=30)
    parser.add_argument('--capture', type=float, default=60)
    parser.add_argument('--seed', type=int, default=20261011)
    parser.add_argument('--hotspot-mask',type=int,choices=range(512),default=31,
                        help='Candidate options latched identically by every functional client')
    parser.add_argument('--gameplay-workers',type=int,choices=(1,2,4),default=2)
    parser.add_argument('--case', choices=('bootstrap', 'ground', 'ground_combat', 'ship', 'ship_wingmen', 'engineer', 'construction', 'factory_construction', 'mining', 'flights', 'steady', *SCENARIOS), default='gameplay_merged')
    parser.add_argument('--gates', help='Directory containing current-build gameplay/fault evidence')
    parser.add_argument('--collect-only', action='store_true', help='Collect independent G/F evidence without granting or requiring a complete G gate')
    parser.add_argument('--continue-remaining', action='store_true', help='Collect remaining G03/G08/G09/G10 as one bounded task, continuing unfinished work in replacement rooms')
    parser.add_argument('--resume-summary', help='Select only unresolved branches from a prior remaining-summary.json; never inherit its pass evidence')
    parser.add_argument('--collect-groups', nargs='+', choices=tuple('G'+str(i).zfill(2) for i in range(1,11)), help='Collect selected existing G groups in a fresh six-client room')
    parser.add_argument('--collect-steps', nargs='+', help='Resume only named existing G steps after shared evidence was interrupted')
    parser.add_argument('--collect-fault-steps', nargs='+', help='Collect named existing F phases in a fresh room after shared evidence was interrupted')
    parser.add_argument('--collect-ui-role', choices=('Commander','Ground','Air'), help='Collect one existing pending-request UI branch using its initially rendered fixed seat')
    parser.add_argument('--preparation-timeout', type=float, default=45, help='Short budget for setup; unresolved preparation is skipped, never passed')
    parser.add_argument('--etw-helper')
    parser.add_argument('--etw-calibration')
    parser.add_argument('--diagnostic-trace', action='store_true', help='CPU/GPU frame diagnosis on the rendered PID only; not a valid performance round')
    parser.add_argument('--stagger-commander-registration', action='store_true',
                        help='Wait for the rendered Commander to register before launching helpers, within the original startup deadline')
    parser.add_argument('--regression-stage', choices=('ownership','gameplay-remaining','commander-remaining','abilities-combat','vehicle-combat','ground-combat','air-combat','g-remaining-sweep'), help='Run the affected combined stages plus real preparation; never grants a full gameplay gate')
    parser.add_argument('--startup-timeout', type=float, default=180)
    parser.add_argument('--gameplay-timeout', type=float, default=300)
    parser.add_argument('--manifest', default=None)
    parser.add_argument('--profile', choices=('HybridAcceptance',), default='HybridAcceptance')
    parser.add_argument('--output', default=str(ROOT / 'TestResults/HybridAcceptance'))
    parser.add_argument('--coordinator-port', type=int, default=8787)
    parser.add_argument('--udp-base', type=int, default=18801)
    args = parser.parse_args()
    if args.resume_summary and (not args.continue_remaining or not args.collect_only):
        parser.error('--resume-summary requires --continue-remaining --collect-only')
    if args.continue_remaining:
        if (not args.collect_only or args.case != 'gameplay_merged' or args.regression_stage
                or args.collect_steps or args.collect_fault_steps or args.collect_ui_role
                or args.stagger_commander_registration
                or (args.collect_groups and set(args.collect_groups) != {'G03', 'G08', 'G09', 'G10'})
                or args.formation not in ('all', '3v3') or args.population not in ('all', '600')
                or args.repeat not in (None, 1)):
            parser.error('--continue-remaining requires the four remaining G groups, --collect-only, six seats and 600 units; step filters and repeated matrices are not supported')
        args.collect_groups = ['G03', 'G08', 'G09', 'G10']
        args.render_role = 'Air' if args.resume_summary else 'Ground'
    if args.collect_only and (args.case not in SCENARIOS or args.regression_stage):
        parser.error('Independent collection is restricted to the existing G/F scenarios')
    if args.collect_groups and (not args.collect_only or args.case != 'gameplay_merged'):
        parser.error('Selecting G groups requires gameplay_merged --collect-only')
    if args.collect_steps and (not args.collect_only or not args.collect_groups):
        parser.error('Selecting steps requires explicit G collection groups')
    if args.collect_fault_steps and (not args.collect_only or args.case != 'fault_merged'):
        parser.error('Selecting F phases requires fault_merged --collect-only')
    if args.collect_ui_role and (not args.collect_only or args.case != 'fault_merged' or args.collect_fault_steps != ['ui-rebind']):
        parser.error('An individual UI role requires only the existing ui-rebind F collection phase')
    if args.preparation_timeout <= 0:
        parser.error('Invalid preparation budget')
    if args.diagnostic_trace and args.case == 'steady':
        parser.error('A diagnostic full trace is separate from the final performance matrix')
    if args.regression_stage and args.case != 'gameplay_merged':
        parser.error('A targeted regression requires gameplay_merged and cannot run as a performance or fault gate')
    if args.stagger_commander_registration and args.render_role != 'Commander':
        parser.error('Staggered Commander registration requires the Commander rendered seat')
    if args.repeat is None:
        args.repeat = 3 if args.case == 'steady' else 1
    if args.repeat < 1 or args.warmup < 0 or args.capture <= 0 or (args.startup_timeout <= 0) or (args.gameplay_timeout <= 0):
        parser.error('Invalid repeat or duration')
    if not all((1 <= p <= 65535 for p in (args.coordinator_port, args.udp_base, args.udp_base + 5))):
        parser.error('Invalid port range')
    if not Path(args.output).resolve().is_relative_to(ROOT):
        parser.error('Evidence must remain inside the project workspace')
    modes = ('Hybrid',)
    formation_scope = ('3v3', '1v1') if args.case == 'steady' else ('3v3',)
    population_scope = (600,) if args.case == 'steady' or args.case in SCENARIOS else (200, 600)
    formations = formation_scope if args.formation == 'all' else (args.formation,)
    populations = population_scope if args.population == 'all' else (int(args.population),)
    cpu = partition(discover())
    if args.continue_remaining:
        from integrated_continuation import execute
        return execute(args, cpu)
    results = []
    for mode, formation, population, repeat in itertools.product(modes, formations, populations, range(1, args.repeat + 1)):
        plan = build_plan(args, cpu, mode, formation, population, repeat)
        plan['collection']={'enabled':args.collect_only,'groups':args.collect_groups,
                            'steps':args.collect_steps,
                            'fault_steps':args.collect_fault_steps,'ui_role':args.collect_ui_role,
                            'preparation_timeout':args.preparation_timeout,
                            'policy':'Existing preparation cheats with readback; short-term unresolved setup is skipped. No collection run grants a G/F gate.'}
        if args.dry_run:
            print(json.dumps(plan, ensure_ascii=False, indent=2))
            continue
        check_preconditions(args, plan)
        result = launch(plan, args)
        results.append(result)
        print(json.dumps({'output': plan['output'], **result}, ensure_ascii=False), flush=True)
        if result['status'] not in ('passed', 'collected', 'passed_regression', 'passed_bootstrap', 'passed_ground', 'passed_ground_combat', 'passed_ship', 'passed_ship_wingmen', 'passed_engineer', 'passed_construction', 'passed_factory_construction', 'passed_mining', 'passed_flights'):
            return 1
    return 0
if __name__ == '__main__':
    raise SystemExit(main())
