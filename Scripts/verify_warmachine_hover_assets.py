"""Read saved assets only. Separates shader/resource checks from native, GPU-sim and PIE acceptance."""
import unreal,json,traceback
from pathlib import Path
ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/WarMachineHover_20260929'
r={'gpu_runtime':'see_runtime_validation_record','visual_acceptance':'pending','performance':'see_performance_record','success':False}
try:
    build=OUT/'native-build-report.json'
    r['native_build']=json.loads(build.read_text(encoding='utf-8-sig')) if build.exists() else 'not_run'
    r['native_gpu_authoring_loaded']=hasattr(unreal.GuLiCombatEffectAuthoringLibrary,'configure_war_machine_hover_system')
    paths=json.loads((OUT/'ue-materials.json').read_text())['materials']
    paths += ['/Game/GuLiStrike/FX/WarMachineHover/M_WarMachineHoverJet','/Game/GuLiStrike/FX/WarMachineHover/M_WarMachineHoverTrail']
    r['materials']={}
    for p in paths:
        d=unreal.MaterialNodeService.get_material_diagnostics(p)
        r['materials'][p]={'success':bool(d.success),'compiled':bool(d.is_compiled_ok),'errors':str(d.compile_errors)}
        assert d.success and d.is_compiled_ok,(p,str(d))
    fn=unreal.load_asset('/Game/Commander/Units/MechanicalAnimation/MF_GuLiRigidMechanical')
    nodes=[n for n in unreal.ObjectIterator(unreal.MaterialExpression) if n.get_outer()==fn]
    indices=sorted(n.get_editor_property('data_index') for n in nodes if isinstance(n,unreal.MaterialExpressionPerInstanceCustomData))
    r['custom_data_indices']=indices;assert indices==list(range(1,29)),indices
    for p in paths[:6]:
        g=json.loads(unreal.MaterialNodeService.export_material_graph(p))
        calls=[n for n in g['expressions'] if n['class']=='MaterialFunctionCall' and n.get('function_path','').startswith(fn.get_path_name())]
        assert len(calls)==1,(p,'duplicated WPO calls')
        r['materials'][p]['rigid_function_calls']=len(calls)
    mesh=unreal.load_asset('/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Rigid')
    r['mesh_class']=mesh.get_class().get_name()
    r['nozzles']={label:list(mesh.find_socket('FX_Hover_'+label).relative_location.to_tuple()) for label in ['FL','FR','RL','RR']}
    system='/Game/GuLiStrike/FX/WarMachineHover/NS_WarMachineHoverPool'
    r['simulation_targets']={str(e.emitter_name):str(e.sim_target) for e in unreal.NiagaraService.list_emitters(system)}
    r['gpu_asset_ready']=len(r['simulation_targets'])==2 and all(x=='GPUComputeSim' for x in r['simulation_targets'].values())
    if r['native_gpu_authoring_loaded']:
        r['native_compile_diagnostics']=unreal.GuLiCombatEffectAuthoringLibrary.get_war_machine_hover_compile_diagnostics(unreal.load_asset(system))
        r['gpu_asset_ready'] &= r['native_compile_diagnostics'].count('GPU finished=1 complete=1')==2
        assert 'VM ERROR:' not in r['native_compile_diagnostics'] and 'GPU ERROR:' not in r['native_compile_diagnostics']
    r['success']=True
except:r['error']=traceback.format_exc()
(OUT/'asset-verification.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({k:r.get(k) for k in ['success','error','native_gpu_authoring_loaded','custom_data_indices','simulation_targets','gpu_asset_ready']}))
