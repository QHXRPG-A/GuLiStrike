"""Build the GuLiStrike explosive round's stylized flight effect in the UE editor."""

import ast
import json
import traceback
from pathlib import Path

import unreal


ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
FOLDER = '/Game/GuLiStrike/FX/GroundMech/ExplosivePackage'
SYSTEM_NAME = 'NS_GroundExplosiveFlight_Toon'
SYSTEM = f'{FOLDER}/{SYSTEM_NAME}'
BODY_MESH = '/Game/GuLiStrike/FX/CommanderWeapons/SM_WM01_Missile.SM_WM01_Missile'
TEMPLATE = '/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain'
BURST = '/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous'
PER_UNIT = '/Niagara/Modules/Emitter/SpawnPerUnit.SpawnPerUnit'
BODY_MATERIAL = 'M_GroundExplosiveToonShell'
PLUME_MATERIAL = 'M_GroundExplosiveToonPlume'
PLUME_EMITTER = 'ExhaustPlume'
SPACING_CM = 120
TIP_SPAWN_GROUP = 1
TIP_LINK_ORDER = 1000000.
SHELL_MESH_SCALE = .27
NOZZLE_OFFSET_CM = 325. * SHELL_MESH_SCALE
MOVEMENT_LIMIT = 48000
DEFAULT_SPEED = 8400
DEFAULT_LIFETIME = 120000 / DEFAULT_SPEED
DEFAULT_HOLD = .12
DEFAULT_FADE = .38
DEFAULT_SIZE = 2.

ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
NS = unreal.NiagaraService
EM = unreal.NiagaraEmitterService
SP = unreal.NiagaraScratchPadService
MAT = unreal.MaterialEditingLibrary

# Source colors are converted from the project palette to linear space.
PALE = 'FEE4D9'
SAND = 'D5C09C'
CHARCOAL = '2C3735'
AMBER = 'EE9D58'
RUST = '8E3A2A'
SMOKE = '835061'

SHELL_CODE = '''
float facing = dot(normalize(Normal * Side), normalize(float3(0.24,-0.48,0.84)));
float mid = smoothstep(-0.31,-0.25,facing);
float highlight = smoothstep(0.43,0.49,facing);
float3 shade = lerp(Dark, Light, 0.18);
float3 middle = lerp(Light, lerp(Shade,Dark,0.68), 0.62);
return lerp(lerp(shade, middle, mid), Light * 1.12, highlight);
'''

PLUME_CODE = '''
float age = ParticleData.g;
float3 n = normalize(Normal);
float facing = saturate(abs(dot(n,normalize(View))));
float sky = saturate(n.z * 0.43 + 0.56);
float around = 6.2831853 * UV.y;
float flow = UV.x + 1.8 * age;
float curl = sin(6.2831853 * flow + 1.3 * sin(around + 0.8 * flow));
float fold = smoothstep(-0.52,0.77,curl + 0.28 * sin(around - 3.1 * flow));

// Only the warmth rolls across the surface. The main flame stays filled throughout.
float heat = 1.0 - smoothstep(0.040,0.145,age);
float smoke = smoothstep(0.108,0.235,age);
float interior = smoothstep(0.06,0.64,facing);
float hotFace = saturate(0.71 + 0.18 * interior + 0.085 * sky + 0.06 * fold);
float3 yellow = float3(Warm.r, min(Warm.r, Warm.g*2.08), Warm.b*0.22);
float3 gold = lerp(Warm,yellow,heat);
float3 edge = lerp(Rim,Warm,0.76 + 0.20 * heat);
float3 fire = lerp(edge,gold,hotFace) * (0.86 + (0.92 + 0.30 * interior) * heat);
fire *= lerp(0.87,1.09,lerp(0.57,fold,1.0-heat));

float3 warmGray = lerp(Dark,Rim,0.18);
float3 litSmoke = lerp(Smoke,Warm,0.17) * 0.71;
float3 smokeColor = lerp(warmGray,litSmoke,saturate(0.26 + 0.43 * sky + 0.24 * fold));
float alpha = lerp(0.96,0.34,smoke) * lerp(0.78,1.0,interior) * ParticleOpacity;
float3 emission = lerp(fire,smokeColor,smoke);
return float4(emission * alpha,alpha);
'''

