"""Author the task-owned persistent laser particle pool through UE5.7/VibeUE.

Run via Scripts/ue_exec.py after compiling the native laser array reader.
Only the laser System/material and its explicit catalog reference are saved.
"""
import ast
import json
from pathlib import Path
import traceback
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
DEST = '/Game/GuLiStrike/FX/WingmanWeapons'
SYSTEM = DEST + '/NS_WingmanLaserPool'
MATERIAL = DEST + '/M_WingmanLaser'
CATALOG = '/Game/GuLiStrike/FX/CommanderWeapons/DA_CommanderCombatEffects'
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
NS, EM, SP = unreal.NiagaraService, unreal.NiagaraEmitterService, unreal.NiagaraScratchPadService
MAT = unreal.MaterialEditingLibrary
report = {'saved': [], 'success': False}


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def prop(value, name):
    return value.get_editor_property(name)


def save(path):
    require(path in (SYSTEM, MATERIAL, CATALOG), 'Unexpected save destination: ' + path)
    require(ASSETS.save_asset(path, only_if_is_dirty=True), 'Save ' + path)
    report['saved'].append(path)


def build_material():
    material = unreal.load_asset(MATERIAL) if ASSETS.does_asset_exist(MATERIAL) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_WingmanLaser', DEST, unreal.Material, unreal.MaterialFactoryNew())
    require(material, 'Create laser material')
    MAT.delete_all_material_expressions(material)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', True)
    MAT.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
    uv = MAT.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -600, 150)
    color = MAT.create_material_expression(material, unreal.MaterialExpressionParticleColor, -600, -150)
    glow = MAT.create_material_expression(material, unreal.MaterialExpressionCustom, -250, -100)
    glow.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    uv_input, tint_input = unreal.CustomInput(), unreal.CustomInput()
    uv_input.set_editor_property('input_name', 'UV')
    tint_input.set_editor_property('input_name', 'Tint')
    glow.set_editor_property('inputs', [uv_input, tint_input])
    glow.set_editor_property('code', 'float x=abs(UV.x*2-1); float core=pow(saturate(1-x*2),0.65); '
                             'float peak=max(Tint.r,max(Tint.g,Tint.b)); return Tint+peak*core*0.9;')
    MAT.connect_material_expressions(uv, '', glow, 'UV')
    MAT.connect_material_expressions(color, 'RGB', glow, 'Tint')
    MAT.connect_material_property(glow, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mask = MAT.create_material_expression(material, unreal.MaterialExpressionCustom, -250, 180)
    mask.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    alpha_input = unreal.CustomInput(); alpha_input.set_editor_property('input_name', 'Alpha')
    mask.set_editor_property('inputs', [uv_input, alpha_input])
    mask.set_editor_property('code', 'float2 p=abs(UV*2-1); return pow(saturate(1-p.x),1.3)*saturate((1-p.y)*12)*Alpha;')
    MAT.connect_material_expressions(uv, '', mask, 'UV')
    MAT.connect_material_expressions(color, 'A', mask, 'Alpha')
    MAT.connect_material_property(mask, '', unreal.MaterialProperty.MP_OPACITY)
    MAT.layout_material_expressions(material); MAT.recompile_material(material)
    report['material_diagnostics'] = str(unreal.MaterialNodeService.get_material_diagnostics(MATERIAL))
    save(MATERIAL)


def build_system():
    if not ASSETS.does_asset_exist(SYSTEM):
        require(prop(NS.create_system('NS_WingmanLaserPool', DEST), 'success'), 'Create laser pool system')
    for entry in list(NS.list_emitters(SYSTEM)):
        require(NS.remove_emitter(SYSTEM, str(prop(entry, 'emitter_name'))), 'Reset task-owned laser emitter')
    # Reuse the proven VibeUE scratch wiring helper without executing the Commander authoring pipeline.
    tree = ast.parse((ROOT / 'Scripts/build_commander_combat_effects.py').read_text(encoding='utf-8'))
    function = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'scratch')
    exec(compile(ast.Module(body=[function], type_ignores=[]), 'shared_combat_scratch_helper', 'exec'), globals())
    report['emitters'] = []
    for requested_name, muzzle in [('LaserBolts', False), ('LaserMuzzles', True)]:
        emitter = require(NS.add_emitter(SYSTEM, '/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst.SimpleSpriteBurst', requested_name), 'Add laser emitter')
        for entry in list(EM.list_modules(SYSTEM, emitter)):
            name = str(prop(entry, 'module_name'))
            if not any(name.startswith(prefix) for prefix in ('EmitterState', 'ParticleState')):
                require(EM.remove_module(SYSTEM, emitter, name), 'Remove template module ' + name)
        require(EM.add_module(SYSTEM, emitter, '/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous', 'EmitterUpdate'), 'Add one pool allocation burst')
        require(NS.set_rapid_iteration_param_by_stage(SYSTEM, emitter, 'EmitterUpdate',
                    f'Constants.{emitter}.SpawnBurst_Instantaneous.Spawn Count', '1024'), 'Set pool block capacity')
        scratch(SYSTEM, emitter, 'ParticleSpawn', 'InitializeLaserSlot',
                [('Index', 'int', 'Engine.ExecIndex')],
                [('Slot', 'int', 'Particles.LaserSlot'), ('Life', 'float', 'Particles.Lifetime'),
                 ('Position', 'Position', 'Particles.Position'), ('Velocity', 'Vector', 'Particles.Velocity'),
                 ('Size', 'vec2', 'Particles.SpriteSize'), ('Color', 'Color', 'Particles.Color'),
                 ('Rotation', 'float', 'Particles.SpriteRotation')],
                'Slot=Index; Life=1000000000.0; Position=float3(0,0,0); Velocity=float3(0,0,0); '
                'Size=float2(0,0); Color=float4(0,0,0,0); Rotation=0.0;')
        update = SP.create_scratch_module(SYSTEM, emitter, 'ParticleUpdate', 'ReadLaserPool')
        require(prop(update, 'success'), 'Create pool reader')
        result = unreal.GuLiCombatEffectAuthoringLibrary.wire_laser_pool_reader(
            unreal.load_asset(SYSTEM), unreal.load_object(None, str(prop(update, 'script_path'))), muzzle)
        require(result is not None, 'Wire laser array reader')
        SP.apply_changes(SYSTEM)
        require(EM.set_renderer_property(SYSTEM, emitter, 0, 'Material', MATERIAL), 'Assign laser material')
        require(EM.set_renderer_property(SYSTEM, emitter, 0, 'Alignment', 'CustomAlignment'), 'Set axial sprite alignment')
        report['emitters'].append({'name': emitter, 'modules': [str(x) for x in EM.list_modules(SYSTEM, emitter)]})
    system = unreal.load_asset(SYSTEM)
    system.set_editor_property('max_pool_size', 64)
    system.set_editor_property('pool_prime_size', 0)
    result = NS.compile_with_results(SYSTEM)
    report['compile'] = {'success': bool(prop(result, 'success')), 'errors': [str(x) for x in prop(result, 'errors')],
                         'warnings': [str(x) for x in prop(result, 'warnings')]}
    require(report['compile']['success'] and not report['compile']['errors'], 'Laser Niagara compile failed')
    report['settings'] = str(NS.get_all_editable_settings(SYSTEM))
    save(SYSTEM)
    catalog = require(unreal.load_asset(CATALOG), 'Load combat effect catalog')
    catalog.set_editor_property('wingman_laser_system', system)
    for key, value in [('laser_length', 3000.0), ('laser_core_width', 50.0), ('laser_intensity', 24.0), ('laser_muzzle_seconds', 0.05)]:
        catalog.set_editor_property(key, value)
    catalog.set_editor_property('friendly_laser_tint', unreal.LinearColor(0.05, 1.0, 0.12, 1.0))
    catalog.set_editor_property('enemy_laser_tint', unreal.LinearColor(1.0, 0.025, 0.015, 1.0))
    save(CATALOG)
    report['catalog'] = {key: catalog.get_editor_property(key) for key in
                         ['laser_length', 'laser_core_width', 'laser_intensity', 'laser_muzzle_seconds']}
    report['saved_summary'] = str(NS.summarize(SYSTEM))


try:
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop PIE before authoring')
    build_material()
    build_system()
    report['success'] = True
except Exception:
    report['error'] = traceback.format_exc()
finally:
    out = ROOT / 'TestResults/WingmanLaser'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'asset-build.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'success': report['success'], 'error': report.get('error'), 'compile': report.get('compile'), 'saved': report['saved']}, ensure_ascii=False))
