"""Host script: inspect the legacy renderer in a real dedicated/two-client PIE outside a measurement window."""
import json,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Scripts'))
from Performance import run_four_stage_review as review
OUT=ROOT/'outputs/performance/20261009-client-three-optimizations'
try:
    review.result('w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\nunreal.SystemLibrary.execute_console_command(w,"gs.WingmanFlight.Presentation 0")')
    setup=review.launch('flight','data_pool')
    started=review.run('w=unreal.EditorLevelLibrary.get_pie_worlds(True)[0]\nf=next(x for x in unreal.ObjectIterator(unreal.GuLiFlightAcceptanceSubsystem) if x.get_outer()==w)\nf.stop_load()\nunreal.MCPythonHelper.submit_result(json.dumps({"success":f.start_wingman_ground_load(125,45)}))')
    time.sleep(2)
    data=review.run('''
definition=unreal.load_asset('/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundMissile')
effect=unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(definition.flight_vfx_id)
system=effect.resource_path
base=json.loads(unreal.GuLiCombatEffectAuthoringLibrary.get_wingman_core_baseline(system))
components=[]
for c in unreal.ObjectIterator(unreal.NiagaraComponent):
 if c.get_world() in unreal.EditorLevelLibrary.get_pie_worlds(True)[1:] and c.get_asset()==system and c.is_active():
  value,valid=c.get_variable_float('User.VisualScale')
  components.append({'world':c.get_world().get_path_name(),'component':c.get_path_name(),'component_scale':list(c.get_world_transform().scale3d.to_tuple()),'visual_scale':value,'visual_scale_valid':valid})
states=[]
for sub in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem):
 if sub.get_world() in unreal.EditorLevelLibrary.get_pie_worlds(True)[1:]:
  states.extend([{'source':str(s.source.kind),'definition':str(s.projectile_definition),'radius':s.radius,'fixed_point':s.get_editor_property('bFixedPoint')} for s in sub.get_effect_states() if str(s.projectile_definition)==str(definition)])
unreal.MCPythonHelper.submit_result(json.dumps({'success':bool(components) and bool(states),'native':base,'definition':definition.get_path_name(),'effect_id':definition.flight_vfx_id,'registered_scale':list(effect.scale.to_tuple()),'component_count':len(components),'ground_state_count':len(states),'components':components[:4],'actual_ground_states':states[:4]}))
''')
    assert all(c['visual_scale_valid'] and c['component_scale']==[1,1,1] for c in data['components']),data
    scale=data['components'][0]['visual_scale']
    assert all(abs(c['visual_scale']-scale)<1e-6 for c in data['components'])
    data['W0_centimeters']=max(data['native']['mesh_y_diameter'],data['native']['mesh_z_diameter'])*scale
    data['formula']='raw transverse mesh diameter × Particles.Scale(User.VisualScale) × component transverse scale(1)'
    data['setup']=setup
    (OUT/'wingman-core-baseline.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'success':True,'W0_centimeters':data['W0_centimeters'],'actual_components':len(data['components'])}),flush=True)
finally:
    review.stop()
