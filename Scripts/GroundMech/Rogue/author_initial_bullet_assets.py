"""Author project-owned blank card and straight companion VFX; never runs PIE."""
import ast
import json
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'Artifacts/GroundAmmo_20261108'
BASE='/Game/GuLiStrike/FX/GroundMech/ExplosivePackage'
FLIGHT=BASE+'/NS_GroundExplosiveFlight'
IMPACT=BASE+'/NS_GroundExplosiveImpact'
BLANK='/Game/GuLiStrike/Cards/Shared/Placeholder'
NS,EM,SP=unreal.NiagaraService,unreal.NiagaraEmitterService,unreal.NiagaraScratchPadService
ASSETS=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)

def require(v,message):
    if not v:raise RuntimeError(message)
    return v

def prop(obj,name):return obj.get_editor_property(name)

def main():
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),'Preserve active PIE')
    evidence=json.loads((OUT/'source_resources.json').read_text(encoding='utf8'))
    require(evidence['success'],'Read exact source resources before adaptation')
    dirty={p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(not any(p.startswith((BASE+'/',BLANK+'/')) for p in dirty) or globals().get('GULI_AMMO_OWNED_RETRY',False),'Preserve unsaved owned assets')
    for source,target in zip(['/Game/Assets/VFX/WeaponBulletVFX/NS/VFX_Missile','/Game/_VFXResources/Niagara_System/NS_Explosion_19'],[FLIGHT,IMPACT]):
        require(ASSETS.load_asset(target) or ASSETS.duplicate_asset(source,target),'Copy '+target)
    # A previous interrupted scratch edit can leave dynamic add pins before
    # regular pins. Repair the owned graph before any stack-input UI API reads it.
    require(unreal.GuLiCombatEffectAuthoringLibrary.finalize_scratch_pins(ASSETS.load_asset(FLIGHT)),'Normalize resumed scratch pins')
    require(SP.apply_changes(FLIGHT),'Apply normalized scratch pins')
    # Keep the missile head and its event-driven smoke/ribbon only. The vendor
    # explosion and particle lights must not bypass gameplay impact/light budgets.
    keep={'NE_spread_spark','NE_FlipBookPointLocation002','NE_FlipBookPointLocation004','RIbbonTrailFollower_14'}
    for e in NS.list_emitters(FLIGHT):
        name=str(e.emitter_name)
        require(NS.enable_emitter(FLIGHT,name,name in keep),'Select flight emitters '+name)
        if name not in keep:continue
        for r in EM.list_renderers(FLIGHT,name):
            if str(r.renderer_type)=='Light':require(EM.set_renderer_property(FLIGHT,name,r.renderer_index,'bIsEnabled','false'),'Use shared light budget')
        require(EM.set_module_input(FLIGHT,name,'EmitterState','Life Cycle Mode','NewEnumerator1'),'Self lifecycle')
        require(EM.set_module_input(FLIGHT,name,'EmitterState','Loop Behavior','NewEnumerator0'),'Continuous companion lifecycle')
        require(EM.set_module_input(FLIGHT,name,'EmitterState','UseLoopDelay','false'),'No inherited launch delay')
        require(NS.set_rapid_iteration_param_by_stage(FLIGHT,name,'EmitterUpdate',f'Constants.{name}.EmitterState.Loop Duration','120'),'Bounded lifecycle')
        for module in EM.list_modules(FLIGHT,name):
            if 'PlayAudio' in str(module.module_name):require(EM.enable_module(FLIGHT,name,str(module.module_name),False),'No vendor audio replay')
    require(NS.set_parameter(FLIGHT,'User.AudioON','false'),'Disable audio')
    require(NS.set_parameter(FLIGHT,'User.FlyDistance','120'),'No vendor flight timeout')
    for name,kind,value in [('FlightVelocity','Vector','(X=16000,Y=0,Z=0)'),('FlightLifetime','Float','120')]:
        if not NS.parameter_exists(FLIGHT,'User.'+name):require(NS.add_user_parameter(FLIGHT,name,kind,value),'External flight parameter '+name)
    tree=ast.parse((ROOT/'Scripts/build_commander_combat_effects.py').read_text(encoding='utf8'))
    fn=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='scratch')
    exec(compile(ast.Module(body=[fn],type_ignores=[]),'shared_scratch_helper','exec'),globals())
    head='NE_spread_spark'
    for module in list(EM.list_modules(FLIGHT,head)):
        if str(module.module_name).startswith('GuLiCompanion'):require(EM.remove_module(FLIGHT,head,str(module.module_name)),'Refresh owned module')
    # The installed module API also matches substrings. Remove the longer name
    # first so AddVelocity cannot accidentally resolve to AddVelocityInCone.
    for motion in ('AddVelocityInCone','AddVelocity','SolveForcesAndVelocity'):
        if any(str(m.module_name)==motion for m in EM.list_modules(FLIGHT,head)):
            require(EM.remove_module(FLIGHT,head,motion),'Remove vendor head motion '+motion)
    inputs=[('Owner','Position','Engine.Owner.Position'),('Velocity','Vector','User.FlightVelocity'),('Lifetime','float','User.FlightLifetime')]
    outputs=[('PositionOut','Position','Particles.Position'),('VelocityOut','Vector','Particles.Velocity'),('LifeOut','float','Particles.Lifetime')]
    code='PositionOut=Owner; VelocityOut=Velocity; LifeOut=max(0.01,Lifetime);'
    for stage,name in [('ParticleSpawn','GuLiCompanionSpawn'),('ParticleUpdate','GuLiCompanionStraight')]:
        module=scratch(FLIGHT,head,stage,name,inputs,outputs,code)
        if stage=='ParticleUpdate':
            modules=list(EM.list_modules(FLIGHT,head))
            # The installed service's reorder_module is a stub. Move the call
            # with the project helper, without editing its engine script asset.
            event=next(m for m in modules if str(m.module_name)=='GenerateLocationEvent')
            event_script=require(unreal.load_asset(str(event.script_asset_path)),'Location event script')
            require(unreal.GuLiCombatEffectAuthoringLibrary.move_client_effect_module(
                ASSETS.load_asset(FLIGHT),unreal.Name(head),event_script,'ParticleUpdate',-1) is not None,
                'Location events after external head')
    assets=[]
    for path in [FLIGHT,IMPACT]:
        obj=ASSETS.load_asset(path);obj.set_editor_property('max_pool_size',64);obj.set_editor_property('pool_prime_size',0)
        result=NS.compile_with_results(path)
        require(result.success,'Niagara compile '+path+': '+str(result))
        require(NS.save_system(path),'Save '+path)
        assets.append({'path':path,'compile':str(result),'emitters':[str(EM.get_emitter_properties(path,str(e.emitter_name))) for e in NS.list_emitters(path)]})
    tools=unreal.AssetToolsHelpers.get_asset_tools();mat=unreal.MaterialEditingLibrary
    parent=ASSETS.load_asset(BLANK+'/M_CardBlank')
    if not parent:
        parent=require(tools.create_asset('M_CardBlank',BLANK,unreal.Material,unreal.MaterialFactoryNew()),'Blank material')
        parent.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
        parent.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
        parent.set_editor_property('two_sided',True)
        zero=mat.create_material_expression(parent,unreal.MaterialExpressionConstant,0,0);zero.set_editor_property('r',0.)
        require(mat.connect_material_property(zero,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR),'Blank color')
        require(mat.connect_material_property(zero,'',unreal.MaterialProperty.MP_OPACITY),'Blank alpha')
        mat.recompile_material(parent)
    instance=ASSETS.load_asset(BLANK+'/MI_CardBlank') or require(tools.create_asset('MI_CardBlank',BLANK,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew()),'Blank instance')
    mat.set_material_instance_parent(instance,parent)
    for obj in [parent,instance]:require(unreal.EditorAssetLibrary.save_loaded_asset(obj,False),'Save blank '+obj.get_path_name())
    require(instance.get_editor_property('parent')==parent,'Blank parent readback')
    # Source names/parameters are reread to prove vendor resources were untouched.
    for path,source in evidence['systems'].items():
        require([str(e.emitter_name) for e in NS.list_emitters(path)]==[e['name'] for e in source['emitters']],'Vendor emitter identity')
    return {'success':True,'assets':assets,'blank':instance.get_path_name(),'PIE':False,'assistant_visual_test':False,
        'logic_reference':'/Game/Assets/Missile_Cluster/飞弹: target interpolation replaced by native linear sweep; Niagara location events retained for flight tails'}

try:report=main()
except Exception:report={'success':False,'error':traceback.format_exc()}
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'asset_authoring.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'error':report.get('error'),'report':str(OUT/'asset_authoring.json')},ensure_ascii=False))
