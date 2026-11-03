"""Numerical Niagara simulation in the editor, without rendering or gameplay."""
import json
from pathlib import Path
import unreal

assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
asset=unreal.load_asset('/Game/GuLiStrike/FX/GroundMech/Burning/NS_GroundBurningBurst')
component=unreal.NiagaraFunctionLibrary.spawn_system_at_location(world,asset,unreal.Vector(0,0,1000),auto_destroy=False,auto_activate=False,pre_cull_check=False)
assert component
rows=[]
try:
    for key,value in [('Radius',500),('StartRadius',175),('InitialAge',0),('Lifetime',1),('ExpandSeconds',.5),('FadeSeconds',.5)]:
        component.set_variable_float('User.'+key,value)
    component.set_variable_int('User.Seed',12345)
    component.activate(True)
    for step in range(1,23):
        component.advance_simulation(1,.05)
        rows.append({'requested_age':step*.05,'data':json.loads(unreal.GuLiCombatEffectAuthoringLibrary.inspect_burning_burst_component(component))})
finally:
    component.deactivate()
    component.destroy_component(component.get_owner() or component)
p=Path(globals().get('GULI_PROBE_OUT',Path(unreal.Paths.project_dir())/'Artifacts/GroundRevision_20261103'))/'geometry_probe.json'
p.write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'rows':len(rows),'report':str(p),'samples':[
    {'time':row['requested_age'],'emitters':[{k:e[k] for k in ('emitter','particles','outer_radius','max_alpha')} for e in row['data']['emitters']]}
    for row in (rows[9],rows[10],rows[-1])]}))
