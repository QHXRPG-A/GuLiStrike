"""Author Commander stronghold data and owned presentation assets in the live editor."""
import json
import traceback
from pathlib import Path
import unreal

PROJECT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = PROJECT / 'TestResults/CommanderStrongholds'
OUT.mkdir(parents=True, exist_ok=True)
ROOT = '/Game/GuLiStrike/FX/StrongholdTransit'
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MAT = unreal.MaterialEditingLibrary
NS = unreal.NiagaraService
EM = unreal.NiagaraEmitterService
report = {'saved': [], 'tables': [], 'niagara': {}, 'materials': {}}


def save(path):
    assert path.startswith((ROOT+'/', '/Game/GuLiStrike/Vehicles/ConstructionVehicle/'))
    assert ASSETS.save_asset(path, only_if_is_dirty=False), path
    report['saved'].append(path)


def duplicate(source, destination):
    if not ASSETS.does_asset_exist(destination):
        assert ASSETS.duplicate_asset(source, destination), destination
    return ASSETS.load_asset(destination)


def custom(material, inputs, code, kind):
    expression = MAT.create_material_expression(material, unreal.MaterialExpressionCustom, 0,0)
    expression.set_editor_property('output_type', kind)
    parameters = []
    for name in inputs:
        item = unreal.CustomInput()
        item.set_editor_property('input_name',name)
        parameters.append(item)
    expression.set_editor_property('inputs',parameters)
    expression.set_editor_property('code',code)
    for name, source in inputs.items():
        assert MAT.connect_material_expressions(source,'',expression,name)
    return expression


