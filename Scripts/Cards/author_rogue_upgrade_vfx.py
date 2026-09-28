"""Build only the project-owned rogue upgrade batch assets, through UE/VibeUE."""
import ast
import json
from pathlib import Path
import traceback
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/RogueCards/Upgrade'
DEST = '/Game/GuLiStrike/FX/RogueCards'
SYSTEM = DEST + '/NS_RogueUpgrade_Lite'
SOURCE = '/Game/Assets/VFX/NiagaraUpgradeGlow/Particles/P_UpgradeGlow04_Converted'
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
NS, EM, SP = unreal.NiagaraService, unreal.NiagaraEmitterService, unreal.NiagaraScratchPadService
MAT = unreal.MaterialEditingLibrary
report = {'success': False, 'saved': [], 'source': SOURCE, 'visual_review': 'user_pending'}


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def prop(value, name):
    return value.get_editor_property(name)


def save(path):
    require(path.startswith(DEST + '/'), 'Outside task scope: ' + path)
    require(ASSETS.save_asset(path, only_if_is_dirty=True), 'Save ' + path)
    report['saved'].append(path)


def short_mote_mask_code():
    # One soft glint per independent particle. The ground ring mask is unchanged.
    return ('float2 p=UV*2-1; '
            'if(Shape<.5){float r=length(p); return pow(saturate(1-abs(r-.78)*12),1.5)*.55*Alpha;} '
            'float2 q=p/float2(.84,.88); float g=saturate(1-dot(q,q)); '
            'float core=g*g; core=.18*core+.82*core*core; '
            'return core*(.84+.16*sin(Phase*17))*Alpha;')


def short_mote_update_code():
    # Stable per-slot/per-particle hashes: no random re-roll each frame and no
    # shared three-dot carrier. All lengths are cm before the card row's scale.
    return '''float t=saturate(Params.x); float s=Params.y;
float fade=saturate(t/.06)*(1-smoothstep(.60,1.0,t));
Position=Center; Size=float2(0,0); Color=float4(0,0,0,0);
Facing=float3(0,0,1); Alignment=float3(0,1,0); Rotation=0; Dynamic=float4(1,t,0,0);
if(Part==0){
 Position=Center+float3(0,0,4*s);
 Size=float2(320,320)*(.78+.32*t)*s;
 Color=float4(Tint.rgb*lerp(float3(1,1,1),float3(.5,1.28855,1.59278),t),Tint.a*fade);
 Dynamic=float4(0,t,0,0);
}else if(s>0 && Part<=Params.z){
 uint seed=(uint)Slot*747796405u+(uint)Part*2891336453u;
 uint4 h=seed+uint4(1013904223u,1664525u,374761393u,668265263u);
 h=(h^(h>>16u))*2246822519u; h=(h^(h>>13u))*3266489917u; h=h^(h>>16u);
 float4 r=float4(h&16777215u)*(1.0/16777216.0);
 uint4 k=h+uint4(1274126177u,1431374977u,42595009u,1597334677u);
 k=(k^(k>>16u))*2246822519u; k=(k^(k>>13u))*3266489917u; k=k^(k>>16u);
 float4 v=float4(k&16777215u)*(1.0/16777216.0);
 float delay=r.z*.30; float life=.42+.28*r.w; float age=t-delay;
 float phase=saturate(age/life);
 float alive=(age>=0 && age<life)?1.0:0.0;
 float particleFade=saturate(age/.04)*(1-smoothstep(.64,1.0,phase))*alive;
 float angle=r.x*6.2831853; float radius=55+110*sqrt(r.y);
 float2 radial=float2(cos(angle),sin(angle))*radius;
 float2 drift=(v.xy*2-1)*(8+27*v.z)*max(age,0);
 float z=12+95*v.z+(100+100*v.w)*max(age,0);
 Position=Center+float3(radial+drift,z)*s;
 Size=float2(4.5+2*v.x,5.5+3*v.y)*s*alive;
 float3 tint=Tint.rgb*lerp(float3(1,1.01008,.85694),float3(.25,.34904,1.01918),t);
 Color=float4(tint,Tint.a*fade*particleFade*(.75+.25*v.w));
 Rotation=v.z*6.2831853; Dynamic=float4(1,phase+r.w*3.0,0,0);
}'''


