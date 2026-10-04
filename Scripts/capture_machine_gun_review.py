"""Render the saved comparison samples without saving preview activation state."""
import json
import time
import traceback
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/ArtSource/MachineGunEffects_20260930/Previews')
OUT.mkdir(parents=True, exist_ok=True)
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
owned = {a.get_actor_label(): a for a in actors.get_all_level_actors() if a.actor_has_tag('MachineGunReview20260930')}
array_api = unreal.NiagaraDataInterfaceArrayFunctionLibrary
original_arrays = []
for preview_actor in owned.values():
    preview_actor.set_editor_property('is_editor_only_actor', False)
    if 'Tracer' in preview_actor.get_actor_label():
        preview_comp = preview_actor.get_component_by_class(unreal.NiagaraComponent)
        original_arrays.append((preview_comp, list(array_api.get_niagara_array_color(preview_comp, 'User.LaserColors')),
                                list(array_api.get_niagara_array_color(preview_comp, 'User.LaserLightColors')),
                                list(array_api.get_niagara_array_bool(preview_comp, 'User.LaserLightEnabled'))))
camera = actors.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(), transient=True)
capture = camera.capture_component2d
capture.set_editor_property('capture_every_frame', False)
capture.set_editor_property('capture_on_movement', False)
capture.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
capture.set_editor_property('primitive_render_mode', unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
capture.set_editor_property('fov_angle', 45)
rt = unreal.RenderingLibrary.create_render_target2d(world, 1280, 720, unreal.TextureRenderTargetFormat.RTF_RGBA8,
                                                   unreal.LinearColor(.015,.02,.03,1), False, False)
capture.set_editor_property('texture_target', rt)
samples = [('MuzzleBefore', 1100), ('MuzzleWM01After', 1100), ('MuzzleSweeperAfter', 1100),
           ('ImpactBefore', 850), ('ImpactAfter', 850), ('TracerBefore', 2200), ('TracerAfter', 2200)]
state = {'sample': 0, 'phase': 0, 'step': 'setup', 'frames': 0, 'captures': [], 'started': time.monotonic()}

def finish(error=None):
    unreal.unregister_slate_post_tick_callback(state['handle'])
    for comp, colors, lights, enabled in original_arrays:
        array_api.set_niagara_array_color(comp, 'User.LaserColors', colors)
        array_api.set_niagara_array_color(comp, 'User.LaserLightColors', lights)
        array_api.set_niagara_array_bool(comp, 'User.LaserLightEnabled', enabled)
    for actor in owned.values():
        actor.set_editor_property('is_editor_only_actor', True)
        comp = actor.get_component_by_class(unreal.NiagaraComponent)
        if comp:
            comp.set_paused(False)
            comp.deactivate()
    actors.destroy_actor(camera)
    report = {'success': error is None, 'error': error, 'captures': state['captures'],
              'scope': 'Isolated real Niagara comparison at identical camera distance; player visual acceptance pending.'}
    (OUT/'isolated-review.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

def tick(delta):
    state['frames'] += 1
    if state['frames'] % 4:
        return
    try:
        if time.monotonic() - state['started'] > 90:
            raise RuntimeError('Preview capture exceeded 90 seconds')
        label, distance = samples[state['sample']]
        actor = owned['MachineGunReview_' + label]
        comp = actor.get_component_by_class(unreal.NiagaraComponent)
        tracer = label.startswith('Tracer')
        if state['step'] == 'setup':
            capture.clear_show_only_components()
            capture.show_only_actor_components(owned['MachineGunReview_Ground'])
            capture.show_only_actor_components(actor)
            aim = actor.get_actor_location() + (unreal.Vector(400, 425, 120) if tracer else unreal.Vector())
            loc = aim + unreal.Vector(0, -distance, distance * .75)
            camera.set_actor_location_and_rotation(loc, unreal.MathLibrary.find_look_at_rotation(loc, aim), False, True)
            comp.activate(True)
            comp.set_paused(True)
            state.update(age=0, step='simulate')
        elif state['step'] == 'simulate':
            age = [2/60, 6/60, 15/60, 60/60][state['phase']]
            if tracer and state['phase'] >= 2:
                arrays = unreal.NiagaraDataInterfaceArrayFunctionLibrary
                fade = .25 if state['phase'] == 2 else 0.0
                colors = list(arrays.get_niagara_array_color(comp, 'User.LaserColors'))
                lights = list(arrays.get_niagara_array_color(comp, 'User.LaserLightColors'))
                for c in colors: c.a = fade
                for c in lights: c.a = (25 if label == 'TracerAfter' else 0) * fade
                arrays.set_niagara_array_color(comp, 'User.LaserColors', colors)
                arrays.set_niagara_array_color(comp, 'User.LaserLightColors', lights)
                if not fade:
                    arrays.set_niagara_array_bool(comp, 'User.LaserLightEnabled', [False]*1024)
            comp.set_paused(False)
            comp.advance_simulation_by_time(age - state['age'] + .0001, 1/60)
            comp.set_paused(True)
            state.update(age=age, step='capture')
        elif state['step'] == 'capture':
            capture.capture_scene()
            state['step'] = 'export'
        else:
            phase = ['start','peak','fade','clear'][state['phase']]
            name = label + '_' + phase + '.png'
            unreal.RenderingLibrary.export_render_target(world, rt, str(OUT), name)
            state['captures'].append({'sample': label, 'phase': phase, 'age': state['age'], 'image': name})
            state['phase'] += 1
            if state['phase'] == 4:
                comp.set_paused(False)
                comp.deactivate()
                state.update(sample=state['sample']+1, phase=0, step='setup')
                if state['sample'] == len(samples):
                    finish()
            else:
                state['step'] = 'simulate'
    except Exception:
        finish(traceback.format_exc())

state['handle'] = unreal.register_slate_post_tick_callback(tick)
MACHINE_GUN_ISOLATED_REVIEW = state
print({'started': True, 'samples': len(samples)})
