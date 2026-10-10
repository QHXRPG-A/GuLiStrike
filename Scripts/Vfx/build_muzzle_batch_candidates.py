"""Continuous previous-frame NDC consumers derived from the three approved-full/review-LOD muzzle layers."""
import ast,json,traceback
from pathlib import Path
import unreal

OUT=Path(unreal.Paths.project_dir())/'outputs/performance/20261010-muzzle-batch'
OUT.mkdir(parents=True,exist_ok=True)
DEST='/Game/GuLiStrike/FX/CommanderWeapons'
AS,NS,EM,SP=unreal.EditorAssetLibrary,unreal.NiagaraService,unreal.NiagaraEmitterService,unreal.NiagaraScratchPadService
NATIVE=unreal.GuLiCombatEffectAuthoringLibrary
report={'success':False,'saved':[],'systems':[],'production_references_changed':False,'input_certification':False}

def need(value,what):
    if not value: raise RuntimeError(what)
    return value

def scratch(path,emitter,stage,name):
    existing=next((m for m in EM.list_modules(path,emitter) if str(m.module_name)==name),None)
    if existing: return unreal.load_object(None,str(existing.script_asset_path))
    result=SP.create_scratch_module(path,emitter,stage,name)
    need(result.success,'Create '+name)
    return need(unreal.load_object(None,str(result.script_path)),'Load '+name)