DIRECTION = '''
float3 Dir = dot(Vel,Vel) > 1.0 ? normalize(Vel) : float3(1,0,0);
'''
FOLLOW_INPUTS = [
    ('Owner', 'Position', 'Engine.Owner.Position'),
    ('Vel', 'Vector', 'User.FlightVelocity'),
    ('Size', 'float', 'User.Size'),
    ('Active', 'float', 'User.FlightActive'),
    ('Duration', 'float', 'User.FlightLifetime'),
]
AGE = ('Age', 'float', 'Particles.Age')
LIFE = ('Life', 'float', 'Particles.Lifetime')
POSITION = ('Position', 'Position', 'Particles.Position')
COLOR = ('PackedColor', 'Color', 'Particles.Color')
ALIVE = ('Alive', 'bool', 'DataInstance.Alive')
WIDTH = ('Width', 'float', 'Particles.RibbonWidth')
DISTANCE = ('Distance', 'float', 'Particles.RibbonUVDistance')
IS_TIP = ('IsTip', 'bool', 'Particles.ExplosiveNozzlePoint')
TAIL_INPUTS = [('Hold', 'float', 'User.TrailHoldSeconds'), ('Fade', 'float', 'User.TrailFadeSeconds')]
PLUME_SHAPE = '''
float grow = saturate(PointAge / 0.061);
float swell = saturate((PointAge - 0.076) / 0.117);
grow = grow*grow*(3.0-2.0*grow);
swell = swell*swell*(3.0-2.0*swell);
float waves = 0.68*sin(0.0132*Distance-8.4*PointAge) + 0.32*sin(0.0071*Distance+12.5*PointAge);
float diameter = (17.0 + 52.0*grow + 25.0*swell) * (1.0 + (0.20*grow)*waves);
float taper = saturate((PointAge-Hold)/max(Fade,0.01));
float history = (1.0-taper)*(1.0-taper)*(1.0-0.44*taper);
Width = min(diameter,108.0) * (1.0-0.22*taper) * Size;
PackedColor = float4(1.0,PointAge,0.0,history);
'''


def require(value, description):
    if not value:
        raise RuntimeError(description)
    return value


def load_scratch_author():
    source = ROOT / 'Scripts/build_commander_combat_effects.py'
    tree = ast.parse(source.read_text(encoding='utf8'))
    definition = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                      and node.name == 'scratch')
    scope = dict(globals(), prop=lambda obj, name: obj.get_editor_property(name))
    exec(compile(ast.Module(body=[definition], type_ignores=[]), str(source), 'exec'), scope)
    return scope['scratch']


def palette_value(hex_color):
    srgb = [int(hex_color[index:index + 2], 16) / 255. for index in (0, 2, 4)]
    linear = [value / 12.92 if value <= .04045 else ((value + .055) / 1.055) ** 2.4
              for value in srgb]
    return unreal.LinearColor(*linear, 1.)


def material(name, code, colors, mesh=False):
    path = f'{FOLDER}/{name}'
    asset = ASSETS.load_asset(path) or TOOLS.create_asset(
        name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    require(asset, f'Create {path}')
    MAT.delete_all_material_expressions(asset)
    asset.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    asset.set_editor_property('two_sided', mesh)
    asset.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE
                              if mesh else unreal.BlendMode.BLEND_ALPHA_COMPOSITE)
    MAT.set_material_usage(asset, unreal.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES
                           if mesh else unreal.MaterialUsage.MATUSAGE_NIAGARA_RIBBONS)

    normal = MAT.create_material_expression(asset, unreal.MaterialExpressionPixelNormalWS, -680, 0)
    inputs = {'Normal': (normal, '')}
    if mesh:
        side = MAT.create_material_expression(asset, unreal.MaterialExpressionTwoSidedSign, -680, 130)
        inputs['Side'] = (side, '')
    else:
        uv = MAT.create_material_expression(asset, unreal.MaterialExpressionTextureCoordinate, -920, 0)
        color = MAT.create_material_expression(asset, unreal.MaterialExpressionParticleColor, -920, 130)
        camera = MAT.create_material_expression(asset, unreal.MaterialExpressionCameraVectorWS, -680, 130)
        inputs['UV'] = (uv, '')
        inputs['View'] = (camera, '')
        inputs['ParticleData'] = (color, 'RGB')
        # The age and fade come from separately typed particle color outputs.
        inputs['ParticleOpacity'] = (color, 'A')
    for index, (key, base_color) in enumerate(colors.items()):
        node = MAT.create_material_expression(asset, unreal.MaterialExpressionVectorParameter, -680, 290 + 180 * index)
        node.set_editor_property('parameter_name', key)
        node.set_editor_property('default_value', palette_value(base_color))
        inputs[key] = (node, 'RGB')

    custom = MAT.create_material_expression(asset, unreal.MaterialExpressionCustom, -280, 40)
    custom.set_editor_property('code', code)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3
                               if mesh else unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    pins = []
    for key in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property('input_name', key)
        pins.append(pin)
    custom.set_editor_property('inputs', pins)
    for key, (node, output) in inputs.items():
        require(MAT.connect_material_expressions(node, output, custom, key), f'{path}.{key}')

    emission = custom
    if not mesh:
        emission = MAT.create_material_expression(asset, unreal.MaterialExpressionComponentMask, 10, 40)
        opacity = MAT.create_material_expression(asset, unreal.MaterialExpressionComponentMask, 10, 200)
        for mask, channels in [(emission, 'rgb'), (opacity, 'a')]:
            for channel in 'rgba':
                mask.set_editor_property(channel, channel in channels)
            require(MAT.connect_material_expressions(custom, '', mask, ''), f'{path} output {channels}')
        require(MAT.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY), f'{path} opacity')
    require(MAT.connect_material_property(emission, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR), f'{path} color')
    MAT.recompile_material(asset)
    # This waits for the target platform's shader jobs. Graph diagnostics can
    # otherwise report success before the rendering shaders have compiled.
    stats = MAT.get_statistics(asset)
    require(stats.num_pixel_shader_instructions > 0, f'{path} has no compiled pixel shader')
    diagnostic = unreal.MaterialNodeService.get_material_diagnostics(asset.get_path_name())
    require(diagnostic.success and diagnostic.is_compiled_ok and not diagnostic.compile_errors,
            f'{path} diagnostics: {diagnostic}')
    require(ASSETS.save_loaded_asset(asset, False), f'Save {path}')
    return asset.get_path_name()


