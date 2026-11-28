"""Build the single red shell used by the radial Burning Burst gameplay event."""
import json
import sys
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0, str(ROOT / 'Scripts/Vfx'))
from niagara_scratch import require, scratch

BASE = '/Game/GuLiStrike/FX/GroundMech/Burning'
SYSTEM = BASE + '/NS_GroundBurningBurst'
MATERIAL = BASE + '/M_GroundBurningShell'
REFERENCE = '/Game/Assets/VFX/Buffs/BuffLoop/NS/Vfx_MagicShell'
REFERENCE_EMITTER = 'NE_MagicShield'
EMITTER = 'BurningShell'
RECIPE = 'RedMagicShell01'
EXPANSION_SECONDS = 0.25
FADE_SECONDS = 0.25
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
NS = unreal.NiagaraService
EM = unreal.NiagaraEmitterService
EDIT = unreal.MaterialEditingLibrary


def shell_material(noise):
    material = ASSETS.load_asset(MATERIAL) if ASSETS.does_asset_exist(MATERIAL) else None
    if not material:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            'M_GroundBurningShell', BASE, unreal.Material, unreal.MaterialFactoryNew())
    require(material, MATERIAL)
    if ASSETS.get_metadata_tag(material, 'GuLi.Burst.Recipe') == RECIPE:
        return material
    EDIT.delete_all_material_expressions(material)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', False)
    EDIT.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES)

    def node(cls, x, y, **properties):
        result = EDIT.create_material_expression(material, cls, x, y)
        for key, value in properties.items():
            result.set_editor_property(key, value)
        return result

    def connect(src, output, dest, pin):
        require(EDIT.connect_material_expressions(src, output, dest, pin), 'Material input ' + pin)

    uv = node(unreal.MaterialExpressionTextureCoordinate, -850, -170, u_tiling=6., v_tiling=1.)
    age = node(unreal.MaterialExpressionParticleRelativeTime, -850, -40)
    panner = node(unreal.MaterialExpressionPanner, -640, -155, speed_x=.01, speed_y=.15)
    connect(uv, '', panner, 'Coordinate')
    connect(age, '', panner, 'Time')
    sample = node(unreal.MaterialExpressionTextureSample, -440, -185, texture=noise)
    connect(panner, '', sample, 'UVs')
    rim = node(unreal.MaterialExpressionFresnel, -440, -15, exponent=2.4, base_reflect_fraction=.04)
    color = node(unreal.MaterialExpressionParticleColor, -440, 135)
    mask = node(unreal.MaterialExpressionCustom, -140, -40,
                output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                code='return Alpha * saturate(0.04 + 0.32*smoothstep(0.42,0.72,Noise) + 0.70*Rim);')
    pins = []
    for name in ('Noise', 'Rim', 'Alpha'):
        pin = unreal.CustomInput()
        pin.set_editor_property('input_name', name)
        pins.append(pin)
    mask.set_editor_property('inputs', pins)
    connect(sample, 'R', mask, 'Noise')
    connect(rim, '', mask, 'Rim')
    connect(color, 'A', mask, 'Alpha')
    require(EDIT.connect_material_property(color, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR), 'Emissive')
    require(EDIT.connect_material_property(mask, '', unreal.MaterialProperty.MP_OPACITY), 'Opacity')
    EDIT.recompile_material(material)
    diag = unreal.MaterialNodeService.get_material_diagnostics(MATERIAL)
    require(diag.success and diag.is_compiled_ok and not diag.compile_errors, str(diag))
    ASSETS.set_metadata_tag(material, 'GuLi.Burst.Recipe', RECIPE)
    require(ASSETS.save_loaded_asset(material, False), MATERIAL)
    return material


