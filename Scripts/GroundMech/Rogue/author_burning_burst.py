"""Rebuild the finite pulse from the COMPLETE approved attached-burning system."""
import ast
import json
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'Artifacts/GroundHUDRevision_20261103'
SOURCE='/Game/GuLiStrike/FX/GroundMech/Burning/NS_GroundBurningStatus'
SYSTEM='/Game/GuLiStrike/FX/GroundMech/Burning/NS_GroundBurningBurst'
ASSETS=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
NS,EM=unreal.NiagaraService,unreal.NiagaraEmitterService
SP=unreal.NiagaraScratchPadService

def prop(value,name):return value.get_editor_property(name)

def require(value,message):
    if not value:raise RuntimeError(message)
    return value

def build():
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),'Preserve active PIE')
    dirty={p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(SYSTEM not in dirty or globals().get('GULI_BURST_RETRY_OWNED',False),'Preserve unsaved burst edits')
    system=ASSETS.load_asset(SYSTEM) or require(ASSETS.duplicate_asset(SOURCE,SYSTEM),'Derive complete burning')
    names=[str(e) for e in NS.summarize(SOURCE).emitter_names]
    require(set(names)=={'Dots','Smoke001','Trails'},'Approved burning source changed')
    # Rebuild this owned system in place, preserving its public asset identity.
    for emitter in list(NS.summarize(SYSTEM).emitter_names):
        require(NS.remove_emitter(SYSTEM,str(emitter)),'Remove superseded pulse emitter')
    for name in names:require(NS.copy_emitter(SOURCE,name,SYSTEM,name)==name,'Copy full source emitter '+name)
    tree=ast.parse((ROOT/'Scripts/build_commander_combat_effects.py').read_text(encoding='utf-8'))
    fn=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='scratch')
    exec(compile(ast.Module(body=[fn],type_ignores=[]),'shared_combat_scratch_helper','exec'),globals())
    defaults=[('Radius','Float','500'),('StartRadius','Float','100'),('InitialAge','Float','0'),('Lifetime','Float','1'),
              ('Seed','Int','0'),('ExpandSeconds','Float','0.5'),('FadeSeconds','Float','0.5')]
    present={str(p.parameter_name).removeprefix('User.') for p in NS.list_parameters(SYSTEM)}
    for name,kind,value in defaults:
        if name not in present:require(NS.add_user_parameter(SYSTEM,name,kind,value),'Parameter '+name)
        require(NS.set_parameter(SYSTEM,'User.'+name,value),'Parameter default '+name)
    records=[]
    def move(emitter,module,stage,index):
        item=next(m for m in EM.list_modules(SYSTEM,emitter) if str(m.module_name)==module)
        script=unreal.load_object(None,str(item.script_asset_path))
        require(unreal.GuLiCombatEffectAuthoringLibrary.move_client_effect_module(system,unreal.Name(emitter),script,stage,index) is not None,'Module order '+module)
    for emitter in names:
        renderers=EM.list_renderers(SYSTEM,emitter)
        ribbon=any(str(r.renderer_type)=='Ribbon' for r in renderers)
        source_materials=[str(EM.get_renderer_details(SOURCE,emitter,i).material_path) for i in range(len(renderers))]
        # Preserve source solver, color, shape location and InitializeRibbon.
        # Only the emitter lifetime/spawn budget and a final presentation envelope change.
        require(EM.remove_module(SYSTEM,emitter,'EmitterState'),'Replace stale template lifecycle')
        require(EM.add_module(SYSTEM,emitter,'/Niagara/Modules/Emitter/EmitterState.EmitterState','EmitterUpdate'),'Finite lifecycle')
        require(EM.set_module_input(SYSTEM,emitter,'EmitterState','Life Cycle Mode','NewEnumerator1'),'Self lifecycle')
        require(EM.set_module_input(SYSTEM,emitter,'EmitterState','Loop Behavior','NewEnumerator1'),'Once')
        require(NS.set_rapid_iteration_param_by_stage(SYSTEM,emitter,'EmitterUpdate',f'Constants.{emitter}.EmitterState.Loop Duration','1'),'Duration')
        module,key,count=('SpawnBurst_Instantaneous','Spawn Count',96) if emitter=='Dots' else ('SpawnRate','SpawnRate',128 if ribbon else 48)
        require(NS.set_rapid_iteration_param_by_stage(SYSTEM,emitter,'EmitterUpdate',f'Constants.{emitter}.{module}.{key}',str(count)),'Bounded spawn count')
        common_in=[('PositionIn','Position','Particles.Position'),('SizeIn','vec2','Particles.SpriteSize'),
            ('ColorIn','Color','Particles.Color'),('Expand','float','User.ExpandSeconds'),('Fade','float','User.FadeSeconds'),
            ('Seed','int','User.Seed'),('VelocityIn','Vector','Particles.Velocity')]
        common_out=[('BasePosition','Position','Particles.GuLiBurstBasePosition'),('BaseSize','vec2','Particles.GuLiBurstBaseSize'),
            ('BaseTint','Color','Particles.GuLiBurstBaseTint'),('Life','float','Particles.Lifetime'),
            ('PositionOut','Position','Particles.Position'),('VelocityOut','Vector','Particles.Velocity')]
        if ribbon:
            common_in += [('WidthIn','float','Particles.RibbonWidth')]
            common_out += [('BaseWidth','float','Particles.GuLiBurstBaseWidth')]
        capture=scratch(SYSTEM,emitter,'ParticleSpawn','GuLiBurstCapture',common_in,common_out,
            '''float angle=frac(float(Seed & 32767)*0.6180339)*6.2831853;
float c=cos(angle),s=sin(angle);
BasePosition=float3(PositionIn.x*c-PositionIn.y*s,PositionIn.x*s+PositionIn.y*c,PositionIn.z);
PositionOut=BasePosition;VelocityOut=float3(VelocityIn.x*c-VelocityIn.y*s,VelocityIn.x*s+VelocityIn.y*c,VelocityIn.z);
BaseSize=SizeIn;BaseTint=ColorIn;Life=Expand+Fade;'''+('BaseWidth=WidthIn;' if ribbon else ''))
        restore_in=[('BasePosition','Position','Particles.GuLiBurstBasePosition'),('BaseSize','vec2','Particles.GuLiBurstBaseSize'),('BaseTint','Color','Particles.GuLiBurstBaseTint')]
        restore_out=[('PositionOut','Position','Particles.Position'),('SizeOut','vec2','Particles.SpriteSize'),('ColorOut','Color','Particles.Color')]
        if ribbon:
            restore_in += [('BaseWidth','float','Particles.GuLiBurstBaseWidth')]
            restore_out += [('WidthOut','float','Particles.RibbonWidth')]
        restore=scratch(SYSTEM,emitter,'ParticleUpdate','GuLiBurstRestore',restore_in,restore_out,
            'PositionOut=BasePosition;SizeOut=BaseSize;ColorOut=BaseTint;'+('WidthOut=BaseWidth;' if ribbon else ''))
        inputs=[('PositionIn','Position','Particles.Position'),('SizeIn','vec2','Particles.SpriteSize'),('Tint','Color','Particles.Color'),
            ('SystemAge','float','System.Age'),('InitialAge','float','User.InitialAge'),('Start','float','User.StartRadius'),
            ('Radius','float','User.Radius'),('Expand','float','User.ExpandSeconds'),('Fade','float','User.FadeSeconds')]
        outputs=[('BasePosition','Position','Particles.GuLiBurstBasePosition'),('PositionOut','Position','Particles.Position'),
            ('SizeOut','vec2','Particles.SpriteSize'),('ColorOut','Color','Particles.Color'),('Alive','bool','DataInstance.Alive')]
        if ribbon:
            inputs += [('WidthIn','float','Particles.RibbonWidth')]
            outputs += [('WidthOut','float','Particles.RibbonWidth')]
        code='''float t=max(SystemAge+InitialAge,0.0);float grow=saturate(t/max(Expand,.001));
float envelope=lerp(min(Start,Radius),Radius,grow);
BasePosition=PositionIn;float distance=length(PositionIn);
float3 direction=distance>.0001?PositionIn/distance:float3(0,0,1);
float normalDistance=saturate(distance/100.0);
'''
        code += '''float halfSize=min(abs(WidthIn)*envelope/200.0,envelope*.12);
WidthOut=halfSize*2;SizeOut=SizeIn;''' if ribbon else '''float sourceHalf=length(SizeIn)*.5;
float halfSize=min(sourceHalf*envelope/100.0,envelope*.25);
SizeOut=SizeIn*(halfSize/max(sourceHalf,.001));'''
        code += '''
PositionOut=direction*(max(envelope-halfSize,0.0)*normalDistance);
float f=saturate((t-Expand)/max(Fade,.001));float fade=1-f*f*(3-2*f);
ColorOut=float4(Tint.rgb,Tint.a*fade);Alive=t<Expand+Fade;'''
        envelope=scratch(SYSTEM,emitter,'ParticleUpdate','GuLiBurstEnvelope',inputs,outputs,code)
        move(emitter,capture,'ParticleSpawn',-1)
        move(emitter,restore,'ParticleUpdate',0)
        move(emitter,envelope,'ParticleUpdate',-1)
        actual=[str(EM.get_renderer_details(SYSTEM,emitter,i).material_path) for i in range(len(renderers))]
        require(actual==source_materials,'Preserve all burning renderer materials')
        records.append({'emitter':emitter,'ribbon':ribbon,'materials':actual,'spawn_budget':count,
            'modules':[{'name':str(m.module_name),'stage':str(m.module_type)} for m in EM.list_modules(SYSTEM,emitter)]})
    require(unreal.GuLiCombatEffectAuthoringLibrary.configure_buff_burst_system(system) is not None,'Bounds and simulation')
    system.set_editor_property('max_pool_size',128);system.set_editor_property('pool_prime_size',0)
    result=NS.compile_with_results(SYSTEM)
    diagnostics=unreal.GuLiCombatEffectAuthoringLibrary.get_buff_compile_diagnostics(system)
    require(result.success and not result.error_count and 'ERROR:' not in diagnostics and 'status=Error' not in diagnostics,'Burst compile: '+str(result)+' '+diagnostics)
    require(NS.save_system(SYSTEM),'Save complete fire pulse')
    return {'success':True,'source':SOURCE,'asset':system.get_path_name(),'emitters':records,'diagnostics':diagnostics,
        'expansion_seconds':.5,'fade_seconds':.5,'lifetime_seconds':1,'radius_cm':500,'visual_acceptance':'pending_user'}

if __name__=='__main__':
    OUT.mkdir(parents=True,exist_ok=True)
    try:report=build()
    except Exception:report={'success':False,'error':traceback.format_exc()}
    (OUT/'burst_authoring.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'report':str(OUT/'burst_authoring.json'),'error':report.get('error','')}))
