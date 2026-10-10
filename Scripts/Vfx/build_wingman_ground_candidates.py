"""Owned opaque sphere/mesh-trail candidate, using the frozen actual legacy transverse diameter."""
import ast,json,traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()); OUT=ROOT/'outputs/performance/20261009-client-three-optimizations'
DEST='/Game/GuLiStrike/FX/CommanderWeapons'; PATH=DEST+'/NS_WingmanGroundFlight_SphereTrail'
AS,NS,EM,SP=unreal.EditorAssetLibrary,unreal.NiagaraService,unreal.NiagaraEmitterService,unreal.NiagaraScratchPadService
NATIVE,EDIT=unreal.GuLiCombatEffectAuthoringLibrary,unreal.MaterialEditingLibrary
report={'success':False,'production_references_changed':False,'saved':[],'materials':[]}

def need(value,message):
    if not value: raise RuntimeError(message)
    return value

def material(name,trail):
    path=DEST+'/'+name
    mat=AS.load_asset(path) if AS.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,DEST,unreal.Material,unreal.MaterialFactoryNew())
    need(mat,'Material '+name); EDIT.delete_all_material_expressions(mat)
    mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided',False)
    EDIT.set_material_usage(mat,unreal.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES)
    need(EDIT.has_material_usage(mat,unreal.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES),'Niagara mesh usage')
    color=EDIT.create_material_expression(mat,unreal.MaterialExpressionParticleColor)
    need(EDIT.connect_material_property(color,'RGB',unreal.MaterialProperty.MP_EMISSIVE_COLOR),'Opaque emission')
    if trail:
        local=EDIT.create_material_expression(mat,unreal.MaterialExpressionPreSkinnedPosition)
        taper=EDIT.create_material_expression(mat,unreal.MaterialExpressionCustom)
        taper.set_editor_property('code','float u=saturate(Local.z/100.0+0.5); float t=lerp(saturate(Ratio),1.0,u); return float3(Local.xy*(t-1.0),0);')
        taper.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        pins=[]
        for key in ['Local','Ratio']:
            p=unreal.CustomInput();p.set_editor_property('input_name',key);pins.append(p)
        taper.set_editor_property('inputs',pins)
        need(EDIT.connect_material_expressions(local,'',taper,'Local'),'Local position')
        need(EDIT.connect_material_expressions(color,'A',taper,'Ratio'),'Tail start/end width ratio')
        transform=EDIT.create_material_expression(mat,unreal.MaterialExpressionTransform)
        transform.set_editor_property('transform_source_type',unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
        transform.set_editor_property('transform_type',unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
        need(EDIT.connect_material_expressions(taper,'',transform,''),'Local taper displacement')
        need(EDIT.connect_material_property(transform,'',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET),'Taper mesh vertices')
    EDIT.recompile_material(mat)
    diagnostics=unreal.MaterialNodeService.get_material_diagnostics(path)
    need(diagnostics.is_compiled_ok,str(diagnostics))
    need(AS.save_loaded_asset(mat,False),'Save material');report['saved'].append(path)
    report['materials'].append({'path':path,'blend':'Opaque','shading':'Unlit','taper':trail,'diagnostics':str(diagnostics)})
    return path

def create_scratch(path,emitter,stage,name):
    r=SP.create_scratch_module(path,emitter,stage,name);need(r.success,'Scratch '+name)
    return need(unreal.load_object(None,str(r.script_path)),name)

try:
    need(not unreal.EditorLevelLibrary.get_pie_worlds(True),'Stop PIE for candidate authoring')
    frozen=json.loads((OUT/'wingman-core-baseline.json').read_text(encoding='utf-8'))
    W0=float(frozen['W0_centimeters']);need(W0>0 and frozen['success'],'Frozen runtime W0')
    core=material('M_WingmanGroundCore_Opaque',False)
    tail=material('M_WingmanGroundTail_Opaque',True)
    if not AS.does_asset_exist(PATH):need(NS.create_system(PATH.rsplit('/',1)[1],DEST).success,'Create shared system')
    for e in list(NS.list_emitters(PATH)):need(NS.remove_emitter(PATH,str(e.emitter_name)),'Rebuild owned emitter')
    # Reuse the installed typed scratch builder; this reads definitions only and does not execute the old authoring task.
    tree=ast.parse((ROOT/'Scripts/build_commander_combat_effects.py').read_text(encoding='utf-8'))
    builder=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='scratch')
    scope={'SP':SP,'ASSETS':AS,'unreal':unreal,'require':need,'prop':lambda o,k:o.get_editor_property(k)}
    exec(compile(ast.Module(body=[builder],type_ignores=[]),'wingman_scratch_helper','exec'),scope)
    scratch=scope['scratch']
    for emitter,count,mat in [('Head',32,core),('Tail',256,tail)]:
        need(NS.add_emitter(PATH,'/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst.SimpleSpriteBurst',emitter)==emitter,'Add '+emitter)
        for m in list(EM.list_modules(PATH,emitter)):
            if not str(m.module_name).startswith(('EmitterState','ParticleState')):need(EM.remove_module(PATH,emitter,str(m.module_name)),'Strip template')
        need(EM.add_module(PATH,emitter,'/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous','EmitterUpdate'),'Single allocation burst')
        need(NS.set_rapid_iteration_param_by_stage(PATH,emitter,'EmitterUpdate',f'Constants.{emitter}.SpawnBurst_Instantaneous.Spawn Count',str(count)),'Capacity')
        need(NS.set_rapid_iteration_param_by_stage(PATH,emitter,'EmitterUpdate',f'Constants.{emitter}.EmitterState.Loop Duration','1000000000'),'Keep allocated particles alive')
        need(EM.set_module_input(PATH,emitter,'EmitterState','Loop Behavior','NewEnumerator1'),'Once allocation')
        scratch(PATH,emitter,'ParticleSpawn','GuLiWingmanInitialize',[],
            [('Life','float','Particles.Lifetime'),('Position','Position','Particles.Position'),('Velocity','Vector','Particles.Velocity'),
             ('Scale','Vector','Particles.Scale'),('Color','Color','Particles.Color')],
            'Life=1e9;Position=float3(0,0,0);Velocity=float3(0,0,0);Scale=float3(0,0,0);Color=float4(0,0,0,0);')
        for stage in ['ParticleSpawn','ParticleUpdate']:
            module=create_scratch(PATH,emitter,stage,'GuLiWingmanRead'+stage)
            need(NATIVE.wire_wingman_mesh_reader(AS.load_asset(PATH),module,emitter=='Tail') is not None,'Wire mesh arrays')
        need(EM.remove_renderer(PATH,emitter,0),'Remove sprite')
        need(EM.add_renderer(PATH,emitter,'Mesh'),'Mesh renderer')
        need(EM.set_renderer_property(PATH,emitter,0,'bOverrideMaterials','true'),'Override material')
        need(EM.set_renderer_property(PATH,emitter,0,'OverrideMaterials',f'((ExplicitMat="/Script/Engine.Material\'{mat}.{mat.rsplit("/",1)[1]}\'"))'),'Mesh material')
    system=AS.load_asset(PATH);need(NATIVE.configure_wingman_mesh_system(system) is not None,'GPU mesh bindings')
    result=NS.compile_with_results(PATH);native=NATIVE.get_war_machine_hover_compile_diagnostics(system)
    need(result.success and result.error_count==0,str(result))
    need('VM ERROR:' not in native and 'GPU ERROR:' not in native,native)
    need(native.count('GPU finished=1 complete=1')==2,native)
    need(NS.set_parameter(PATH,'User.GuLiWingmanInputVersion','1'),'Compiled input contract')
    need(NS.save_system(PATH),'Save shared system');report['saved'].append(PATH)
    pp=DEST+'/DA_WingmanGroundFlightPresentation'
    if AS.does_asset_exist(pp):profile=AS.load_asset(pp)
    else:
        factory=unreal.DataAssetFactory();factory.set_editor_property('data_asset_class',unreal.GuLiProjectileFlightPresentationProfile)
        profile=unreal.AssetToolsHelpers.get_asset_tools().create_asset(pp.rsplit('/',1)[1],DEST,unreal.GuLiProjectileFlightPresentationProfile,factory)
    profile.set_editor_property('BatchSystem',system);profile.set_editor_property('LegacyCoreDiameter',W0)
    profile.set_editor_property('MaximumTrailLength',5000);profile.set_editor_property('PulsePeriod',.5);profile.set_editor_property('TrailFadeSeconds',.55)
    need(AS.save_loaded_asset(profile,False),'Save opt-in profile');report['saved'].append(pp)
    report.update(success=True,W0_centimeters=W0,diameter_range_cm=[W0*1.5,W0*2.5],emission_range=[1,4],tail_head_width_cm=W0*.35,
        profile=pp,native=native,compile=str(result),renderers={e:[str(x) for x in EM.list_renderers(PATH,e)] for e in ['Head','Tail']})
except Exception:
    report['error']=traceback.format_exc()
(OUT/'wingman-candidate-build.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'saved':report['saved'],'W0':report.get('W0_centimeters'),'error':report.get('error')}))
