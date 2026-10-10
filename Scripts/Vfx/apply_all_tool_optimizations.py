"""Build the user-requested combined copies; preserve the previous delivery assets."""
import ast, json, sys, traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir())
OUT = ROOT/'outputs/performance/20261009-all-optimizations'
OUT.mkdir(parents=True, exist_ok=True)
tree = ast.parse((ROOT/'Scripts/Vfx/tool_laser_candidates.py').read_text(encoding='utf-8'))
definitions = []
for node in tree.body:
    if isinstance(node, ast.Try):
        break
    definitions.append(node)
scope = {}
exec(compile(ast.Module(body=definitions,type_ignores=[]),'tool_laser_helpers','exec'),scope)
duplicate, need, compile_save = [scope[k] for k in ['duplicate','need','compile_save']]
scope['report'] = {'success':False,'systems':[],'saved':[]}
report = scope['report']
AS, NS, ES = unreal.EditorAssetLibrary, unreal.NiagaraService, unreal.NiagaraEmitterService

try:
    assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
    report['authorization'] = '直接应用所有优化，并补齐整版对照'
    report['laser_contracts'] = []
    for role, _, old, widths in scope['CONTRACTS']:
        target = old + '_GPU_All'
        duplicate(old, target)
        scope['set_widths'](target, widths)
        for emitter in widths:
            need(ES.enable_module(target,emitter,'SolveForcesAndVelocity',False),'Disable unused beam solver')
        for emitter in ['Spark','Spark001']:
            if any(str(m.module_name)=='Collision' for m in ES.list_modules(target,emitter)):
                need(ES.enable_module(target,emitter,'Collision',False),'Disable endpoint presentation collision')
        error = unreal.GuLiCombatEffectAuthoringLibrary.configure_tool_laser_gpu_candidate(AS.load_asset(target))
        need(error is not None, 'Beam GPU ' + str(error))
        # The generic setter can choose the copied system-level RI store. Set
        # the owning emitter stage after graph edits, then verify every store.
        for emitter in widths:
            need(NS.set_rapid_iteration_param_by_stage(target,emitter,'EmitterUpdate',
                f'Constants.{emitter}.SpawnBurst_Instantaneous.Spawn Count','8'),'Owning-stage 8 nodes')
            need(NS.set_parameter(target,
                f'Constants.{emitter}.SpawnBurst_Instantaneous.Spawn Count','8'),'Copied system 8 nodes')
        compile_save(target)
        settings=NS.get_all_editable_settings(target)
        for emitter in widths:
            values=[int(float(p.current_value)) for p in settings.rapid_iteration_parameters
                    if p.setting_path==f'Constants.{emitter}.SpawnBurst_Instantaneous.Spawn Count']
            need(values and all(value==8 for value in values),'Compiled node readback '+emitter+': '+str(values))
        report['laser_contracts'].append({'role':role,'old':old,'new':target,'widths':widths,
           'nodes':8,'beam_simulation':'GPU','spark_simulation':'CPU','spark_collision':False,
           'beam_solver':False,'visual_change':'Endpoint particles no longer bounce on scenery; source colors/material/ribbon/width envelope retained.'})
    sys.path.insert(0,str(ROOT/'Scripts/Vfx'))
    from certify_machinegun_bounds import certify
    report['flashes'] = []
    for role in ['Muzzle','Impact']:
        old='/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGun'+role+'_Optimized'
        target='/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGun'+role+'_AllOptimizations'
        duplicate(old,target)
        for emitter in ['Sparks','Debris']:
            need(ES.enable_module(target,emitter,'Collision',False),'Disable flash presentation collision')
        compile_save(target)
        report['flashes'].append({'role':role,'old':old,'new':target,'envelope':certify(target),
            'visible_layers_retained':6,'visual_change':'Spark/debris continue their original motion without presentation collision.'})
    report['success']=True
except Exception:
    report['error']=traceback.format_exc()
(OUT/'combined-vfx-build.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'saved':report['saved'],'error':report.get('error')}))