try:
    need(not unreal.EditorLevelLibrary.get_pie_worlds(True),'Stop PIE for graph authoring')
    cp=DEST+'/NDC_CommanderMuzzles'
    channel=AS.load_asset(cp) if AS.does_asset_exist(cp) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'NDC_CommanderMuzzles',DEST,unreal.NiagaraDataChannelAsset,unreal.NiagaraDataChannelAssetFactoryNew())
    need(NATIVE.configure_muzzle_channel(channel) is not None,'Configure muzzle NDC')
    need(AS.save_loaded_asset(channel,False),'Save muzzle NDC'); report['saved'].append(cp)
    tree=ast.parse((Path(unreal.Paths.project_dir())/'Scripts/build_commander_combat_effects.py').read_text(encoding='utf-8'))
    builder=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='scratch')
    scope={'SP':SP,'ASSETS':AS,'unreal':unreal,'require':need,'prop':lambda o,k:o.get_editor_property(k)}
    exec(compile(ast.Module(body=[builder],type_ignores=[]),'muzzle_follower_defaults','exec'),scope)
    for level,suffix in [('Full',''),('Reduced','_Reduced'),('Minimal','_Minimal')]:
        source=DEST+'/NS_MachineGunMuzzle_AllOptimizations'+suffix
        path=DEST+'/NS_MachineGunMuzzle_Batch'+suffix
        if not AS.does_asset_exist(path):need(AS.duplicate_asset(source,path),'Copy approved muzzle layers')
        system=AS.load_asset(path)
        for entry in NS.list_emitters(path):
            emitter=str(entry.emitter_name)
            if emitter=='RIbbonTrailFollower' and not any(str(m.module_name)=='GuLiMuzzleFollowerInitializeV3' for m in EM.list_modules(path,emitter)):
                # Event-spawned particles run Spawn before their ReceiveLocationEvent handler.
                scope['scratch'](path,emitter,'ParticleSpawn','GuLiMuzzleFollowerInitializeV3',[],
                    [('Slot','int','Particles.MuzzleSlot'),('Generation','int','Particles.MuzzleGeneration'),('Age','float','Particles.MuzzleAge'),('Alive','bool','DataInstance.Alive'),('Position','Position','Particles.MuzzlePosition'),('Scale','Vector','Particles.MuzzleScale'),('Tint','Color','Particles.MuzzleTint'),('Seed','int','Particles.MuzzleSeed')],
                    'Slot=-1;Generation=0;Age=0;Alive=true;Position=float3(0,0,0);Scale=float3(1,1,1);Tint=float4(1,1,1,1);Seed=0;')
                init_default=next(m for m in EM.list_modules(path,emitter) if str(m.module_name)=='GuLiMuzzleFollowerInitializeV3')
                need(NATIVE.move_client_effect_module(system,emitter,unreal.load_object(None,str(init_default.script_asset_path)),'ParticleSpawn',0) is not None,'Follower defaults before event payload')
            need(EM.set_module_input(path,emitter,'EmitterState','Loop Behavior','NewEnumerator0'),'Infinite consumer '+emitter)
            need(EM.set_module_input(path,emitter,'EmitterState','Life Cycle Mode','NewEnumerator1'),'Self-managed consumer '+emitter)
            for module in list(EM.list_modules(path,emitter)):
                if str(module.module_name).startswith('SpawnBurst_Instantaneous'):
                    need(EM.enable_module(path,emitter,str(module.module_name),False),'Disable per-system burst')
            if emitter!='RIbbonTrailFollower':
                lo,hi=(2,3) if emitter=='Sparks' else (5,5) if emitter=='Debris' else (1,1)
                if level=='Reduced' and emitter=='Sparks':lo,hi=1,2
                if level=='Reduced' and emitter=='Debris':lo,hi=3,3
                bind=scratch(path,emitter,'EmitterSpawn','GuLiMuzzleBind')
                spawn=scratch(path,emitter,'EmitterUpdate','GuLiMuzzleSpawn')
                init=scratch(path,emitter,'ParticleSpawn','GuLiMuzzleRead')
                need(NATIVE.wire_impact_reader(system,channel,bind,spawn,init,lo,hi) is not None,'Wire '+emitter)
                need(NATIVE.move_client_effect_module(system,emitter,init,'ParticleSpawn',0) is not None,'Initialize event inputs first')
            if emitter!='RIbbonTrailFollower':
                birth=scratch(path,emitter,'ParticleSpawn','GuLiMuzzleBirthGate')
                need(NATIVE.wire_impact_lifecycle(system,birth) is not None,'Reject stale previous-frame rows at birth '+emitter)
                need(NATIVE.move_client_effect_module(system,emitter,birth,'ParticleSpawn',-1) is not None,'Birth gate after all initializers '+emitter)
            else:
                if any(str(m.module_name)=='GuLiMuzzleBirthGate' for m in EM.list_modules(path,emitter)):
                    need(EM.enable_module(path,emitter,'GuLiMuzzleBirthGate',False),'Follower event payload arrives after Spawn')
            update=scratch(path,emitter,'ParticleUpdate','GuLiMuzzleLifecycle')
            need(NATIVE.wire_impact_lifecycle(system,update) is not None,'Lifecycle '+emitter)
            need(NATIVE.move_client_effect_module(system,emitter,update,'ParticleUpdate',0) is not None,'Generation before particle state')
            # ParticleState can overwrite Alive. Gate again after all original update modules.
            retire=scratch(path,emitter,'ParticleUpdate','GuLiMuzzleRetire')
            need(NATIVE.wire_impact_lifecycle(system,retire) is not None,'Final generation gate '+emitter)
            need(NATIVE.move_client_effect_module(system,emitter,retire,'ParticleUpdate',-1) is not None,'Retire after original ParticleState '+emitter)
        need(NATIVE.prepare_impact_batch_system(system) is not None,'Per-event inputs and location-event payload')
        for entry in NS.list_emitters(path):
            emitter=str(entry.emitter_name)
            for stage,name in [('ParticleSpawn','GuLiMuzzleBirthRender'),('ParticleUpdate','GuLiMuzzleRender')]:
                render=scratch(path,emitter,stage,name)
                need(NATIVE.wire_muzzle_render(system,emitter,render) is not None,'Local simulation / event world render '+emitter)
                need(NATIVE.move_client_effect_module(system,emitter,render,stage,-1) is not None,'Render after original simulation '+emitter)
        result=NS.compile_with_results(path)
        native=NATIVE.get_war_machine_hover_compile_diagnostics(system)
        entry={'path':path,'level':level,'compile':str(result),'native':native,'input_version':0}
        report['systems'].append(entry)
        need(result.success and result.error_count==0,str(result))
        need('VM ERROR:' not in native and 'GPU ERROR:' not in native,native)
        # Fail closed until real F+1/identity/lifecycle checks certify this exact saved version.
        need(NS.set_parameter(path,'User.GuLiMuzzleInputVersion','0'),'Uncertified input version')
        need(NS.save_system(path),'Save batch candidate');report['saved'].append(path)
    report['success']=True
except Exception:
    report['error']=traceback.format_exc()
(OUT/'muzzle-batch-build.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'saved':report['saved'],'error':report.get('error')}))
