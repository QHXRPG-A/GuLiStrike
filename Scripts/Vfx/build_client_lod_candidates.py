"""Build owned absolute-value review copies. No production table references are switched here."""
import json,traceback
from pathlib import Path
import unreal

OUT=Path(unreal.Paths.project_dir())/'outputs/performance/20261009-client-three-optimizations'
AS,NS,EM=unreal.EditorAssetLibrary,unreal.NiagaraService,unreal.NiagaraEmitterService
report={'success':False,'production_references_changed':False,'saved':[],'systems':[]}

def need(value,what):
    if not value: raise RuntimeError(what)
    return value

def copy(source,target):
    if not AS.does_asset_exist(target): need(AS.duplicate_asset(source,target),'Duplicate '+target)
    return AS.load_asset(target)

def ri(path,emitter,stage,name,value):
    need(NS.set_rapid_iteration_param_by_stage(path,emitter,stage,'Constants.'+emitter+'.'+name,str(value)),name)
    need(NS.set_parameter(path,'Constants.'+emitter+'.'+name,str(value)),'System store '+name)

def compile_save(path):
    result=NS.compile_with_results(path)
    native=unreal.GuLiCombatEffectAuthoringLibrary.get_war_machine_hover_compile_diagnostics(AS.load_asset(path))
    need(result.success and result.error_count==0,str(result))
    need('VM ERROR:' not in native and 'GPU ERROR:' not in native,native)
    need(NS.save_system(path),'Save '+path)
    report['saved'].append(path)
    report['systems'].append({'path':path,'compile':str(result),'native':native,
        'emitters':[{'name':str(e.emitter_name),'enabled':str(e)} for e in NS.list_emitters(path)],
        'counts':[{'name':p.setting_path,'value':p.current_value} for p in NS.get_all_editable_settings(path).rapid_iteration_parameters
                  if any(k in p.setting_path for k in ['Spawn Count','RandomRangeInt.Minimum','RandomRangeInt.Maximum','EndpointSpawnRate','Beam Width'])]})

try:
    need(not unreal.EditorLevelLibrary.get_pie_worlds(True),'PIE must be stopped for candidate authoring')
    for role in ['Impact','Muzzle']:
        source='/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGun'+role+'_AllOptimizations'
        for level in ['Reduced','Minimal']:
            path=source+'_'+level;copy(source,path)
            for e in ['Glow','Flash','RibbonCore','RIbbonTrailFollower','Sparks','Debris']:
                need(NS.enable_emitter(path,e,level=='Reduced' or e in ['Glow','Flash','RibbonCore']),'Enable '+e)
            if level=='Reduced':
                # Original burst is 2..3 sparks and 5 debris. Absolute values preserve the random range.
                ri(path,'Sparks','EmitterUpdate','RandomRangeInt.Minimum',1)
                ri(path,'Sparks','EmitterUpdate','RandomRangeInt.Maximum',2)
                ri(path,'Debris','EmitterUpdate','SpawnBurst_Instantaneous.Spawn Count',3)
            compile_save(path)
    for source in ['/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Optimized_GPU_All',
                   '/Game/GuLiStrike/Buildings/Construction/NS_ConstructionLaser_Optimized_GPU_All']:
        path=source+'_ThreeTier';obj=copy(source,path)
        error=unreal.GuLiCombatEffectAuthoringLibrary.configure_tool_laser_endpoint_lod(obj)
        need(error is not None,'Endpoint LOD graph: '+str(error));compile_save(path)
    report['success']=True
except Exception:
    report['error']=traceback.format_exc()
(OUT/'lod-candidate-build.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'saved':report['saved'],'error':report.get('error')}))