def set_rapid(emitter, module, input_name, value, system_copy=False):
    key = f'Constants.{emitter}.{module}.{input_name}'
    require(NS.set_rapid_iteration_param_by_stage(SYSTEM, emitter, 'EmitterUpdate', key, str(value)), f'Set {key}')
    if system_copy:
        # System Update and the emitter carry independent rapid-iteration values.
        require(NS.set_parameter(SYSTEM, key, str(value)), f'Set System Update {key}')


def add_emitter(name, burst=False):
    emitter = require(NS.add_emitter(SYSTEM, TEMPLATE, name), f'Create emitter {name}')
    for module in list(EM.list_modules(SYSTEM, emitter)):
        label = str(module.module_name)
        if label not in ('EmitterState', 'InitializeParticle', 'ParticleState'):
            require(EM.remove_module(SYSTEM, emitter, label), f'Remove {emitter}.{label}')
    for key, value in [('Life Cycle Mode', 'NewEnumerator1'),
                       ('Loop Behavior', 'NewEnumerator1' if burst else 'NewEnumerator0')]:
        require(EM.set_module_input(SYSTEM, emitter, 'EmitterState', key, value), f'{emitter}.{key}')
    require(not EM.get_emitter_properties(SYSTEM, emitter).local_space, f'{emitter} must be world-space')
    if burst:
        require(EM.add_module(SYSTEM, emitter, BURST, 'EmitterUpdate'), f'{emitter} burst')
        set_rapid(emitter, 'EmitterState', 'Loop Duration', 60.)
        set_rapid(emitter, 'SpawnBurst_Instantaneous', 'Spawn Count', 1)
        set_rapid(emitter, 'SpawnBurst_Instantaneous', 'Spawn Time', 0.)
    return emitter


def renderer(emitter, index, key, value):
    require(EM.set_renderer_property(SYSTEM, emitter, index, key, str(value)), f'{emitter} renderer {key}')


def add_body(scratch, shell):
    emitter = add_emitter('ProjectileShell', burst=True)
    geometry = [POSITION, ('Heading', 'Vector', 'Particles.Velocity'), ('MeshScale', 'Vector', 'Particles.Scale')]
    shape = DIRECTION + f'''
MeshScale = (Size * {SHELL_MESH_SCALE:.2f}).xxx;
Position = Owner - Dir * (165.0 * MeshScale.x);
Heading = Dir;
'''
    scratch(SYSTEM, emitter, 'ParticleSpawn', 'InitializeExplosiveShell', FOLLOW_INPUTS, [LIFE, ALIVE, *geometry],
            shape + 'Life=max(0.01,Duration); Alive=Active>0.5;')
    scratch(SYSTEM, emitter, 'ParticleUpdate', 'TrackExplosiveShell', [*FOLLOW_INPUTS, AGE], [ALIVE, *geometry],
            shape + 'Alive=Active>0.5 && Age<Duration;')
    require(EM.remove_renderer(SYSTEM, emitter, 0), 'Remove the shell emitter template sprite')
    require(EM.add_renderer(SYSTEM, emitter, 'Mesh'), 'Add shell mesh renderer')
    renderer(emitter, 0, 'Meshes', f'((Mesh="{BODY_MESH}"))')
    renderer(emitter, 0, 'FacingMode', 'Velocity')
    renderer(emitter, 0, 'bOverrideMaterials', 'True')
    renderer(emitter, 0, 'OverrideMaterials', f'((ExplicitMat="{shell}"))')


