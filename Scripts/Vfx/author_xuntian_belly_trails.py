"""Author the world-space shell smoke and the silver seeker glow."""
from pathlib import Path
from runpy import run_path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
DEST = '/Game/GuLiStrike/FX/Xuntian/Belly'
TEMPLATE = '/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain'
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
ns, em, mat = unreal.NiagaraService, unreal.NiagaraEmitterService, unreal.MaterialEditingLibrary
scratch = run_path(str(ROOT/'Scripts/Vfx/niagara_scratch.py'))['scratch']


def require(condition, label):
    if not condition:
        raise RuntimeError(label)
    return condition


def make_material(name, glow):
    path = DEST + '/' + name
    obj = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    mat.delete_all_material_expressions(obj)
    obj.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE if glow else unreal.BlendMode.BLEND_TRANSLUCENT)
    obj.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    obj.set_editor_property('two_sided', True)
    mat.set_material_usage(obj, unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
    uv = mat.create_material_expression(obj, unreal.MaterialExpressionTextureCoordinate, -600, 200)
    color = mat.create_material_expression(obj, unreal.MaterialExpressionParticleColor, -600, -60)
    mask = mat.create_material_expression(obj, unreal.MaterialExpressionCustom, -350, 200)
    mask.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    inp = unreal.CustomInput()
    inp.set_editor_property('input_name', 'UV')
    mask.set_editor_property('inputs', [inp])
    code = ('float2 p=UV*2-1; return pow(saturate(1-length(p)),2.4);' if glow else
            'float2 p=UV*2-1; float a=atan2(p.y,p.x); float edge=0.85+0.08*sin(5*a+0.7)+0.05*cos(3*a); '
            'return 1-smoothstep(edge-0.42,edge,length(p));')
    mask.set_editor_property('code', code)
    opacity = mat.create_material_expression(obj, unreal.MaterialExpressionMultiply, -100, 200)
    require(mat.connect_material_expressions(uv, '', mask, 'UV'), name)
    require(mat.connect_material_expressions(mask, '', opacity, 'A'), name)
    require(mat.connect_material_expressions(color, 'A', opacity, 'B'), name)
    require(mat.connect_material_property(opacity, '', unreal.MaterialProperty.MP_OPACITY), name)
    require(mat.connect_material_property(color, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR), name)
    mat.recompile_material(obj)
    require(assets.save_loaded_asset(obj, False), name)
    return obj.get_path_name()


def build(glow):
    name = 'NS_XuntianSilverGlow' if glow else 'NS_XuntianBlackSmoke'
    system = DEST + '/' + name
    material = make_material('M_XuntianSilverGlow' if glow else 'M_XuntianBlackSmoke', glow)
    if not assets.does_asset_exist(system):
        require(ns.create_system(name, DEST).success, name)
    for old in ns.list_emitters(system):
        require(ns.remove_emitter(system, str(old.emitter_name)), name)
    emitter = require(ns.add_emitter(system, TEMPLATE, 'SilverGlow' if glow else 'WorldSmoke'), name)
    for module in list(em.list_modules(system, emitter)):
        if str(module.module_name) not in ('EmitterState', 'SpawnRate', 'InitializeParticle', 'ParticleState'):
            require(em.remove_module(system, emitter, str(module.module_name)), str(module.module_name))
    # Fountain is a world-space emitter. Update moves only the glow to the live head.
    rate = 35 if glow else 100
    require(ns.set_rapid_iteration_param_by_stage(system, emitter, 'EmitterUpdate',
            f'Constants.{emitter}.SpawnRate.SpawnRate', str(rate)), name)
    require(em.set_renderer_property(system, emitter, 0, 'Material', material), name)
    spawn_in = [('Owner','Position','Engine.Owner.Position'), ('Id','int','Particles.UniqueID')]
    spawn_out = [('Life','float','Particles.Lifetime'), ('Pos','Position','Particles.Position'),
                 ('Size','vec2','Particles.SpriteSize'), ('Color','Color','Particles.Color'), ('Angle','float','Particles.SpriteRotation')]
    if glow:
        spawn_in.append(('LODDistance','float','Engine.Owner.LODDistance'))
        setup = 'Life=LODDistance>=15000.0 ? 0.14 : (LODDistance>=8000.0 ? 0.08 : 0.05); Pos=Owner; Size=float2(178,178); Color=float4(3.2,3.65,4.0,0.92); Angle=0;'
    else:
        setup = ('float h=frac(sin((float(Id)+1)*13.739)*951.31); Life=2; Pos=Owner; '
                 'Size=float2(185,185); Color=float4(0.016,0.019,0.018,0.78); Angle=h*360;')
    scratch(system, emitter, 'ParticleSpawn', 'Initialize'+emitter, spawn_in, spawn_out, setup)
    if glow:
        scratch(system, emitter, 'ParticleUpdate', 'FollowShell', [spawn_in[0]], [spawn_out[1]], 'Pos=Owner;')
    else:
        scratch(system, emitter, 'ParticleUpdate', 'HoldThenFade', [('Age','float','Particles.Age')], [spawn_out[2],spawn_out[3]],
                'float t=saturate(Age-1); float w=185+110*saturate(Age*0.5); Size=float2(w,w); '
                'Color=float4(0.016,0.019,0.018,0.78*(1-t));')
    obj = assets.load_asset(system)
    extent = 350 if glow else 30000
    obj.set_editor_property('bFixedBounds', True)
    obj.set_editor_property('FixedBounds', unreal.Box(min=unreal.Vector(-extent,-extent,-extent), max=unreal.Vector(extent,extent,extent)))
    obj.set_editor_property('max_pool_size', 96)
    result = ns.compile_with_results(system)
    require(result.success and not result.errors, name + ' ' + str(result.errors))
    require(assets.save_loaded_asset(obj, False), name)
    return obj


def build_trails():
    return build(False), build(True)
