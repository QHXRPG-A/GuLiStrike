"""Build the project-owned Wingman engine flame and world-space ribbon in UE5.7.

Run through Scripts/ue_exec.py. Only /Game/GuLiStrike/FX/WingmanFlight is saved.
"""
import json
import traceback
from pathlib import Path
import unreal

DEST = '/Game/GuLiStrike/FX/WingmanFlight'
SYSTEM = DEST + '/NS_WingmanFlightTrail'
TEMPLATE = '/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain'
OUT = Path('D:/UE5.7/test1/TestResults/WingmanFlightVFX')
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
NS, EM, SP = unreal.NiagaraService, unreal.NiagaraEmitterService, unreal.NiagaraScratchPadService
MAT = unreal.MaterialEditingLibrary
report = {'system': SYSTEM, 'template': TEMPLATE, 'saved': [], 'materials': {}}


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def save(asset):
    path = asset if isinstance(asset, str) else asset.get_path_name()
    require(path.startswith(DEST + '/'), 'Out-of-scope save: ' + path)
    require(ASSETS.save_asset(path, only_if_is_dirty=True), 'Save: ' + path)
    report['saved'].append(path)


def scratch(emitter, stage, name, inputs, outputs, code):
    result = SP.create_scratch_module(SYSTEM, emitter, stage, name)
    require(result.success, 'Create scratch ' + name)
    module = str(result.module_name)
    hlsl = SP.add_custom_hlsl_node(SYSTEM, emitter, module, code)
    require(hlsl.success, 'HLSL ' + name)
    node = str(hlsl.node_id)
    read = SP.add_node(SYSTEM, emitter, module, 'MapGet')
    require(read.success, 'MapGet ' + name)
    read_id = str(read.node_id)
    input_node = next(n for n in SP.list_nodes(SYSTEM, emitter, module)
                      if str(n.node_type) in ('Input', 'NiagaraNodeInput') or str(n.node_type).endswith('NodeInput'))
    input_id = str(input_node.node_id)
    input_pin = next(str(p.pin_name) for p in SP.get_node_pins(SYSTEM, emitter, module, input_id)
                     if str(p.direction).lower() == 'output')
    read_pin = next(str(p.pin_name) for p in SP.get_node_pins(SYSTEM, emitter, module, read_id)
                    if str(p.direction).lower() == 'input')
    require(SP.connect_pins(SYSTEM, emitter, module, input_id, input_pin, read_id, read_pin), 'Map read')
    for pin, kind, variable in inputs:
        require(SP.add_pin(SYSTEM, emitter, module, read_id, 'Output', kind, variable).success, 'Read ' + variable)
        require(SP.add_pin(SYSTEM, emitter, module, node, 'Input', kind, pin).success, 'Input ' + pin)
        require(SP.connect_pins(SYSTEM, emitter, module, read_id, variable, node, pin), 'Wire ' + pin)
    for pin, kind, variable in outputs:
        require(SP.add_pin(SYSTEM, emitter, module, node, 'Output', kind, pin).success, 'Output ' + pin)
        target = SP.add_module_output(SYSTEM, emitter, module, variable, kind)
        require(target.success, 'Map output ' + variable)
        require(SP.connect_pins(SYSTEM, emitter, module, node, pin, str(target.node_id), variable), 'Wire ' + variable)
    require(unreal.GuLiCombatEffectAuthoringLibrary.finalize_scratch_pins(ASSETS.load_asset(SYSTEM)), 'Finalize pins')
    require(SP.apply_changes(SYSTEM), 'Apply scratch')