def add_plume(scratch, plume):
    emitter = add_emitter(PLUME_EMITTER, burst=True)
    set_rapid(emitter, 'SpawnBurst_Instantaneous', 'Spawn Group', TIP_SPAWN_GROUP, system_copy=True)
    require(EM.add_module(SYSTEM, emitter, PER_UNIT, 'EmitterUpdate'), 'Add distance-based spawning')
    for key, value in [('Spawn Spacing', SPACING_CM), ('Spawn Probability', 1.),
                       ('Max Movement Threshold', MOVEMENT_LIMIT)]:
        set_rapid(emitter, 'SpawnPerUnit', key, value, system_copy=True)
    nozzle = f'float3 Nozzle = Owner - Dir * ({NOZZLE_OFFSET_CM:.2f} * Size);\n'
    link_order = ('LinkOrder', 'float', 'Particles.RibbonLinkOrder')
    spawn_inputs = [*FOLLOW_INPUTS, *TAIL_INPUTS, ('Id', 'int', 'Particles.UniqueID'),
                    ('SpawnGroup', 'int', 'Emitter.SpawnGroup')]
    spawn_code = DIRECTION + nozzle + f'''
IsTip = SpawnGroup=={TIP_SPAWN_GROUP};
Position = Nozzle;
Distance = dot(Nozzle,Dir);
LinkOrder = IsTip ? {TIP_LINK_ORDER:.1f} : float(Id);
Life = IsTip ? max(Duration,0.01) : max(Hold+Fade,0.01);
Alive = Active>0.5;
float PointAge = 0.0;
'''
    scratch(SYSTEM, emitter, 'ParticleSpawn', 'PlaceExhaustPoint', spawn_inputs,
            [IS_TIP, POSITION, DISTANCE, link_order, LIFE, ALIVE, WIDTH, COLOR], spawn_code + PLUME_SHAPE)

    # Only the tip point follows the projectile. All previously deposited points
    # retain their position and age on their own clocks.
    previous_pos = ('StoredPosition', 'Position', POSITION[2])
    previous_dist = ('StoredDistance', 'float', DISTANCE[2])
    update_inputs = [*FOLLOW_INPUTS, *TAIL_INPUTS, AGE, IS_TIP, previous_pos, previous_dist]
    update_code = DIRECTION + nozzle + '''
Position = IsTip ? Nozzle : StoredPosition;
Distance = IsTip ? dot(Nozzle,Dir) : StoredDistance;
Alive = IsTip ? (Active>0.5 && Age<Duration) : Age<max(Hold+Fade,0.01);
float PointAge = IsTip ? 0.0 : Age;
'''
    scratch(SYSTEM, emitter, 'ParticleUpdate', 'AgeExhaustPoint', update_inputs,
            [POSITION, DISTANCE, ALIVE, WIDTH, COLOR], update_code + PLUME_SHAPE)

    require(EM.remove_renderer(SYSTEM, emitter, 0), 'Remove the plume emitter template sprite')
    require(EM.add_renderer(SYSTEM, emitter, 'Ribbon'), 'Add continuous 3D plume renderer')
    for key, value in [
        ('Material', f"/Script/Engine.Material'{plume}'"),
        ('Shape', 'Tube'), ('TubeSubdivisions', 10), ('MaxNumRibbons', 1),
        ('bLinkOrderUseUniqueID', 'False'),
        ('bUseMaterialBackfaceCulling', 'True'), ('bCastShadows', 'False'),
        ('TessellationMode', 'Custom'), ('bUseConstantFactor', 'True'), ('TessellationFactor', 2),
        ('UV0Settings', '(DistributionMode=TiledFromStartOverRibbonLength,LeadingEdgeMode=Locked,'
         'TrailingEdgeMode=Locked,TilingLength=500.0,Offset=(X=0,Y=0),Scale=(X=1,Y=1))'),
    ]:
        renderer(emitter, 0, key, value)


