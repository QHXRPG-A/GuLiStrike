"""Bounded, read-only observation of the two reported issues in the current PIE."""
import json
import math
import time
import traceback
from pathlib import Path
import unreal

out = Path('D:/UE5.7/test1/Artifacts/Diagnostics/20260928-movement-projectiles/post-build-observation.json')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world
authority = next(o for o in unreal.ObjectIterator(unreal.GuLiBattleAuthoritySubsystem) if o.get_outer() == world)
presentation = next(o for o in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if o.get_outer() == world)
runtime = next(o for o in unreal.ObjectIterator(unreal.GuLiCombatEffectRuntimeSubsystem) if o.get_outer() == world)
started = time.monotonic()
last_nav = -1.0
result = {'world': world.get_path_name(), 'read_only': True, 'nav': [], 'effects': [], 'frames': 0}
handle = None


def xyz(v):
    return [v.x, v.y, v.z]


def finish(error=None):
    global handle
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        handle = None
    result['duration_seconds'] = time.monotonic() - started
    result['error'] = error
    result['counters'] = presentation.get_counters().export_text() if not error else None
    out.write_text(json.dumps(result, indent=2), encoding='utf-8')


def tick(dt):
    global last_nav
    try:
        elapsed = time.monotonic() - started
        if elapsed >= 12.0:
            finish()
            return
        current_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if current_world != world:
            finish('PIE world changed during passive observation')
            return
        result['frames'] += 1
        now = unreal.GameplayStatics.get_time_seconds(world)
        if elapsed - last_nav >= 0.1:
            last_nav = elapsed
            diag = json.loads(authority.get_move_response_diagnostics())
            result['nav'].append({'time': now, 'sim_tick': diag['sim_tick'], 'soldiers': diag['soldiers']})
        for state in presentation.get_effect_states():
            if state.kind != unreal.GuLiCombatEffectKind.LINEAR_PROJECTILE:
                continue
            actual = runtime.query_effect(state.effect_id)
            row = {'time': now, 'id': str(state.effect_id), 'state': state.export_text(),
                   'confirmed': actual.export_text() if actual else None}
            result['effects'].append(row)
    except Exception:
        finish(traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
print(json.dumps({'started': True, 'duration_seconds': 12, 'output': str(out)}))