def mote_slot_assignment_code():
    return ('int ParticleIndex=ExecIndex(); Slot=ParticleIndex/19; Part=ParticleIndex%19; '
            'Life=1000000000.0; Size=float2(0,0); Tint=float4(0,0,0,0); Scale=float3(0,0,0);')


def material(name, mesh=False):
    path = DEST + '/' + name
    obj = unreal.load_asset(path) if ASSETS.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    require(obj, 'Create ' + path)
    MAT.delete_all_material_expressions(obj)
    obj.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    obj.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    obj.set_editor_property('two_sided', False)
    MAT.set_material_usage(obj, unreal.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES if mesh else unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
    uv = MAT.create_material_expression(obj, unreal.MaterialExpressionTextureCoordinate, -600, 150)
    color = MAT.create_material_expression(obj, unreal.MaterialExpressionParticleColor, -600, -180)
    dynamic = MAT.create_material_expression(obj, unreal.MaterialExpressionDynamicParameter, -600, 350)
    dynamic.set_editor_property('param_names', ['Shape', 'Phase', 'Unused2', 'Unused3'])
    mask = MAT.create_material_expression(obj, unreal.MaterialExpressionCustom, -230, 80)
    mask.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    inputs = []
    for name in ['UV', 'Dynamic', 'Alpha']:
        item = unreal.CustomInput(); item.set_editor_property('input_name', name); inputs.append(item)
    mask.set_editor_property('inputs', inputs)
    code = ('float h=saturate(UV.y); float edge=saturate(h*12)*pow(saturate(1-h),1.7); '
            'float band=pow(saturate(1-abs(h-frac(Phase*1.1))*6),2); '
            'return (edge*.14+band*.34)*Alpha;') if mesh else short_mote_mask_code()
    mask.set_editor_property('code', 'float Shape=Dynamic.x; float Phase=Dynamic.y; ' + code)
    require(MAT.connect_material_expressions(uv, '', mask, 'UV'), 'UV')
    require(MAT.connect_material_expressions(dynamic, 'RGBA', mask, 'Dynamic'), 'Dynamic')
    require(MAT.connect_material_expressions(color, 'A', mask, 'Alpha'), 'Alpha')
    require(MAT.connect_material_property(color, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR), 'Emissive')
    require(MAT.connect_material_property(mask, '', unreal.MaterialProperty.MP_OPACITY), 'Opacity')
    MAT.layout_material_expressions(obj); MAT.recompile_material(obj)
    diag = unreal.MaterialNodeService.get_material_diagnostics(path)
    report.setdefault('materials', {})[path] = {'compiled_ok': diag.is_compiled_ok, 'errors': list(diag.compile_errors)}
    require(diag.is_compiled_ok and not diag.compile_errors, 'Material diagnostics ' + path)
    save(path)
    return path


def audit_source():
    s = NS.summarize(SOURCE)
    report['source_emitters'] = []
    for e in s.emitter_names:
        renderer = EM.get_renderer_details(SOURCE, str(e), 0)
        values = NS.list_rapid_iteration_params(SOURCE, str(e))
        report['source_emitters'].append({'name': str(e), 'simulation': str(EM.get_emitter_properties(SOURCE, str(e)).sim_target),
            'material': renderer.material_path, 'mesh': renderer.mesh_path,
            'burst': [p.value for p in values if 'Spawn Count' in p.parameter_name]})
    cls = unreal.load_class(None, '/Script/Niagara.NiagaraDataInterfaceVectorCurve')
    report['source_color_curves'] = []
    for o in unreal.ObjectIterator(cls):
        p = o.get_path_name()
        if 'P_UpgradeGlow04_Converted' not in p or 'NiagaraGraph' not in p or 'VectorFromCurve_VectorCurve' not in p:
            continue
        report['source_color_curves'].append({'path': p, 'keys': {
            n: [[k.get_editor_property('Time'), k.get_editor_property('Value')] for k in o.get_editor_property(n).get_editor_property('Keys')]
            for n in ['XCurve', 'YCurve', 'ZCurve']}})


def build():
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(), 'PIE is running')
    audit_source()
    mesh_path = DEST + '/SM_UpgradeCylinder'
    if not ASSETS.does_asset_exist(mesh_path):
        require(ASSETS.duplicate_asset('/Game/Assets/VFX/NiagaraUpgradeGlow/Mesh/SM_cylinder', mesh_path), 'Duplicate glow cylinder')
    glow = material('M_UpgradeGlow', True)
    sprite = material('M_UpgradeRingMotes')
    mesh = unreal.load_asset(mesh_path); mesh.set_material(0, unreal.load_asset(glow)); save(mesh_path)
    if not ASSETS.does_asset_exist(SYSTEM):
        require(prop(NS.create_system('NS_RogueUpgrade_Lite', DEST), 'success'), 'Create system')
    for entry in list(NS.list_emitters(SYSTEM)):
        require(NS.remove_emitter(SYSTEM, str(prop(entry, 'emitter_name'))), 'Reset task-owned emitter')
    tree = ast.parse((ROOT / 'Scripts/build_commander_combat_effects.py').read_text(encoding='utf-8'))
    fn = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'scratch')
    exec(compile(ast.Module(body=[fn], type_ignores=[]), 'shared_combat_scratch_helper', 'exec'), globals())
    for name, is_mesh in [('UpgradeGlow', True), ('UpgradeRingMotes', False)]:
        emitter = require(NS.add_emitter(SYSTEM, '/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst.SimpleSpriteBurst', name), 'Add emitter')
        for entry in list(EM.list_modules(SYSTEM, emitter)):
            n = str(entry.module_name)
            if not any(n.startswith(prefix) for prefix in ['EmitterState', 'ParticleState', 'InitializeParticle']):
                require(EM.remove_module(SYSTEM, emitter, n), 'Remove template ' + n)
        require(EM.add_module(SYSTEM, emitter, '/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous', 'EmitterUpdate'), 'Add pool burst')
        require(NS.set_rapid_iteration_param_by_stage(SYSTEM, emitter, 'EmitterUpdate',
            f'Constants.{emitter}.SpawnBurst_Instantaneous.Spawn Count', '1024' if is_mesh else '19456'), 'Pool size')
        divisor = 1 if is_mesh else 19
        scratch(SYSTEM, emitter, 'ParticleSpawn', 'AssignUpgradeSlot', [],
            [('Slot', 'int', 'Particles.UpgradeSlot'), ('Part', 'int', 'Particles.UpgradePart'),
             ('Life', 'float', 'Particles.Lifetime'), ('Size', 'vec2', 'Particles.SpriteSize'),
             ('Tint', 'Color', 'Particles.Color'), ('Scale', 'Vector', 'Particles.Scale')],
            f'int ParticleIndex=ExecIndex(); Slot=ParticleIndex/{divisor}; Part=ParticleIndex%{divisor}; Life=1000000000.0; Size=float2(0,0); Tint=float4(0,0,0,0); Scale=float3(0,0,0);')
        reader = SP.create_scratch_module(SYSTEM, emitter, 'ParticleUpdate', 'ReadUpgradePool')
        require(reader.success, 'Reader scratch')
        require(unreal.GuLiCombatEffectAuthoringLibrary.wire_rogue_upgrade_pool_reader(
            unreal.load_asset(SYSTEM), unreal.load_object(None, str(reader.script_path))) is not None, 'Wire pool reader')
        require(SP.apply_changes(SYSTEM), 'Apply reader')
        common = 'float t=saturate(Params.x); float s=Params.y; float fade=saturate(t/.06)*(1-smoothstep(.60,1.0,t)); '
        if is_mesh:
            code = common + ('Position=Center+float3(0,0,3*s); Scale=float3(16.5,16.5,11.7)*s*float3(.85+.15*t,.85+.15*t,.18+.82*t); '
                'Color=float4(Tint.rgb*lerp(float3(.25,.4842,.75876),float3(.001946,.006562,.53559),t),Tint.a*fade); '
                'Dynamic=float4(0,t,0,0);')
            outputs = [('Position','Position','Particles.Position'), ('Scale','Vector','Particles.Scale'),
                       ('Color','Color','Particles.Color'), ('Dynamic','vec4','Particles.DynamicMaterialParameter')]
        else:
            code = short_mote_update_code()
            outputs = [('Position','Position','Particles.Position'), ('Size','vec2','Particles.SpriteSize'),
                ('Color','Color','Particles.Color'), ('Facing','Vector','Particles.SpriteFacing'),
                ('Alignment','Vector','Particles.SpriteAlignment'), ('Rotation','float','Particles.SpriteRotation'),
                ('Dynamic','vec4','Particles.DynamicMaterialParameter')]
        scratch(SYSTEM, emitter, 'ParticleUpdate', 'ShapeUpgradeGlow' if is_mesh else 'ShapeUpgradeRingMotes',
            [('Center','Position','Particles.UpgradeCenter'), ('Params','Vector','Particles.UpgradeParameters'),
             ('Tint','Color','Particles.UpgradeTint'), ('Part','int','Particles.UpgradePart'), ('Slot','int','Particles.UpgradeSlot')], outputs, code)
        if is_mesh:
            for idx in reversed(range(len(EM.list_renderers(SYSTEM, emitter)))):
                require(EM.remove_renderer(SYSTEM, emitter, idx), 'Remove sprite renderer')
            require(EM.add_renderer(SYSTEM, emitter, 'Mesh'), 'Add glow mesh renderer')
        else:
            require(EM.set_renderer_property(SYSTEM, emitter, 0, 'Material', sprite), 'Sprite material')
            require(EM.set_renderer_property(SYSTEM, emitter, 0, 'FacingMode', 'CustomFacingVector'), 'Ground-facing ring')
            require(EM.set_renderer_property(SYSTEM, emitter, 0, 'Alignment', 'CustomAlignment'), 'Sprite alignment')
    system = unreal.load_asset(SYSTEM)
    require(unreal.GuLiCombatEffectAuthoringLibrary.configure_rogue_upgrade_system(system) is not None, 'GPU configuration')
    system.set_editor_property('max_pool_size', 16)
    system.set_editor_property('pool_prime_size', 0)
    compile_result = NS.compile_with_results(SYSTEM)
    report['compile'] = {'success': compile_result.success, 'errors': list(compile_result.errors), 'warnings': list(compile_result.warnings)}
    report['native_diagnostics'] = unreal.GuLiCombatEffectAuthoringLibrary.get_rogue_upgrade_compile_diagnostics(system)
    require(compile_result.success and not compile_result.errors, 'Niagara compile')
    save(SYSTEM)
    report['readback'] = []
    for e in NS.summarize(SYSTEM).emitter_names:
        p = EM.get_emitter_properties(SYSTEM, str(e)); r = EM.get_renderer_details(SYSTEM, str(e), 0)
        report['readback'].append({'name': str(e), 'sim_target': str(p.sim_target), 'local_space': p.local_space,
            'bounds': str(p.calculate_bounds_mode), 'renderer': r.renderer_type, 'material': r.material_path, 'mesh': r.mesh_path,
            'modules': [str(m.module_name) for m in EM.list_modules(SYSTEM, str(e))]})
    report['user_parameters'] = [str(n) for n in NS.summarize(SYSTEM).user_parameter_names]
    report['success'] = True


try:
    build()
except Exception:
    report['error'] = traceback.format_exc()
finally:
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'asset-build.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'success': report['success'], 'error': report.get('error'), 'compile': report.get('compile'), 'saved': report['saved']}, ensure_ascii=False))
