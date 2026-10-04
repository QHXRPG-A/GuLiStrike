"""Scoped authoring for the shared laser pool's commander-only particle lights.

Requires the updated native WireLaserPoolReader. Importing this module does not
modify assets; both the full laser builder and the scoped upgrade call it.
"""
import ast
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
SYSTEM = '/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanLaserPool'
LIGHT_PARAMETERS = ('User.LaserLightPositions', 'User.LaserLightColors',
                    'User.LaserLightRadii', 'User.LaserLightEnabled')


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def prop(value, key):
    return value.get_editor_property(key)


def configure(system_path=SYSTEM):
    """Modify only LaserBolts; retain sprite material, other emitters and pool settings."""
    require(system_path == SYSTEM, 'Unexpected laser-light destination')
    ns, em, sp = unreal.NiagaraService, unreal.NiagaraEmitterService, unreal.NiagaraScratchPadService
    emitter = 'LaserBolts'
    lights = [r for r in em.list_renderers(system_path, emitter) if str(prop(r, 'renderer_type')) == 'Light']
    require(len(lights) <= 1, 'Multiple existing light renderers require inspection')
    if not lights:
        require(em.add_renderer(system_path, emitter, 'Light'), 'Add ground tracer light renderer')
    modules = list(em.list_modules(system_path, emitter))
    readers = [m for m in modules if str(prop(m, 'module_name')) == 'ReadLaserPool']
    require(len(readers) == 1, 'Expected one existing laser pool reader')
    system = require(unreal.load_asset(system_path), 'Load laser pool')
    reader = require(unreal.load_object(None, str(prop(readers[0], 'script_asset_path'))), 'Load laser pool reader')
    result = unreal.GuLiCombatEffectAuthoringLibrary.wire_laser_pool_reader(system, reader, False)
    require(result is not None, 'Wire ground light arrays')
    names = set(map(str, prop(ns.summarize(system_path), 'user_parameter_names')))
    require(set(LIGHT_PARAMETERS).issubset(names), 'Updated native module must be compiled and loaded before this upgrade')

    # Initialize to darkness before the first array update; a pooled empty slot never lights the origin.
    for module in modules:
        if str(prop(module, 'module_name')) == 'InitializeGroundTracerLight':
            require(em.remove_module(system_path, emitter, 'InitializeGroundTracerLight'), 'Refresh owned light initializer')
    tree = ast.parse((ROOT / 'Scripts/build_commander_combat_effects.py').read_text(encoding='utf-8'))
    function = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'scratch')
    scope = {'unreal': unreal, 'SP': sp, 'ASSETS': unreal.get_editor_subsystem(unreal.EditorAssetSubsystem),
             'require': require, 'prop': prop}
    exec(compile(ast.Module(body=[function], type_ignores=[]), 'combat_scratch_helper', 'exec'), scope)
    scope['scratch'](system_path, emitter, 'ParticleSpawn', 'InitializeGroundTracerLight', [],
                     [('Position', 'Position', 'Particles.LaserLightPosition'),
                      ('Color', 'Color', 'Particles.LaserLightColor'),
                      ('Radius', 'float', 'Particles.LightRadius'),
                      ('Enabled', 'bool', 'Particles.LightEnabled')],
                     'Position=float3(0,0,0); Color=float4(0,0,0,0); Radius=0.0; Enabled=false;')
    require(sp.apply_changes(system_path), 'Apply laser light reader')
    return readback()


def readback():
    ns, em = unreal.NiagaraService, unreal.NiagaraEmitterService
    summary = ns.summarize(SYSTEM)
    params = list(map(str, prop(summary, 'user_parameter_names')))
    require(set(LIGHT_PARAMETERS).issubset(params), 'Missing light parameter arrays')
    require(not prop(summary, 'has_gpu_emitters'), 'Light renderer requires the existing CPU simulation')
    require(sum(str(prop(r, 'renderer_type')) == 'Light' for r in em.list_renderers(SYSTEM, 'LaserBolts')) == 1,
            'Expected exactly one bolt light renderer')
    require(not any(str(prop(r, 'renderer_type')) == 'Light' for r in em.list_renderers(SYSTEM, 'LaserMuzzles')),
            'Wingman muzzle renderer unexpectedly changed')
    renderers = [r for r in unreal.ObjectIterator(unreal.NiagaraLightRendererProperties)
                 if r.get_path_name().startswith(SYSTEM + '.') and 'LaserBolts' in r.get_path_name()]
    require(len(renderers) == 1, 'Expected one serialized bolt light renderer')
    renderer = renderers[0]
    expected = {'bUseInverseSquaredFalloff': False, 'bAlphaScalesBrightness': True,
                'bAffectsTranslucency': False, 'bAllowMegaLights': False, 'bMegaLightsCastShadows': False,
                'RadiusScale': 1.0, 'DefaultExponent': 2.0, 'SpecularScale': 0.2, 'DiffuseScale': 1.0}
    settings = {key: prop(renderer, key) for key in expected}
    for key, value in expected.items():
        require(abs(float(settings[key]) - float(value)) < 0.00001, 'Incorrect light setting: ' + key)
    bindings = {key: prop(renderer, key).export_text() for key in
                ('PositionBinding', 'ColorBinding', 'RadiusBinding', 'LightRenderingEnabledBinding')}
    for key, attribute in {'PositionBinding': 'LaserLightPosition', 'ColorBinding': 'LaserLightColor',
                           'RadiusBinding': 'LightRadius', 'LightRenderingEnabledBinding': 'LightEnabled'}.items():
        require(attribute in bindings[key], 'Incorrect light binding: ' + key)
    return {'system': SYSTEM, 'parameters': params, 'renderer': renderer.get_path_name(),
            'settings': settings, 'bindings': bindings}
