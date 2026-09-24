import json
import math
import os
import time
from pathlib import Path
import unreal

if not getattr(unreal, '_codex_nav_diagnostic_bridge', None):
    exec(compile(Path('D:/UE5.7/test1/Artifacts/MassNavRepair20260924/current-stuck/session_bridge.py').read_text(encoding='utf-8'), 'session_bridge.py', 'exec'), {})

out = Path('D:/UE5.7/test1/Artifacts/MassNavRepair20260924/current-stuck')
ed = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
server = ed.get_game_world()
assert server, 'No PIE world in this editor instance'
report = {'captured_at': time.time(), 'pid': os.getpid(), 'game_world': server.get_path_name(), 'clients': [], 'authorities': []}
for w in unreal.ObjectIterator(unreal.World):
    if 'UEDPIE_' not in w.get_path_name():
        continue
    pc = unreal.GameplayStatics.get_player_controller(w, 0)
    if not pc or not hasattr(pc, 'get_commander_net_sync_component'):
        continue
    component = pc.get_commander_net_sync_component()
    report['clients'].append({'world': w.get_path_name(), 'controller': pc.get_name(), 'local': pc.is_local_controller(),
                              'diagnostics': json.loads(str(component.get_move_response_diagnostics()))})
selected = set(i for c in report['clients'] for i in c['diagnostics'].get('selected', []))
for authority in unreal.ObjectIterator(unreal.GuLiBattleAuthoritySubsystem):
    if 'UEDPIE_' not in authority.get_path_name():
        continue
    data = json.loads(str(authority.get_move_response_diagnostics()))
    if not data.get('soldiers'):
        continue
    report['authorities'].append(data)
    server = authority.get_outer()
    selected_soldiers = [s for s in data['soldiers'] if s['id'] in selected]
    for s in selected_soldiers:
        unreal.SystemLibrary.execute_console_command(server, 'gs.GM.Commander.Nav.Soldier ' + str(s['id']))
    unreal.SystemLibrary.execute_console_command(server, 'gs.GM.Commander.Nav.Stats')
    data['selected_soldiers'] = selected_soldiers
    data['probes'] = []
    navs = unreal.GameplayStatics.get_all_actors_of_class(server, unreal.RecastNavMesh)
    nav = next((n for n in navs if 'CommanderSoldier' in n.get_name()), None)
    for soldier in selected_soldiers:
        p = unreal.Vector(*soldier['position'])
        probe = {'id': soldier['id'], 'nav': nav.get_name() if nav else None, 'candidates': [], 'nearby_geometry': []}
        if nav:
            candidates = [(p, 0.1), (p, 300.0)]
            candidates += [(p + unreal.Vector(math.cos(i * math.pi / 4) * 300, math.sin(i * math.pi / 4) * 300, 0), 150.0) for i in range(8)]
            candidates += [(p, 600.0), (p, 1500.0)]
            for index, (point, extent) in enumerate(candidates):
                try:
                    q = unreal.NavigationSystemV1.project_point_to_navigation(server, point, nav, None, unreal.Vector(extent, extent, 50))
                    probe['candidates'].append({'index': index, 'extent': extent, 'point': [q.x, q.y, q.z] if q else None})
                except Exception as exc:
                    probe['candidates'].append({'index': index, 'error': str(exc)})
        for actor in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.Actor):
            center, extent = actor.get_actor_bounds(True, False)
            gap = math.hypot(max(0, abs(p.x - center.x) - extent.x), max(0, abs(p.y - center.y) - extent.y))
            if gap < 700 and extent.x > 0 and extent.y > 0 and extent.x < 5000 and extent.y < 5000:
                probe['nearby_geometry'].append({'name': actor.get_name(), 'class': actor.get_class().get_name(), 'center': [center.x, center.y, center.z], 'extent': [extent.x, extent.y, extent.z], 'gap_xy': gap})
        data['probes'].append(probe)
if report['authorities']:
    report['authority'] = report['authorities'][0]
out.mkdir(parents=True, exist_ok=True)
filename = 'live-' + str(report['pid']) + '-' + str(int(report['captured_at'])) + '.json'
(out / filename).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
(out / 'latest-live.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print('GULI_STUCK_CAPTURE ' + filename)

# Capture a short time series without pausing PIE or changing gameplay state.
# The callback unregisters after three additional snapshots.
if not globals().get('_guli_capture_repeat', False):
    _guli_capture_repeat = True
    _guli_capture_code = compile(Path('D:/UE5.7/test1/Artifacts/MassNavRepair20260924/current-stuck/capture_live.py').read_text(encoding='utf-8'), 'capture_live.py', 'exec')
    _guli_capture_deadline = time.monotonic() + 2
    _guli_capture_remaining = 3
    def _guli_capture_tick(delta):
        global _guli_capture_deadline, _guli_capture_remaining
        if time.monotonic() < _guli_capture_deadline:
            return
        _guli_capture_deadline = time.monotonic() + 2
        _guli_capture_remaining -= 1
        try:
            exec(_guli_capture_code, globals())
        finally:
            if _guli_capture_remaining <= 0:
                unreal.unregister_slate_post_tick_callback(_guli_capture_handle)
    _guli_capture_handle = unreal.register_slate_post_tick_callback(_guli_capture_tick)
