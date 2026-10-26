"""Bounded, read-only capture of the user's current PIE. No input or viewport access."""
import json
import sys
import time
import types
from pathlib import Path
import unreal

OUT = Path(unreal.Paths.project_dir()) / 'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export'
module = types.ModuleType('guli_structure_capture')
sys.modules[module.__name__] = module
module.rows = []
module.start = time.monotonic()
module.error = None
worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
assert worlds, 'An existing PIE is required; this script never starts PIE.'
module.world = worlds[0]
module.pawn = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(module.world, unreal.load_class(None, '/Script/GuLiStrike.GuLiGroundMechCharacter')) if p.get_controller())
module.mesh = module.pawn.get_editor_property('mesh')
module.anim = module.mesh.get_anim_instance()
module.last_time = -1
module.path = OUT / globals().get('CAPTURE_NAME', 'structure_passive_before.json')
module.reproduce = globals().get('REPRODUCE', False)
module.moving_turn = globals().get('REPRO_MOVING_TURN', False)
module.controller = module.pawn.get_controller()
module.original_transform = module.pawn.get_actor_transform()
module.original_control = module.controller.get_control_rotation()
module.heading = module.original_transform.rotation.rotator()
if module.reproduce and globals().get('REPRO_FLAT', False):
    module.pawn.get_editor_property('character_movement').stop_movement_immediately()
    module.controller.un_possess()
    module.pawn.set_actor_location(unreal.Vector(-4800, 50000, 1151), False, True)
    module.pawn.set_actor_rotation(unreal.Rotator(0, 0, 0), True)
    module.controller.possess(module.pawn)
    module.heading = unreal.Rotator(0, 0, 0)
module.instrumented = hasattr(unreal.GuLiGroundMechAnimationEditorLibrary, 'read_ground_structure')
if module.instrumented:
    unreal.GuLiGroundMechAnimationEditorLibrary.read_ground_structure(module.mesh, True)
module.bones = ['root', 'MiddleJoint', 'UpperLeg_L', 'Leg_L', 'Foot_L', 'FootEnd_L', 'UpperLeg_R', 'Leg_R', 'Foot_R', 'FootEnd_R']


def sample(delta, state=module):
    try:
        if time.monotonic() - state.start > 15:
            unreal.unregister_slate_post_tick_callback(state.handle)
            restore(state)
            state.path.write_text(json.dumps({'pawn': state.pawn.get_path_name(), 'error': state.error, 'rows': state.rows}), encoding='utf-8')
            state.done = True
            return
        t = unreal.GameplayStatics.get_time_seconds(state.world)
        if t == state.last_time:
            return
        state.last_time = t
        a, m, p = state.anim, state.mesh, state.pawn
        if state.reproduce:
            age = time.monotonic() - state.start
            # Existing semantic input executes the real Ground simulation.
            forward = unreal.MathLibrary.get_forward_vector(state.heading)
            if age < 6:
                axis = unreal.Vector2D(forward.y, forward.x)
                aim = p.get_actor_location() + forward * 10000
                phase = 'forward'
            elif age < 10:
                f = p.get_actor_forward_vector()
                axis = unreal.Vector2D(f.y, f.x) if state.moving_turn else unreal.Vector2D(0, 0)
                aim = p.get_actor_location() + unreal.Vector(-forward.y, forward.x, 0) * 10000
                phase = 'moving_turn_right' if state.moving_turn else 'turn_right'
            else:
                axis = unreal.Vector2D(0, 0)
                aim = p.get_actor_location() + forward * 10000
                phase = 'turn_left'
            assert p.submit_gameplay_input(axis, aim, False, False, False, True, True), 'semantic input was rejected'
        else:
            phase = 'passive'
        row = {'time': t, 'pos': p.get_actor_location().to_tuple(), 'yaw': p.get_actor_rotation().yaw,
               'velocity': p.get_velocity().to_tuple(), 'bones': {}, 'phase': phase}
        row['mesh_scale'] = m.get_socket_transform('root', unreal.RelativeTransformSpace.RTS_WORLD).scale3d.to_tuple()
        for name in ['speed', 'turn_rate', 'moving_turn_left_weight', 'moving_turn_right_weight', 'integrated_forward_blend', 'integrated_strafe_blend', 'walk_play_rate', 'run_play_rate', 'is_grounded', 'animation_state_revision']:
            row[name] = a.get_editor_property(name)
        row['plants'] = [a.get_curve_value('GroundPlant_L'), a.get_curve_value('GroundPlant_R')]
        if state.instrumented:
            row['ik'] = json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.read_ground_structure(m, True))
        montage = a.get_current_active_montage()
        row['montage'] = montage.get_name() if montage else None
        row['montage_position'] = a.montage_get_position(montage) if montage else 0
        for name in state.bones:
            transform = m.get_socket_transform(name, unreal.RelativeTransformSpace.RTS_COMPONENT)
            row['bones'][name] = [transform.translation.to_tuple(), transform.rotation.to_tuple()]
        state.rows.append(row)
    except Exception as exc:
        state.error = str(exc)
        unreal.unregister_slate_post_tick_callback(state.handle)
        restore(state)
        state.path.write_text(json.dumps({'error': state.error, 'rows': state.rows}), encoding='utf-8')


def restore(state):
    if state.instrumented:
        unreal.GuLiGroundMechAnimationEditorLibrary.read_ground_structure(state.mesh, False)
    if state.reproduce:
        p = state.pawn
        p.submit_gameplay_input(unreal.Vector2D(0, 0), unreal.Vector(0, 0, 0), False, False, False, False, False)
        p.get_editor_property('character_movement').stop_movement_immediately()
        state.controller.un_possess()
        p.set_actor_transform(state.original_transform, False, True)
        state.controller.possess(p)
        state.controller.set_control_rotation(state.original_control)


module.handle = unreal.register_slate_post_tick_callback(sample)
unreal.MCPythonHelper.submit_result(json.dumps({'started': True, 'pawn': module.pawn.get_path_name(), 'seconds': 15, 'output': str(module.path)}))
