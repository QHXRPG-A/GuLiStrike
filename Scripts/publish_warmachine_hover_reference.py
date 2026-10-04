"""Apply the user-approved WM01 reference to the existing production VFX 48 assets."""
import hashlib
import json
import traceback
from datetime import datetime
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'ArtSource/WarMachineHover_20260930'
BASE='/Game/GuLiStrike/FX/WarMachineHover'
FORMAL=BASE+'/NS_WarMachineHoverPool'
CANDIDATE=BASE+'/Reference_v2/NS_WarMachineHoverReference'
BACKUP=BASE+'/Archive_20260930_PreReference'
LIB=unreal.EditorAssetLibrary
NS=unreal.NiagaraService
EM=unreal.NiagaraEmitterService
report={'success':False,'authorization':'用户：需要应用正式资源','timestamp':datetime.now().isoformat(),
    'formal_system':FORMAL,'backups':[],'native_build':'not_run','runtime_pie':'not_run'}

def require(value,message):
    if not value:raise RuntimeError(message)
    return value

def registry_row():
    table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects')
    rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    row=next(r for r in rows if r.get('Id')==48)
    require(row['Name']=='WarMachineHover' and row['ResourcePath']==FORMAL+'.NS_WarMachineHoverPool',str(row))
    return row

try:
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),'PIE is active')
    checked=json.loads((OUT/'validation.json').read_text(encoding='utf-8'))
    built=json.loads((OUT/'candidate-build.json').read_text(encoding='utf-8'))
    require(checked['success'] and built['success'],'Candidate validation is missing')
    for relative in ['Scripts/Niagara/GuLiHoverReferenceJet.hlsl','Scripts/Niagara/GuLiHoverReferenceTrail.hlsl']:
        expected=next(value for key,value in built['source_sha256'].items() if key.replace('\\','/')==relative)
        require(hashlib.sha256((ROOT/relative).read_bytes()).hexdigest()==expected,'Unreviewed shader change: '+relative)
    require(not NS.parameter_exists(CANDIDATE,'User.HoverPreviewRoot'),'Candidate contains preview-only position overrides')
    report['registry_before']=registry_row()

    # Keep a functional rollback: its renderers must use its own material copies.
    for name in ['M_WarMachineHoverJet','M_WarMachineHoverTrail']:
        target=BACKUP+'/'+name
        asset=unreal.load_asset(target) if LIB.does_asset_exist(target) else LIB.duplicate_asset(BASE+'/'+name,target)
        require(asset,'Backup '+name)
        require(LIB.save_loaded_asset(asset,False),'Save backup '+name)
        report['backups'].append(target)
    target=BACKUP+'/NS_WarMachineHoverPool'
    if not LIB.does_asset_exist(target):
        asset=require(LIB.duplicate_asset(FORMAL,target),'Backup system')
        for emitter,material in [('LaserBolts','M_WarMachineHoverJet'),('LaserMuzzles','M_WarMachineHoverTrail')]:
            require(EM.set_renderer_property(target,emitter,0,'Material',BACKUP+'/'+material),'Backup material binding')
        result=NS.compile_with_results(target)
        require(result.success and not result.errors,str(result))
        require(LIB.save_loaded_asset(asset,False),'Save backup system')
    report['backups'].append(target)

    context={'WM01_HOVER_PUBLISH':True}
    source=ROOT/'Scripts/build_warmachine_hover_reference.py'
    exec(compile(source.read_text(encoding='utf-8'),str(source),'exec'),context)
    result=context['r']
    require(result['success'],result.get('error','Formal build failed'))
    report['saved']=result['saved']
    report['compile']=result['compile']
    require(report['saved']==[BASE+'/M_WarMachineHoverJet',BASE+'/M_WarMachineHoverTrail',FORMAL],'Unexpected publication scope')
    report['parameters']={}
    for name,expected in [('HoverMinTrailLength',500),('HoverMaxTrailLength',1500),('HoverTrailGrowTime',1)]:
        info=NS.get_parameter(FORMAL,'User.'+name)
        require(info is not None,name)
        value=float(info.current_value)
        require(abs(value-expected)<.0001,str(info))
        report['parameters'][name]=value
    report['emitters']={}
    for emitter,expected in [('LaserBolts','M_WarMachineHoverJet'),('LaserMuzzles','M_WarMachineHoverTrail')]:
        properties=EM.get_emitter_properties(FORMAL,emitter)
        require(str(properties.sim_target)=='GPUComputeSim',str(properties))
        modules=[str(m.module_name) for m in EM.list_modules(FORMAL,emitter)]
        require(not any('PreviewReference' in m for m in modules),'Preview generator in production')
        renderer=str(EM.get_renderer_details(FORMAL,emitter,0))
        require(BASE+'/'+expected in renderer,'Wrong production material')
        report['emitters'][emitter]={'properties':str(properties),'modules':modules,'renderer':renderer}
    report['native_gpu_diagnostics']=unreal.GuLiCombatEffectAuthoringLibrary.get_war_machine_hover_compile_diagnostics(unreal.load_asset(FORMAL))
    require('GPU ERROR:' not in report['native_gpu_diagnostics'] and 'VM ERROR:' not in report['native_gpu_diagnostics'],'GPU compile errors')
    require(report['native_gpu_diagnostics'].count('GPU finished=1 complete=1')==2,'GPU compile incomplete')
    require(not NS.parameter_exists(FORMAL,'User.HoverPreviewRoot'),'Preview-only input leaked into production')
    report['registry_after']=registry_row()
    require(report['registry_before']==report['registry_after'],'Registry changed during publication')
    report['production_resource_applied']=True
    report['registry_path_unchanged']=True
    report['success']=True
except Exception:
    report['error']=traceback.format_exc()
(OUT/'formal-publication.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({k:report.get(k) for k in ['success','error','saved','parameters','production_resource_applied','native_build']}))
