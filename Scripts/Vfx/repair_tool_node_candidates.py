"""Correct the owning emitter stores of the existing 16/8-node review copies."""
import ast
import json
from pathlib import Path
import unreal

assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
root = Path(unreal.Paths.project_dir())
tree = ast.parse((root / 'Scripts/Vfx/tool_laser_candidates.py').read_text(encoding='utf-8'))
definitions = []
for node in tree.body:
    if isinstance(node, ast.Try):
        break
    definitions.append(node)
scope = {}
exec(compile(ast.Module(body=definitions,type_ignores=[]),'tool_laser_helpers','exec'),scope)
scope['report'] = {'success':False,'systems':[],'saved':[]}
for role, _, base, widths in scope['CONTRACTS']:
    for count in (16,8):
        target = base + '_Nodes' + str(count)
        for emitter in widths:
            assert unreal.NiagaraService.set_rapid_iteration_param_by_stage(target,emitter,'EmitterUpdate',
                f'Constants.{emitter}.SpawnBurst_Instantaneous.Spawn Count',str(count))
            # Also refresh the copied system RI entry. CPU copies can keep
            # that entry until the asynchronous compiler publishes its data.
            assert unreal.NiagaraService.set_parameter(target,
                f'Constants.{emitter}.SpawnBurst_Instantaneous.Spawn Count',str(count))
        scope['compile_save'](target)
        settings = unreal.NiagaraService.get_all_editable_settings(target)
        for emitter in widths:
            values = [int(float(p.current_value)) for p in settings.rapid_iteration_parameters
                      if p.setting_path == f'Constants.{emitter}.SpawnBurst_Instantaneous.Spawn Count']
            assert values and all(v == count for v in values), (target, emitter, values)
scope['report']['success'] = True
scope['report']['old_measurements_invalid'] = 'Old main Beam was 80 despite labels 16/8. Old node-count comparisons are not evidence about 16/8. Final combined GPU_All was independently compiled/read back as 8 before whole-build sampling.'
(root / 'outputs/performance/20261009-all-optimizations/repaired-node-candidates.json').write_text(json.dumps(scope['report'],ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'saved':scope['report']['saved']}))