def build():
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),
            'Exit PIE before authoring Niagara assets')
    owned_packages = {SYSTEM, f'{FOLDER}/{BODY_MATERIAL}', f'{FOLDER}/{PLUME_MATERIAL}'}
    dirty = {package.get_name() for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(not (owned_packages & dirty) or globals().get('GULI_EXPLOSIVE_TOON_RETRY', False),
            f'Target assets already have unsaved edits: {owned_packages & dirty}')
    mesh = require(ASSETS.load_asset(BODY_MESH), 'Read existing finned projectile mesh')
    bounds = mesh.get_bounds()
    require(abs(bounds.origin.x - 2.5) < .1 and abs(bounds.box_extent.x - 162.5) < .1, 'Projectile mesh X bounds changed')

    shell = material(BODY_MATERIAL, SHELL_CODE, {'Light': PALE, 'Shade': SAND, 'Dark': CHARCOAL}, mesh=True)
    plume = material(PLUME_MATERIAL, PLUME_CODE, {'Warm': AMBER, 'Rim': RUST, 'Smoke': SMOKE, 'Dark': CHARCOAL})
    if not ASSETS.does_asset_exist(SYSTEM):
        require(NS.create_system(SYSTEM_NAME, FOLDER).success, 'Create stylized Niagara system')
    for emitter in NS.list_emitters(SYSTEM):
        require(NS.remove_emitter(SYSTEM, str(emitter.emitter_name)), f'Rebuild {emitter.emitter_name}')
    for name, kind, value in [
        ('FlightActive', 'Float', '1'),
        ('FlightVelocity', 'Vector', f'({DEFAULT_SPEED},0,0)'),
        ('FlightLifetime', 'Float', str(DEFAULT_LIFETIME)),
        ('TrailHoldSeconds', 'Float', str(DEFAULT_HOLD)),
        ('TrailFadeSeconds', 'Float', str(DEFAULT_FADE)),
        ('Size', 'Float', str(DEFAULT_SIZE)),
    ]:
        if not NS.parameter_exists(SYSTEM, f'User.{name}'):
            default = f'(X={DEFAULT_SPEED},Y=0,Z=0)' if kind == 'Vector' else value
            require(NS.add_user_parameter(SYSTEM, name, kind, default), f'Add User.{name}')
        require(NS.set_parameter(SYSTEM, f'User.{name}', value), f'Default User.{name}')

    scratch = load_scratch_author()
    add_body(scratch, shell)
    add_plume(scratch, plume)
    asset = require(ASSETS.load_asset(SYSTEM), SYSTEM)
    asset.set_editor_property('max_pool_size', 32)
    asset.set_editor_property('pool_prime_size', 0)
    asset.set_editor_property('bFixedBounds', True)
    reach = DEFAULT_SPEED * (DEFAULT_HOLD + DEFAULT_FADE)
    asset.set_editor_property('FixedBounds', unreal.Box(unreal.Vector(-reach-300.,-300.,-300.),
                                                       unreal.Vector(300.,300.,300.)))
    result = NS.compile_with_results(SYSTEM)
    require(result.success and not result.errors, f'{SYSTEM} compile: {result}')
    values = NS.get_all_editable_settings(SYSTEM).rapid_iteration_parameters
    for suffix, expected in [('SpawnPerUnit.Max Movement Threshold', MOVEMENT_LIMIT),
                             ('SpawnPerUnit.Spawn Spacing', SPACING_CM),
                             ('SpawnBurst_Instantaneous.Spawn Group', TIP_SPAWN_GROUP)]:
        key = f'Constants.{PLUME_EMITTER}.{suffix}'
        copies = [float(param.current_value) for param in values if str(param.setting_path) == key]
        require(len(copies) == 2 and all(value == expected for value in copies), f'{key}: {copies}')
    require(NS.save_system(SYSTEM), f'Save {SYSTEM}')
    package = asset.get_outer()
    reloaded, message = unreal.EditorLoadingAndSavingUtils.reload_packages(
        [package], unreal.ReloadPackagesInteractionMode.ASSUME_POSITIVE)
    require(reloaded, f'Reload {SYSTEM}: {message}')
    return {'ok': True, 'system': SYSTEM, 'materials': [shell, plume],
            'compile_warnings': list(map(str, result.warnings)), 'reloaded': reloaded}


if globals().get('GULI_EXPLOSIVE_TOON_BUILD', True):
    try:
        report = build()
    except Exception:
        report = {'ok': False, 'error': traceback.format_exc()}
    unreal.MCPythonHelper.submit_result(json.dumps(report))