def build():
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(), 'Active PIE')
    dirty = {pkg.get_name() for pkg in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(not dirty.intersection((MATERIAL, SYSTEM)), 'Unsaved changes on the burning-shell assets')
    reference = EM.get_renderer_details(REFERENCE, REFERENCE_EMITTER, 0)
    mesh = require(unreal.load_asset(str(reference.mesh_path)), 'Reference shell mesh')
    source_material = require(unreal.load_asset(str(reference.material_path)), 'Reference shell material')
    noise = require(EDIT.get_material_instance_texture_parameter_value(source_material, 'Noise'), 'Reference shell noise')
    material = shell_material(noise)
    system = ASSETS.load_asset(SYSTEM) if ASSETS.does_asset_exist(SYSTEM) else None
    if not system:
        require(NS.create_system('NS_GroundBurningBurst', BASE).success, 'Create burning shell')
        system = require(ASSETS.load_asset(SYSTEM), SYSTEM)
    lifetime = EXPANSION_SECONDS + FADE_SECONDS
    timing = {'ExpandSeconds': EXPANSION_SECONDS, 'FadeSeconds': FADE_SECONDS, 'Lifetime': lifetime}
    rebuild = ASSETS.get_metadata_tag(system, 'GuLi.Burst.Recipe') != RECIPE
    defaults = [(key, 'Float', str(seconds)) for key, seconds in timing.items()]
    if rebuild:
        defaults.extend([('Radius','Float','500'), ('StartRadius','Float','100'), ('InitialAge','Float','0'), ('Seed','Int','0')])
    present = {str(p.parameter_name) for p in NS.list_parameters(SYSTEM)}
    for key, kind, value in defaults:
        if 'User.' + key not in present:
            require(NS.add_user_parameter(SYSTEM, key, kind, value), key)
        if float(NS.get_parameter(SYSTEM, 'User.' + key).current_value) != float(value):
            require(NS.set_parameter(SYSTEM, 'User.' + key, value), key)
    if rebuild:
        for old in NS.list_emitters(SYSTEM):
            require(NS.remove_emitter(SYSTEM, str(old.emitter_name)), 'Remove superseded emitter')
        template = '/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain'
        require(str(NS.add_emitter(SYSTEM, template, EMITTER)) == EMITTER, 'Add shell emitter')
        keep = {'EmitterState', 'InitializeParticle', 'ParticleState'}
        for module in list(EM.list_modules(SYSTEM, EMITTER)):
            name = str(module.module_name)
            if name not in keep:
                require(EM.remove_module(SYSTEM, EMITTER, name), 'Remove template module ' + name)
        require(EM.add_module(SYSTEM, EMITTER, '/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous', 'EmitterUpdate'), 'One shell')
        for param in ('Life Cycle Mode', 'Loop Behavior'):
            require(EM.set_module_input(SYSTEM, EMITTER, 'EmitterState', param, 'NewEnumerator1'), param)
        require(NS.set_rapid_iteration_param_by_stage(SYSTEM, EMITTER, 'EmitterUpdate',
                f'Constants.{EMITTER}.SpawnBurst_Instantaneous.Spawn Count', '1'), 'Shell count')
        require(EM.remove_renderer(SYSTEM, EMITTER, 0), 'Remove template sprite')
        require(EM.add_renderer(SYSTEM, EMITTER, 'Mesh'), 'Shell renderer')
        for param, value in [('Meshes', f'((Mesh="{mesh.get_path_name()}"))'), ('bOverrideMaterials', 'True'),
                             ('OverrideMaterials', f'((ExplicitMat="{material.get_path_name()}"))')]:
            require(EM.set_renderer_property(SYSTEM, EMITTER, 0, param, value), param)
        require(unreal.GuLiCombatEffectAuthoringLibrary.configure_buff_burst_system(system) is not None, 'Local-space fixed bounds')
        box = mesh.get_bounding_box()
        extent = (box.max-box.min)*.5
        center = (box.max+box.min)*.5
        dimensions = ','.join(f'{v:.8f}' for v in extent.to_tuple())
        offset = ','.join(f'{v:.8f}' for v in center.to_tuple())
        # The project's red #A34053 in linear space, with emissive gain.
        red = [(((v/255.+.055)/1.055)**2.4)*4. for v in (0xA3, 0x40, 0x53)]
        tint = ','.join(f'{x:.8f}' for x in red)
        inputs = [('Clock','float','System.Age'),('InitialAge','float','User.InitialAge'),
                  ('Radius','float','User.Radius'),('Start','float','User.StartRadius'),
                  ('Expand','float','User.ExpandSeconds'),('Fade','float','User.FadeSeconds'),
                  ('Duration','float','User.Lifetime')]
        outputs = [('Pos','Position','Particles.Position'),('Size','Vector','Particles.Scale'),
                   ('Tint','Color','Particles.Color'),('Life','float','Particles.Lifetime'),('Alive','bool','Particles.Alive')]
        envelope = f'''float t=max(0.0,Clock+InitialAge);
float rise=max(0.001,Expand);
float limit=max(0.001,min(Duration,rise+max(Fade,0.0)));
float endRadius=max(0.0,Radius);
float u=saturate(t/rise);
float r=lerp(clamp(Start,0.0,endRadius),endRadius,u*u*(3.0-2.0*u));
Size=r/float3({dimensions});
Pos=-float3({offset})*Size;
float f=saturate((t-rise)/max(0.001,Fade));
float alpha=1.0-f*f*(3.0-2.0*f);
Tint=float4({tint},alpha);
Life=max(0.001,limit-max(InitialAge,0.0));
Alive=t<limit;'''
        for stage, name in [('ParticleSpawn','StartBurningShell'),('ParticleUpdate','ExpandBurningShell')]:
            scratch(SYSTEM, EMITTER, stage, name, inputs, outputs, envelope)
        system.set_editor_property('max_pool_size',128)
        system.set_editor_property('pool_prime_size',0)
        ASSETS.set_metadata_tag(system, 'GuLi.Burst.Recipe', RECIPE)
    loop_param = f'Constants.{EMITTER}.EmitterState.Loop Duration'
    require(NS.set_rapid_iteration_param_by_stage(SYSTEM, EMITTER, 'EmitterUpdate', loop_param, str(lifetime)), loop_param)
    result = NS.compile_with_results(SYSTEM)
    require(result.success and not result.errors, str(result))
    saved_timing = {key: float(NS.get_parameter(SYSTEM, 'User.' + key).current_value) for key in timing}
    require(saved_timing == timing, str(saved_timing))
    emitters = [str(e.emitter_name) for e in NS.list_emitters(SYSTEM) if e.is_enabled]
    require(emitters == [EMITTER], str(emitters))
    renderer = EM.get_renderer_details(SYSTEM, EMITTER, 0)
    require(str(renderer.mesh_path) == mesh.get_path_name() and str(renderer.material_path) == material.get_path_name(), str(renderer))
    require(ASSETS.save_loaded_asset(system, False), SYSTEM)
    return {'success':True,'asset':system.get_path_name(),'source':REFERENCE,'mesh':mesh.get_path_name(),
            'material':material.get_path_name(),'noise':noise.get_path_name(),'color_srgb':'#A34053',
            'emitters':emitters,'burst_count':1,'radius_cm':500,'expansion_seconds':saved_timing['ExpandSeconds'],
            'fade_seconds':saved_timing['FadeSeconds'],'lifetime_seconds':saved_timing['Lifetime'],
            'visual_acceptance':'pending_user'}


if __name__ == '__main__':
    unreal.MCPythonHelper.submit_result(json.dumps(build()))