def main():
    assert not unreal.WidgetService.is_pie_running()
    builder = '/Game/GuLiStrike/Vehicles/ConstructionVehicle/BP_ConstructionVehicle'
    duplicate('/Game/GuLiStrike/Vehicles/ElectromagneticMiner/BP_MiningVehicle_TransporterLvl2',builder)
    for side in ['L','R']:
        assert unreal.BlueprintService.set_component_property(builder,'MiningLaser_'+side,'bAutoActivate','false')
        assert unreal.BlueprintService.set_component_property(builder,'MiningLaser_'+side,'bVisible','false')
    compiled = unreal.BlueprintService.compile_blueprint(builder)
    assert compiled.success, str(compiled)
    save(builder)
    report['builder_compile'] = str(compiled)

    namespace = {'__name__':'stronghold_import'}
    source = (PROJECT/'Scripts/import_data_to_engine.py').read_text(encoding='utf-8')
    definitions = source.split('report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}',1)[0]
    exec(compile(definitions,'import_data_to_engine.py','exec'),namespace)
    namespace['PROGRESS'] = str(OUT/'import_progress.log')
    manifest = json.loads((PROJECT/'Data/Json/manifest.json').read_text(encoding='utf-8'))
    for name in ['DT_GuLiStrikeBuildings_Buildings','DT_GuLiStrikeCommander_Soldiers','DT_GuLiStrikeSpellFields_Fields']:
        result = namespace['import_table'](name,manifest['tables'][name])
        report['tables'].append(result)
        assert result.get('imported'), result

    energy = ROOT+'/M_TransitEnergy'
    material = ASSETS.load_asset(energy) if ASSETS.does_asset_exist(energy) else TOOLS.create_asset(
        'M_TransitEnergy',ROOT,unreal.Material,unreal.MaterialFactoryNew())
    MAT.delete_all_material_expressions(material)
    material.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    tint = MAT.create_material_expression(material,unreal.MaterialExpressionVectorParameter,0,0)
    tint.set_editor_property('parameter_name','Tint')
    tint.set_editor_property('default_value',unreal.LinearColor(0,.55,1,1))
    opacity = MAT.create_material_expression(material,unreal.MaterialExpressionScalarParameter,0,0)
    opacity.set_editor_property('parameter_name','Opacity')
    opacity.set_editor_property('default_value',.9)
    normal = MAT.create_material_expression(material,unreal.MaterialExpressionPixelNormalWS,0,0)
    camera = MAT.create_material_expression(material,unreal.MaterialExpressionCameraVectorWS,0,0)
    emission = custom(material,{'N':normal,'V':camera,'Tint':tint},
        'float core=pow(saturate(abs(dot(N,V))),8); return Tint.rgb*(4+core*8)+float3(.55,.8,1)*core*8;',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    assert MAT.connect_material_property(emission,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert MAT.connect_material_property(opacity,'',unreal.MaterialProperty.MP_OPACITY)
    MAT.layout_material_expressions(material)
    MAT.recompile_material(material)
    save(energy)
    for path in [energy]:
        diagnostics = unreal.MaterialNodeService.get_material_diagnostics(path)
        report['materials'][path] = str(diagnostics)
        assert diagnostics.success and diagnostics.is_compiled_ok and not diagnostics.compile_errors, str(diagnostics)

    trail = ROOT+'/NS_TransitTrail'
    duplicate('/Game/GuLiStrike/FX/WingmanFlight/NS_WingmanFlightTrail',trail)
    for emitter in list(NS.list_emitters(trail)):
        name = str(emitter.emitter_name)
        if name not in ['LeftFlightRibbon','TransitRibbon']:
            assert NS.remove_emitter(trail,name)
    if any(str(e.emitter_name)=='LeftFlightRibbon' for e in NS.list_emitters(trail)):
        assert NS.rename_emitter(trail,'LeftFlightRibbon','TransitRibbon')
    ribbon = ROOT+'/M_TransitTrail'
    duplicate('/Game/GuLiStrike/FX/WingmanFlight/M_WingmanFlightTrail',ribbon)
    save(ribbon)
    assert EM.set_renderer_property(trail,'TransitRibbon',1,'Material',ribbon)
    assert NS.set_parameter(trail,'User.NozzleSpacing','0')
    assert NS.set_parameter(trail,'User.Throttle','1')
    assert NS.set_parameter(trail,'User.Tint','(R=0.0,G=0.65,B=1.0,A=1.0)')
    assert NS.set_parameter(trail,'User.Opacity','1')
    system = ASSETS.load_asset(trail)
    system.set_editor_property('effect_type',None)
    system.set_editor_property('bFixedBounds',True)
    system.set_editor_property('FixedBounds',unreal.Box(min=unreal.Vector(-250000,-250000,-250000),max=unreal.Vector(250000,250000,250000)))
    result = NS.compile_with_results(trail)
    report['niagara'][trail] = str(result)
    assert result.success and not result.errors, str(result)
    save(trail)

    flash = ROOT+'/NS_TransitFlash'
    if not ASSETS.does_asset_exist(flash):
        result = NS.create_system('NS_TransitFlash',ROOT,'/Niagara/DefaultAssets/Templates/Systems/DirectionalBurst')
        assert result.success, str(result)
    flash_material = ROOT+'/M_TransitFlash'
    material = ASSETS.load_asset(flash_material) if ASSETS.does_asset_exist(flash_material) else TOOLS.create_asset(
        'M_TransitFlash',ROOT,unreal.Material,unreal.MaterialFactoryNew())
    MAT.delete_all_material_expressions(material)
    material.set_editor_property('blend_mode',unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    uv = MAT.create_material_expression(material,unreal.MaterialExpressionTextureCoordinate,0,0)
    color = MAT.create_material_expression(material,unreal.MaterialExpressionParticleColor,0,0)
    age = MAT.create_material_expression(material,unreal.MaterialExpressionParticleRelativeTime,0,0)
    glow = custom(material,{'UV':uv,'Color':color,'Age':age},
        'float r=length(UV.xy-.5)*2; float mask=pow(saturate(1-r),3); return float3(.1,.65,1)*mask*30*(1-saturate(Age));',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    mask = custom(material,{'UV':uv,'Age':age},
        'return pow(saturate(1-length(UV.xy-.5)*2),2)*(1-saturate(Age));',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    assert MAT.connect_material_property(glow,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert MAT.connect_material_property(mask,'',unreal.MaterialProperty.MP_OPACITY)
    MAT.recompile_material(material)
    save(flash_material)
    for emitter in list(NS.list_emitters(flash)):
        assert NS.remove_emitter(flash,str(emitter.emitter_name))
    emitter = str(NS.add_emitter(flash,'/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst.SimpleSpriteBurst','TransitFlash'))
    assert emitter
    for stage, module, name, value in [
        ('EmitterUpdate','EmitterState','Loop Duration','0.2'),
        ('EmitterUpdate','SpawnBurst_Instantaneous','Spawn Count','1'),
        ('EmitterUpdate','SpawnBurst_Instantaneous','Spawn Time','0'),
        ('ParticleSpawn','InitializeParticle','Lifetime','0.2'),
        ('ParticleSpawn','InitializeParticle','Uniform Sprite Size','8000'),
        ('ParticleSpawn','InitializeParticle','Color','0.1,0.65,1.0,1.0')]:
        assert NS.set_rapid_iteration_param_by_stage(flash,emitter,stage,f'Constants.{emitter}.{module}.{name}',value)
    assert EM.set_renderer_property(flash,emitter,0,'Material',flash_material)
    result = NS.compile_with_results(flash)
    report['niagara'][flash] = str(result)
    assert result.success and not result.errors, str(result)
    save(flash)
    report['success'] = True


try:
    main()
except Exception:
    report['success'] = False
    report['error'] = traceback.format_exc()
(OUT/'asset_authoring.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'success':report['success'],'saved':report['saved'],'error':report.get('error')},ensure_ascii=False))
