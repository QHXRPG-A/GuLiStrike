"""Emit read-only Wingman runtime summaries for the active PIE world."""

import json
import os
import time

import unreal


editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_game_world()
if world is None:
    raise RuntimeError("PIE world is not ready")

for command in (
    "gs.Wingman.Stats",
    "gs.Wingman.RelayStats",
    "gs.Wingman.LeaseWatchdog",
):
    unreal.SystemLibrary.execute_console_command(world, command)

unreal.MCPythonHelper.submit_result(
    json.dumps({"success": True, "world": str(world.get_path_name())})
)

# Opt-in temporal diagnosis on the existing two-client PIE sampler. Compare
# distance / world delta, not just nonzero movement or an absolute step size.
if globals().get("WINGMAN_MOTION_CAPTURE_PATH"):
    _motion_path = str(WINGMAN_MOTION_CAPTURE_PATH)
    _motion_duration = float(globals().get("WINGMAN_MOTION_CAPTURE_SECONDS", 20.0))
    _motion_start = time.perf_counter()
    _motion_previous = {}
    _motion_stats = {}
    _motion_events = []
    _motion_stalls = []
    _motion_frames = 0
    _motion_errors = []
    _motion_identity_changes = 0
    _motion_network = []
    _motion_cadence = {}
    _motion_network_next = 0

    def _xyz(v):
        return [float(v.x), float(v.y), float(v.z)]

    def _distance(a, b):
        return sum((a[i] - b[i]) ** 2 for i in range(3)) ** 0.5

    def _motion_tick(_delta):
        global _motion_frames, _motion_identity_changes, _motion_network_next
        _motion_frames += 1
        try:
            presenters = {p.get_path_name().split(':')[0]: p
                for p in unreal.ObjectIterator(unreal.GuLiWingmanPresentationActor)
                if 'UEDPIE_' in p.get_path_name()}
            for pawn in unreal.ObjectIterator(unreal.GuLiWingmanPawn):
                path = str(pawn.get_path_name())
                if 'UEDPIE_' not in path:
                    continue
                mesh = pawn.get_component_by_class(unreal.StaticMeshComponent)
                if pawn.get_editor_property('hidden') or not mesh:
                    hidden_previous = _motion_previous.pop(path, None)
                    hidden_stats = _motion_stats.get(path)
                    if hidden_previous and hidden_stats and hidden_stats['current_stall_seconds'] > 0.2:
                        _motion_stalls.append({'pawn': path, 'seconds': hidden_stats['current_stall_seconds'],
                            'ended_by': 'hidden', 'last': hidden_previous})
                        hidden_stats['current_stall_seconds'] = 0
                    continue
                root = pawn.get_editor_property('root_component')
                role = 'owner' if root.get_collision_enabled() != unreal.CollisionEnabled.NO_COLLISION else 'remote'
                now = float(unreal.GameplayStatics.get_time_seconds(pawn))
                handle = pawn.get_wingman_handle()
                identity = handle.export_text()
                presenter = presenters.get(path.split(':')[0])
                timing = presenter.get_presentation_timing(handle) if presenter and role == 'remote' else None
                state = {'t': now, 'root': _xyz(pawn.get_actor_location()),
                         'model': _xyz(mesh.get_world_location()), 'role': role,
                         'identity': identity, 'timing': timing}
                if timing:
                    stream_key = path.split(':')[0] + ':' + identity
                    cadence = _motion_cadence.setdefault(stream_key, {'samples': 0,
                        'last_source': timing[1], 'last_receipt': now,
                        'receipt_interval_sum': 0, 'max_receipt_gap': 0, 'max_source_gap': 0})
                    if timing[1] > cadence['last_source']:
                        interval = now - cadence['last_receipt']
                        cadence['samples'] += 1
                        cadence['receipt_interval_sum'] += interval
                        cadence['max_receipt_gap'] = max(cadence['max_receipt_gap'], interval)
                        cadence['max_source_gap'] = max(cadence['max_source_gap'], timing[1] - cadence['last_source'])
                        cadence['last_source'] = timing[1]
                        cadence['last_receipt'] = now
                previous = _motion_previous.get(path)
                _motion_previous[path] = state
                if previous and previous['identity'] != identity:
                    _motion_identity_changes += 1
                    if path in _motion_stats:
                        _motion_stats[path]['current_stall_seconds'] = 0
                    continue
                if not previous or previous['role'] != role or now <= previous['t']:
                    continue
                dt = now - previous['t']
                stats = _motion_stats.setdefault(path, {'role': role, 'frames': 0, 'max_dt': 0,
                    'root_max_cm_s': 0, 'model_max_cm_s': 0, 'model_zero_frames': 0,
                    'model_over_200m_s': 0, 'root_over_200m_s': 0,
                    'current_stall_seconds': 0, 'longest_stall_seconds': 0})
                stats['frames'] += 1
                stats['max_dt'] = max(stats['max_dt'], dt)
                for field in ('root', 'model'):
                    distance = _distance(state[field], previous[field])
                    speed = distance / dt
                    stats[field + '_max_cm_s'] = max(stats[field + '_max_cm_s'], speed)
                    if speed > 20000:
                        stats[field + '_over_200m_s'] += 1
                        if field == 'model':
                            _motion_events.append({'pawn': path, 'dt': dt, 'speed': speed,
                                'before': previous, 'after': state})
                    if field == 'model' and distance < 0.01:
                        stats['model_zero_frames'] += 1
                        stats['current_stall_seconds'] += dt
                        stats['longest_stall_seconds'] = max(stats['longest_stall_seconds'], stats['current_stall_seconds'])
                    elif field == 'model':
                        if stats['current_stall_seconds'] > 0.2:
                            _motion_stalls.append({'pawn': path, 'seconds': stats['current_stall_seconds'],
                                'ended_by': 'movement', 'before': previous, 'after': state})
                        stats['current_stall_seconds'] = 0
            if globals().get('WINGMAN_NETWORK_CAPTURE') and time.perf_counter() >= _motion_network_next:
                _motion_network_next = time.perf_counter() + 0.25
                unreal.SystemLibrary.execute_console_command(world, 'gs.WingmanAttack.QA.Sample')
                with open('D:/UE5.7/test1/TestResults/WingmanAttack/pie-sample.json', encoding='utf-8-sig') as stream:
                    worlds = json.load(stream)
                _motion_network.append([{k: w[k] for k in ('world', 'time', 'connections', 'relays')} for w in worlds])
        except Exception as exc:
            _motion_errors.append(str(exc))
        if time.perf_counter() - _motion_start >= _motion_duration:
            unreal.unregister_slate_post_tick_callback(_motion_callback)
            os.makedirs(os.path.dirname(_motion_path), exist_ok=True)
            report = {'seconds': time.perf_counter() - _motion_start,
                      'frames': _motion_frames, 'tracks': _motion_stats,
                      'identity_changes': _motion_identity_changes,
                      'network': _motion_network,
                      'cadence': _motion_cadence,
                      'events': sorted(_motion_events, key=lambda e: e['speed'], reverse=True)[:100],
                      'stalls': _motion_stalls,
                      'errors': _motion_errors}
            with open(_motion_path, 'w', encoding='utf-8') as stream:
                json.dump(report, stream, indent=2)
            unreal.log('Wingman motion capture: ' + _motion_path)

    _motion_callback = unreal.register_slate_post_tick_callback(_motion_tick)
