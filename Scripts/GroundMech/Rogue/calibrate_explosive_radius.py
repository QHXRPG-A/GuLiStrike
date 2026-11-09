"""Numeric authoring measurement of the existing explosion, without rendering or gameplay."""
import json
from pathlib import Path
import unreal

OUT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / 'Artifacts/GroundExplosiveEnhancement_20261109'
PATH = '/Game/GuLiStrike/FX/GroundMech/ExplosivePackage/NS_GroundExplosiveImpact'
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
OUT.mkdir(parents=True, exist_ok=True)
asset = unreal.load_asset(PATH)
inputs = json.loads(unreal.GuLiCombatEffectAuthoringLibrary.get_machine_gun_bounds_inputs(asset))
assert 'emitters' in inputs, inputs
(OUT / 'explosion_graph_inputs.json').write_text(json.dumps(inputs, ensure_ascii=False, indent=2), encoding='utf8')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
component = unreal.NiagaraFunctionLibrary.spawn_system_at_location(
    world, asset, unreal.Vector(0, 0, 1000), auto_destroy=False, auto_activate=False, pre_cull_check=False)
assert component
samples = []
try:
    component.set_visibility(False)
    component.set_variable_float('User.Area_Scale', 1.0)
    component.activate(True)
    for step in range(1, 121):
        component.advance_simulation(1, .025)
        samples.append({'time': step * .025,
                        'data': json.loads(unreal.GuLiGroundMechFireEditorLibrary.read_explosive_impact_particles(component))})
finally:
    component.deactivate()
    component.destroy_component(component.get_owner() or component)
(OUT / 'explosion_particle_samples.json').write_text(json.dumps(samples, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'samples': len(samples), 'output': str(OUT), 'asset_modified': False}))
