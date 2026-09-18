"""Versioned, compare-before-write Vec2 RI migration for the project's mining copy only."""
import hashlib,json,os,re,traceback
from pathlib import Path
import unreal
root=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
out=root/'TestResults/Scale020'
baseline=out/'mining-input-readback.json'
path='/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Green'
report={'version':1,'success':False,'source_baseline_sha256':hashlib.sha256(baseline.read_bytes()).hexdigest(),
        'asset':path,'coordinate_space':'world-centimeters','entries':[],'changed':0}
def sizes():
    return {p.input_name:p.current_value for p in
            unreal.NiagaraEmitterService.get_rapid_iteration_parameters(path,'Spark','ParticleSpawn')}
def numbers(value): return [float(n) for n in re.findall(r'-?\d+(?:\.\d+)?',value)]
def equal(a,b): return len(a)==len(b) and all(abs(x-y)<.0001 for x,y in zip(a,b))
try:
    report['switches']=dict(unreal.GuLiScaleMigrationLibrary.read_mining_size_switches())
    before=sizes()
    source=json.loads(baseline.read_text(encoding='utf-8'))['rapid_iteration']['Spark']
    pending=[]
    for suffix,old,target in [('Min',[5.,8.],[1.,1.6]),('Max',[1.5,20.],[.3,4.])]:
        field='Constants.Spark.InitializeParticle.Sprite Size '+suffix
        key='[ParticleSpawn] '+field
        recorded=next(p for p in source if p['name']==key)
        assert equal(numbers(recorded['value']),old),recorded
        current=numbers(before[key])
        assert equal(current,old) or equal(current,target),(key,current)
        row={'emitter':'Spark','stage':'ParticleSpawn','field':field,'before':old,'target':target,
             'read_before':current,'changed':not equal(current,target)}
        report['entries'].append(row)
        if row['changed']: pending.append(row)
    # Persist the full intent before mutating. A repeat applies absolute targets.
    (out/'mining-sprite-size-manifest-v1.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    for row in pending:
        assert unreal.GuLiScaleMigrationLibrary.set_mining_sprite_size020('Spark','ParticleSpawn',row['field'],
            unreal.Vector2D(*row['before']),unreal.Vector2D(*row['target'])),row
    report['changed']=len(pending)
    if pending:
        result=unreal.NiagaraService.compile_with_results(path)
        report['compile']={'success':result.success,'errors':result.error_count,'warnings':result.warning_count,
                           'messages':list(result.errors)}
        assert result.success and result.error_count==0,report['compile']
        assert unreal.NiagaraService.save_system(path)
    after=sizes()
    for row in report['entries']:
        row['read_after']=numbers(after['[ParticleSpawn] '+row['field']])
        assert equal(row['read_after'],row['target']),row
    report['success']=True
except Exception: report['error']=traceback.format_exc()
finally:
    name=os.environ.get('GULI_SCALE020_MINING_REPORT','mining-sprite-size-readback.json')
    assert Path(name).name==name
    (out/name).write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.SystemLibrary.quit_editor()
