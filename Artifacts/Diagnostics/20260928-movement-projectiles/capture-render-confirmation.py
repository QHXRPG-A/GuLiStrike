"""Read Niagara upload arrays and collision state; do not issue game commands."""
import json
import math
import re
import time
import traceback
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/Artifacts/Diagnostics/20260928-movement-projectiles/post-build-render-confirmation-visible.json')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world
authority = next(o for o in unreal.ObjectIterator(unreal.GuLiBattleAuthoritySubsystem) if o.get_outer() == world)
presentation = next(o for o in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if o.get_outer() == world)
runtime = next(o for o in unreal.ObjectIterator(unreal.GuLiCombatEffectRuntimeSubsystem) if o.get_outer() == world)
arrays = unreal.NiagaraDataInterfaceArrayFunctionLibrary
start = time.monotonic()
last_sample = -1.0
last_nav = -1.0
last_components = -10.0
components = []
handle = None
pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
original_camera_pivot = pawn.get_actor_location()
focused_at = None
result = {'world': world.get_path_name(), 'gameplay_read_only': True, 'camera_only_temporary_change': True,
          'measurement': 'Native Niagara CPU upload arrays, not GPU particle readback',
          'samples': [], 'nav': [], 'terminals': {}, 'frames': 0, 'visible_array_samples': 0, 'unmatched_array_samples': 0}


def xyz(v):
    return [float(v.x), float(v.y), float(v.z)]


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def finish(error=None):
    global handle
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        handle = None
    result['error'] = error
    if focused_at is not None and unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() == world:
        pawn.jump_to_world_location(original_camera_pivot)
        result['camera_restored'] = True
        result['original_camera_pivot'] = xyz(original_camera_pivot)
    result['duration_seconds'] = time.monotonic() - start
    result['counters'] = presentation.get_counters().export_text() if not error else None
    OUT.write_text(json.dumps(result, indent=2), encoding='utf-8')


def tick(dt):
    global last_sample, last_nav, last_components, components, focused_at
    try:
        elapsed = time.monotonic() - start
        if focused_at is not None and time.monotonic() - focused_at >= 15:
            finish()
            return
        if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() != world:
            finish('PIE world changed')
            return
        if focused_at is None:
            candidates = [s for s in presentation.get_effect_states()
                          if s.kind == unreal.GuLiCombatEffectKind.LINEAR_PROJECTILE
                          and s.source.kind == unreal.GuLiTargetKind.COMMANDER_SOLDIER]
            if candidates:
                candidates.sort(key=lambda s:math.dist(xyz(s.location), xyz(pawn.get_actor_location())))
                pawn.jump_to_world_location(candidates[0].location)
                focused_at = time.monotonic()
                result['focus_location'] = xyz(candidates[0].location)
                return
            if elapsed >= 12:
                finish('No live ground shots available in the bounded observation window')
            return
        if elapsed - last_sample < 0.05:
            return
        last_sample = elapsed
        result['frames'] += 1
        now = unreal.GameplayStatics.get_time_seconds(world)
        if elapsed - last_nav >= 0.1:
            last_nav = elapsed
            diag = json.loads(authority.get_move_response_diagnostics())
            keys = ['id', 'team', 'unit_type', 'alive', 'position', 'velocity', 'no_progress', 'nav_state', 'command', 'order']
            result['nav'].append({'time': now, 'sim_tick': diag['sim_tick'],
                                  'soldiers': [{k:s[k] for k in keys} for s in diag['soldiers'] if s['unit_type'] == 2]})
        if elapsed - last_components >= 1:
            last_components = elapsed
            components = [o for o in unreal.ObjectIterator(unreal.NiagaraComponent)
                          if o.get_world() == world and o.get_asset() and 'Pool' in o.get_asset().get_name()]
        states = []
        for s in presentation.get_effect_states():
            if s.kind != unreal.GuLiCombatEffectKind.LINEAR_PROJECTILE or s.source.kind != unreal.GuLiTargetKind.COMMANDER_SOLDIER:
                continue
            exported = s.export_text()
            eid = re.search(r'EffectId=([0-9A-Fa-f]{32})', exported).group(1)
            launch, velocity, direction = xyz(s.launch_location), xyz(s.velocity), xyz(s.launch_direction)
            sample_time, start_time, end_time = s.sample_time, s.start_time, s.end_time
            finished = s.phase == unreal.GuLiCombatEffectPhase.FINISHED
            render_time = min(now - 0.2, sample_time)
            reached = finished and render_time >= sample_time
            age = min(max(render_time - start_time, 0), end_time - start_time)
            head = xyz(s.location) if reached else [p+v*age for p,v in zip(launch,velocity)]
            confirmed = runtime.query_effect(s.effect_id)
            confirmation = confirmed.sample_time if confirmed else sample_time
            sequence = re.search(r'Sequence=(\d+)', exported)
            row = {'time':now,'id':eid,'sequence':int(sequence.group(1)) if sequence else None,'phase':str(s.phase),'reason':str(s.end_reason),
                   'launch':launch,'direction':direction,'speed':s.motion.speed,'start_time':start_time,
                   'sample_time':sample_time,'collision_sample_time':confirmation,'expected_head':head}
            states.append(row)
            if finished:
                result['terminals'][eid] = {'point':xyz(s.location),'start_time':start_time,'sample_time':sample_time,'reason':str(s.end_reason),
                                            'launch':launch,'direction':direction}
        for component in components:
            if not component.is_active():
                continue
            positions = arrays.get_niagara_array_position(component, 'User.LaserPositions')
            sizes = arrays.get_niagara_array_vector2d(component, 'User.LaserSizes')
            colors = arrays.get_niagara_array_color(component, 'User.LaserColors')
            directions = arrays.get_niagara_array_vector(component, 'User.LaserDirections')
            for index, (p, size, color, direction) in enumerate(zip(positions, sizes, colors, directions)):
                if color.a <= 0 or size.y <= 0:
                    continue
                result['visible_array_samples'] += 1
                head = [a+b*size.y*0.5 for a,b in zip(xyz(p),xyz(direction))]
                row = min(states, key=lambda s:math.dist(head,s['expected_head']), default=None)
                if row is None or math.dist(head,row['expected_head']) > 1.0:
                    result['unmatched_array_samples'] += 1
                    continue
                row = dict(row)
                row['niagara_head'] = head
                row['match_error_cm'] = math.dist(head,row['expected_head'])
                row['slot'] = index
                distance = dot([a-b for a,b in zip(head,row['launch'])],row['direction'])
                row['ahead_of_confirmed_cm'] = distance - max(0,row['collision_sample_time']-row['start_time'])*row['speed']
                result['samples'].append(row)
    except Exception:
        finish(traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
print(json.dumps({'started':True,'duration_seconds':15,'output':str(OUT)}))