def material(name, ribbon):
    path = DEST + '/' + name
    obj = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else TOOLS.create_asset(
        name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    require(obj, 'Create material')
    MAT.delete_all_material_expressions(obj)
    obj.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    obj.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    obj.set_editor_property('two_sided', True)
    MAT.set_material_usage(obj, unreal.MaterialUsage.MATUSAGE_NIAGARA_RIBBONS if ribbon else unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
    uv = MAT.create_material_expression(obj, unreal.MaterialExpressionTextureCoordinate, -600, 200)
    color = MAT.create_material_expression(obj, unreal.MaterialExpressionParticleColor, -600, -150)
    mask = MAT.create_material_expression(obj, unreal.MaterialExpressionCustom, -350, 200)
    mask.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    param = unreal.CustomInput(); param.set_editor_property('input_name', 'UV')
    mask.set_editor_property('inputs', [param])
    # Ribbon V spans its width; sprite X spans the nozzle width. No texture or scene reads.
    code = ('return pow(saturate(1-abs(UV.y*2-1)),2);' if ribbon else
            'float2 p=UV*2-1; float axial=pow(saturate(1-abs(p.y)),0.6); '
            'float core=pow(saturate(1-abs(p.x)*1.8),3); '
            'return axial*(core+0.28*pow(saturate(1-abs(p.x)),2));')
    mask.set_editor_property('code', code)
    MAT.connect_material_expressions(uv, '', mask, 'UV')
    opacity = MAT.create_material_expression(obj, unreal.MaterialExpressionMultiply, -100, 200)
    MAT.connect_material_expressions(mask, '', opacity, 'A')
    MAT.connect_material_expressions(color, 'A', opacity, 'B')
    emission = MAT.create_material_expression(obj, unreal.MaterialExpressionMultiply, -100, -100)
    emission.set_editor_property('const_b', 6.0 if ribbon else 12.0)
    MAT.connect_material_expressions(color, 'RGB', emission, 'A')
    MAT.connect_material_property(emission, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MAT.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY)
    MAT.layout_material_expressions(obj)
    MAT.recompile_material(obj)
    diagnostic = unreal.MaterialNodeService.get_material_diagnostics(path)
    report['materials'][path] = str(diagnostic)
    require(diagnostic.success and diagnostic.is_compiled_ok and not diagnostic.compile_errors, 'Material diagnostics: ' + str(diagnostic))
    save(obj)
    return path


def main():
    require(not unreal.WidgetService.is_pie_running(), 'Stop PIE before authoring')
    flame = material('M_WingmanEngineFlame', False)
    ribbon = material('M_WingmanFlightTrail', True)
    if not ASSETS.does_asset_exist(SYSTEM):
        require(NS.create_system('NS_WingmanFlightTrail', DEST).success, 'Create system')
    for emitter in list(NS.list_emitters(SYSTEM)):
        require(NS.remove_emitter(SYSTEM, str(emitter.emitter_name)), 'Remove owned emitter')
    for name, kind, value in [('Forward','Vector','(X=1,Y=0,Z=0)'), ('Throttle','Float','1'),
                              ('Right','Vector','(X=0,Y=1,Z=0)'), ('NozzleSpacing','Float','174'),
                              ('Opacity','Float','1'), ('Tint','Color','(R=0.12,G=0.55,B=1,A=1)')]:
        if not any(str(p.parameter_name) == 'User.' + name for p in NS.list_parameters(SYSTEM)):
            require(NS.add_user_parameter(SYSTEM, name, kind, value), 'User parameter ' + name)
        # VibeUE 4.0 add_user_parameter uses labeled vectors; set_parameter uses numeric tuples.
        set_value = ('1,0,0' if name == 'Forward' else '0,1,0') if kind == 'Vector' else value
        require(NS.set_parameter(SYSTEM, 'User.' + name, set_value), 'Default ' + name)
    shared_inputs = [('Owner','Position','Engine.Owner.Position'), ('Forward','Vector','User.Forward'),
                     ('Right','Vector','User.Right'), ('Spacing','float','User.NozzleSpacing'),
                     ('Throttle','float','User.Throttle'), ('Opacity','float','User.Opacity'), ('Tint','Color','User.Tint')]
    for name, rate, life, side in [('LeftEngineFlame',20,0.1,-1), ('RightEngineFlame',20,0.1,1),
                                    ('LeftFlightRibbon',50,0.45,-1), ('RightFlightRibbon',50,0.45,1)]:
        emitter = require(NS.add_emitter(SYSTEM, TEMPLATE, name), 'Add emitter')
        for module in list(EM.list_modules(SYSTEM, emitter)):
            if str(module.module_name) not in ('EmitterState','SpawnRate','InitializeParticle','ParticleState'):
                require(EM.remove_module(SYSTEM, emitter, str(module.module_name)), 'Strip template module')
        require(NS.set_rapid_iteration_param_by_stage(SYSTEM, emitter, 'EmitterUpdate',
                f'Constants.{emitter}.SpawnRate.SpawnRate', str(rate)), 'Set spawn rate')
        outputs = [('Life','float','Particles.Lifetime'), ('Position','Position','Particles.Position'),
                   ('Velocity','Vector','Particles.Velocity'), ('Color','Color','Particles.Color'),
                   ('Size','vec2','Particles.SpriteSize'), ('Alignment','Vector','Particles.SpriteAlignment'),
                   ('Rotation','float','Particles.SpriteRotation'),
                   ('Width','float','Particles.RibbonWidth')]
        shape = ('float3 dir=Forward/max(length(Forward),0.001); '
                 f'Life={life}; Velocity=float3(0,0,0); Color=float4(Tint.rgb,Opacity); '
                 'Size=float2(150,1000*Throttle); Alignment=dir; Rotation=0; Width=110; ')
        nozzle = f'Owner+Right*Spacing*{side}.0'
        shape += f'Position={nozzle}-dir*Size.y*0.43;' if name.endswith('EngineFlame') else f'Position={nozzle};'
        scratch(emitter, 'ParticleSpawn', 'InitializeWingman' + name, shared_inputs, outputs, shape)
        if name.endswith('EngineFlame'):
            scratch(emitter, 'ParticleUpdate', 'FollowWingmanNozzle', shared_inputs,
                    [o for o in outputs if o[0] in ('Position','Color','Size','Alignment')],
                    'float3 dir=Forward/max(length(Forward),0.001); Size=float2(150,1000*Throttle); '
                    f'Alignment=dir; Position={nozzle}-dir*Size.y*0.43; Color=float4(Tint.rgb,Opacity);')
            require(EM.set_renderer_property(SYSTEM, emitter, 0, 'Material', flame), 'Flame material')
            require(EM.set_renderer_property(SYSTEM, emitter, 0, 'Alignment', 'CustomAlignment'), 'Flame alignment')
        else:
            require(EM.enable_renderer(SYSTEM, emitter, 0, False), 'Disable template sprite')
            require(EM.add_renderer(SYSTEM, emitter, 'Ribbon'), 'Ribbon renderer')
            require(EM.set_renderer_property(SYSTEM, emitter, 1, 'Material', ribbon), 'Ribbon material')
            scratch(emitter, 'ParticleUpdate', 'FadeWingmanRibbon',
                    [('Age','float','Particles.NormalizedAge'), ('Opacity','float','User.Opacity'), ('Tint','Color','User.Tint')],
                    [('Color','Color','Particles.Color'), ('Width','float','Particles.RibbonWidth')],
                    'float fade=saturate(1-Age); Color=float4(Tint.rgb,Opacity*fade*fade*0.65); Width=110*fade;')
    system = ASSETS.load_asset(SYSTEM)
    system.set_editor_property('bFixedBounds', True)
    system.set_editor_property('FixedBounds', unreal.Box(min=unreal.Vector(-25000,-25000,-25000), max=unreal.Vector(25000,25000,25000)))
    system.set_editor_property('max_pool_size', 0)  # The persistent component is owned by the Pawn pool.
    system.set_editor_property('pool_prime_size', 0)
    effect_path = DEST + '/FXT_WingmanFlight'
    effect = ASSETS.load_asset(effect_path) if ASSETS.does_asset_exist(effect_path) else TOOLS.create_asset(
        'FXT_WingmanFlight', DEST, unreal.NiagaraEffectType, unreal.NiagaraEffectTypeFactoryNew())
    settings = unreal.NiagaraSystemScalabilitySettings()
    settings.set_editor_property('cull_by_distance', True)
    settings.set_editor_property('max_distance', 180000.0)
    array = effect.get_editor_property('system_scalability_settings')
    array.set_editor_property('settings', [settings])
    effect.set_editor_property('system_scalability_settings', array)
    effect.set_editor_property('cull_reaction', unreal.NiagaraCullReaction.DEACTIVATE_IMMEDIATE_RESUME)
    effect.set_editor_property('allow_culling_for_local_players', True)
    effect.set_editor_property('update_frequency', unreal.NiagaraScalabilityUpdateFrequency.MEDIUM)
    system.set_editor_property('effect_type', effect)
    save(effect)
    result = NS.compile_with_results(SYSTEM)
    report['compile'] = {'success': bool(result.success), 'errors': list(result.errors), 'warnings': list(result.warnings)}
    require(result.success and not result.errors, 'Niagara compile: ' + str(result))
    save(SYSTEM)
    report['readback'] = {'summary': str(NS.summarize(SYSTEM)), 'settings': str(NS.get_all_editable_settings(SYSTEM)),
                          'emitters': {str(e.emitter_name): {'modules':[str(m) for m in EM.list_modules(SYSTEM,str(e.emitter_name))],
                           'renderers':[str(r) for r in EM.list_renderers(SYSTEM,str(e.emitter_name))]} for e in NS.list_emitters(SYSTEM)}}
    report['success'] = True


try:
    main()
except Exception:
    report['success'] = False
    report['error'] = traceback.format_exc()
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'build.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report, ensure_ascii=False))
