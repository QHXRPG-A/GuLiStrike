"""World-space mining particles and culling. No local-space explosions are rescaled here."""
import json,re,math
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()); OUT=ROOT/'TestResults/Scale020'
baseline=json.loads((OUT/'effect-space-inspection.json').read_text(encoding='utf-8'))
path='/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Green'
NS=unreal.NiagaraService
entries=[]
suffixes=('BeamWidth.Beam Width','AddVelocity.Velocity','GravityForce.Gravity',
          'InitializeParticle.Uniform Sprite Size','RandomRangeFloat.Maximum','RandomRangeFloat.Minimum',
          'SolveForcesAndVelocity.Acceleration Limit','SolveForcesAndVelocity.Speed Limit')
for p in baseline[path]['editable_settings']['rapid_iteration_parameters']:
    if not p['setting_path'].endswith(suffixes): continue
    numbers=re.findall(r'-?\d+(?:\.\d+)?',p['current_value'])
    if not numbers or 'raw' in p['current_value']: continue
    target=', '.join(format(float(n)*.2,'.6f') for n in numbers)
    entries.append({'emitter':p['emitter_name'],'stage':p['script_stage'],'field':p['setting_path'],
                    'old':p['current_value'],'target':target})
current=NS.get_all_editable_settings(path)
by_key={(x.emitter_name,x.script_stage,x.setting_path):x.current_value for x in current.rapid_iteration_parameters}
def nums(s): return [float(x) for x in re.findall(r'-?\d+(?:\.\d+)?',s)]
def eq(a,b): return len(nums(a))==len(nums(b)) and all(math.isclose(x,y,abs_tol=1e-4) for x,y in zip(nums(a),nums(b)))
pending=[]
for e in entries:
    c=by_key[e['emitter'],e['stage'],e['field']]
    if eq(c,e['target']): continue
    if not eq(c,e['old']): raise RuntimeError('Conflicting mining setting: '+str(e)+' current='+c)
    pending.append(e)
for e in pending:
    assert NS.set_rapid_iteration_param_by_stage(path,e['emitter'],e['stage'],e['field'],e['target']),e
compile_result=NS.compile_with_results(path)
if not compile_result.success or compile_result.error_count:
    raise RuntimeError(str(compile_result))
assert NS.save_system(path)
fx_entries=[]
for fx_path in ['/Game/GuLiStrike/FX/UnitFeedback/FXT_UnitDestruction','/Game/GuLiStrike/FX/WingmanFlight/FXT_WingmanFlight']:
    obj=unreal.load_asset(fx_path)
    container=obj.get_editor_property('system_scalability_settings')
    settings=container.get_editor_property('settings')
    for s in settings:
        old=s.get_editor_property('max_distance')
        assert old in (180000.,36000.),(fx_path,old)
        s.set_editor_property('max_distance',36000.)
        fx_entries.append({'path':fx_path,'old':old,'target':36000.})
    container.set_editor_property('settings',settings)
    obj.set_editor_property('system_scalability_settings',container)
    assert unreal.EditorAssetLibrary.save_loaded_asset(obj,only_if_is_dirty=False)
report={'mining':entries,'mining_changed':len(pending),'effect_type_settings':fx_entries,
        'compile_errors':compile_result.error_count,'compile_warnings':compile_result.warning_count}
(OUT/'effect-constants-migration.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'mining_changed':len(pending),'compile_errors':compile_result.error_count,'effect_types':len(fx_entries)}))
