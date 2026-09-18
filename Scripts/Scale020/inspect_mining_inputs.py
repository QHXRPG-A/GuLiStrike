"""Read-only Niagara pin/default diagnostics; no compilation or package save."""
import json,traceback
from pathlib import Path
import unreal
out=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))/'TestResults/Scale020/mining-input-readback.json'
report={'success':False,'emitters':{}}
try:
    path='/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Green'
    for emitter in unreal.NiagaraService.list_emitters(path):
        name=emitter.emitter_name
        rows=[]
        for module,inputs in {
            'InitializeParticle':['Sprite Size Mode','Sprite Size','Uniform Sprite Size','Sprite Size Min','Sprite Size Max','Position Mode'],
            'ScaleSpriteSize':['Scale Calculation Method','Initial Sprite Size','Use Initial Size','Scale Mode'],
            'AddVelocity':['Velocity Mode','Velocity Speed','Radius Falloff Near / Far'],
            'CurlNoiseForce':['Noise Strength','Noise Frequency','Pan Noise Field']}.items():
            for param in inputs:
                rows.append({'module':module,'input':param,
                    'value':unreal.NiagaraEmitterService.get_module_input(path,name,module,param)})
        report['emitters'][name]=rows
    report['rapid_iteration']={}
    for emitter in ('Beam','Beam001','Spark','Spark001'):
        report['rapid_iteration'][emitter]=[
            {'name':p.input_name,'type':p.input_type,'value':p.current_value}
            for p in unreal.NiagaraEmitterService.get_rapid_iteration_parameters(path,emitter,'')]
    report['success']=True
except Exception:report['error']=traceback.format_exc()
out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.SystemLibrary.quit_editor()
