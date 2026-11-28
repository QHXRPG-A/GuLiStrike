"""Author in-system Niagara distance bands for the formal GuLiStrike combat effects."""
import json
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'ArtSource/FX/DistanceLOD_20261127'
NEAR = 8000.
FAR = 15000.
MAX_VIEW = 1.e8
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
SYSTEMS = unreal.NiagaraService
EMITTERS = unreal.NiagaraEmitterService

# A retained core has no upper distance limit. Names here are the enabled emitters
# from the formal systems, including authored spellings and ribbon dependencies.
# A None far entry extends the exact middle stack without a second emitter copy.
LAYERS = {
    'NS_Sparks': ('OmnidirectionalBurst','OmnidirectionalBurst'),
    'NS_Sparks_Smaller': ('OmnidirectionalBurst','OmnidirectionalBurst'),
    'NS_Trails': ('DirectionalBurst','DirectionalBurst'),
    'NS_Tornado_under': ('outSide_b_under MeshMain004 Smoke Gareki', None),
    'NS_Hit_1': ('Glow Flash RibbonCore RIbbonTrailFollower Sparks', 'Glow Flash'),
    'P_UpgradeGlow_Converted': ('mesh02 mesh03 circle001 Circle02 particles01 Particle02 light04', None),
    'NS_ConstructionComplete': ('mesh01 mesh02 UP01 mesh03 star01 circle01 Decal', 'mesh01 UP01 circle01'),
    'NS_ConstructionTopLoop': ('TailColumns FloatingMotes', 'TailColumns'),
    'NS_WingmanBombardment_01': ('NE_Cloud_01 NE_Cloud_02 NE_Core01 NE_CloudOut_01 NE_GlowOut NE_DustOut_01 NE_Ground_01', 'NE_Cloud_01 NE_Core01 NE_Ground_01'),
    'NS_WM01_Explosion_2A': ('Explosion_Flipbook OmnidirectionalBurst', 'Explosion_Flipbook'),
    'NS_WM01_MissileFlight': ('Core Exhaust', None),
    'NS_GroundMech_Blink': ('Strands ArmorFlecks', 'Strands'),
    'NS_GroundBurningBurst': ('BurningShell', None),
    'NS_GroundBurningStatus': ('Dots Smoke001', 'Dots'),
    'NS_GroundElectromagneticArc': ('Lightning glow', None),
    'NS_GroundExplosiveFlight_Toon': ('ProjectileShell ExhaustPlume', None),
    'NS_GroundExplosiveImpact': ('NE_Main_Explosion NE_Impact', 'NE_Main_Explosion'),
    'NS_GroundMech_Muzzle_A3': ('NE_gun_nozzle_fire NE_spread_spark', 'NE_gun_nozzle_fire'),
    'NS_GroundMech_RocketJet': ('NE_gun_nozzle_fire NE_forward_simple_bullet NE_spread_spark NE_forward_simple_bullet001', 'NE_gun_nozzle_fire NE_forward_simple_bullet'),
    'NS_NoSurvivorsCastFlash': ('Borders', 'Borders'),
    'NS_NoSurvivorsCastLightning': ('Lightning', None),
    'NS_NoSurvivorsFlight': ('NE_Thrusters NE_Smoke', 'NE_Thrusters'),
    'NS_NoSurvivorsImpact': ('NE_Cloud_01 NE_Cloud_02 NE_Core01 NE_GlowOut NE_DustOut_01 NE_Ground_01', 'NE_Cloud_01 NE_Core01 NE_Ground_01'),
    'NS_TransitFlash': ('TransitFlash', 'TransitFlash'),
    'NS_TransitTrail': ('TransitRibbon', 'TransitRibbon'),
    'NS_WingmanFlightTrail': ('LeftEngineFlame RightEngineFlame LeftFlightRibbon RightFlightRibbon', None),
    'NS_XuntianSilverGlow': ('SilverGlow', 'SilverGlow'),
}
COPIES = {
    **{name:'/Game/GuLiStrike/FX/ModelProjectiles/'+name for name in ('NS_Sparks','NS_Sparks_Smaller','NS_Trails')},
    'NS_Hit_1': '/Game/GuLiStrike/FX/GroundMech/Weapons/NS_GroundHit',
    'P_UpgradeGlow_Converted': '/Game/GuLiStrike/FX/GroundMech/Upgrades/NS_GroundUpgradeGlow',
    'NS_Tornado_under': '/Game/GuLiStrike/FX/GroundMech/Upgrades/NS_GroundMaxRankTornado',
}
# Emitters whose silhouette remains necessary also at lower detail. Split their
# emission, leaving the original full-detail graph untouched inside its near band.
# A tuple gives the two absolute values emitted by the medium and far copies.
DENSITY = {
    **{name:{'OmnidirectionalBurst':('SpawnBurst_Instantaneous.Spawn Count',90,24)} for name in ('NS_Sparks','NS_Sparks_Smaller')},
    'NS_Trails':{'DirectionalBurst':('SpawnRate.SpawnRate',150,36)},
    'NS_GroundMech_Blink': {'Strands': ('SpawnBurst_Instantaneous.Spawn Count',1152,576),'ArmorFlecks':('SpawnBurst_Instantaneous.Spawn Count',64,0)},
    'NS_GroundMech_Muzzle_A3': {'NE_gun_nozzle_fire':('SpawnBurst_Instantaneous.Spawn Count',7,4)},
    'NS_WingmanFlightTrail': {'LeftEngineFlame':('SpawnRate.SpawnRate',10,10),'RightEngineFlame':('SpawnRate.SpawnRate',10,10),
                             'LeftFlightRibbon':('SpawnRate.SpawnRate',24,24),'RightFlightRibbon':('SpawnRate.SpawnRate',24,24)},
    'NS_NoSurvivorsCastLightning': {'Lightning': ('SpawnRate.SpawnRate',30,30)},
    'NS_TransitTrail': {'TransitRibbon':('SpawnRate.SpawnRate',25,12)},
    'NS_XuntianSilverGlow': {'SilverGlow':('SpawnRate.SpawnRate',18,9)},
}
# A single-burst flash has exactly one particle. Preserve every flash, and make
# its medium/far sprite shader cheaper. These materials still consume the original
# per-particle color, motion, sprite size, opacity and lifetime.
FLASH = {'NS_TransitFlash':'TransitFlash','NS_NoSurvivorsCastFlash':'Borders'}
MATERIAL_DIR='/Game/GuLiStrike/FX/DistanceLOD'

def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value

def emitters(path):
    return {str(e.emitter_name):e for e in SYSTEMS.list_emitters(path)}

def ri(path, emitter, key, value):
    param=f'Constants.{emitter}.{key}'
    require(SYSTEMS.set_rapid_iteration_param_by_stage(path,emitter,'EmitterUpdate',param,str(value)),f'{path}: {param}')
    SYSTEMS.set_parameter(path,param,str(value))

def distance_band(path, emitter, low, high):
    # The enum is user-defined. UE stores its internal enumerator, not its display text.
    for key,value,display in (('Scalability Mode','NewEnumerator1','Self'),('Enable Distance Culling','true','true')):
        if EMITTERS.get_module_input(path,emitter,'EmitterState',key)!=display:
            require(EMITTERS.set_module_input(path,emitter,'EmitterState',key,value),f'{path}: {emitter} {key}')
        require(EMITTERS.get_module_input(path,emitter,'EmitterState',key)==display,f'{emitter} {key} readback')
    error=unreal.GuLiCombatEffectAuthoringLibrary.configure_distance_lod_limits(ASSETS.load_asset(path),emitter,low>0.,high<MAX_VIEW)
    require(not error,f'{path} {emitter}: {error}')
    ri(path,emitter,'EmitterState.MinDistance',float(low))
    ri(path,emitter,'EmitterState.MaxDistance',float(high))

def flash_material(tier):
    path=f'{MATERIAL_DIR}/M_Flash_{tier}'
    if ASSETS.does_asset_exist(path):
        return ASSETS.load_asset(path)
    m=unreal.AssetToolsHelpers.get_asset_tools().create_asset(path.rsplit('/',1)[1],MATERIAL_DIR,unreal.Material,unreal.MaterialFactoryNew())
    require(m,path)
    edit=unreal.MaterialEditingLibrary
    m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided',True)
    m.set_editor_property('used_with_niagara_sprites',True)
    uv=edit.create_material_expression(m,unreal.MaterialExpressionTextureCoordinate,-700,-100)
    color=edit.create_material_expression(m,unreal.MaterialExpressionParticleColor,-700,150)
    mask=edit.create_material_expression(m,unreal.MaterialExpressionCustom,-290,0)
    pins=[]
    for name in ('UV','Alpha'):
        p=unreal.CustomInput();p.set_editor_property('input_name',name);pins.append(p)
    mask.set_editor_property('inputs',pins)
    mask.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    if tier=='Reduced':
        code='float2 q=abs(2*UV-1); float halo=saturate(1-dot(q,q)); float cross=saturate(1-4*min(q.x,q.y)); return Alpha*halo*(0.45+0.55*cross);'
    else:
        code='float2 q=2*UV-1; return Alpha*saturate(1-dot(q,q));'
    mask.set_editor_property('code',code)
    require(edit.connect_material_expressions(uv,'',mask,'UV'),'UV')
    require(edit.connect_material_expressions(color,'A',mask,'Alpha'),'alpha')
    require(edit.connect_material_property(color,'RGB',unreal.MaterialProperty.MP_EMISSIVE_COLOR),'emissive')
    require(edit.connect_material_property(mask,'',unreal.MaterialProperty.MP_OPACITY),'opacity')
    edit.recompile_material(m)
    diag=unreal.MaterialNodeService.get_material_diagnostics(m.get_path_name())
    require(diag.success and diag.is_compiled_ok and not diag.compile_errors,str(diag))
    require(ASSETS.save_loaded_asset(m,False),path)
    return m

def configure_copy(path, source, tier, low, high, key=None, value=None, material=None):
    name=source+'_'+tier
    require(unreal.GuLiCombatEffectAuthoringLibrary.ensure_distance_lod_emitter(ASSETS.load_asset(path),source,name),f'Duplicate {path} {source}')
    distance_band(path,name,low,high)
    if key:ri(path,name,key,value)
    if material:
        require(EMITTERS.set_renderer_property(path,name,0,'Material',material.get_path_name()),f'{path} {name} material')
    if name=='ExhaustPlume_Reduced':
        # Reduce the ribbon geometry while preserving the moving shell and plume.
        require(EMITTERS.set_renderer_property(path,name,0,'TessellationMode','Disabled'),name)
    return name

def continuous_flight_core(path, emitter, module_prefix, original_life, middle_life, far_life):
    """Span the reduced emission periods for particles that follow a live nozzle or head."""
    sp = unreal.NiagaraScratchPadService
    module = next(str(m) for m in sp.list_scratch_modules(path, emitter) if str(m).startswith(module_prefix))
    nodes = list(sp.list_nodes(path, emitter, module))
    custom = next(str(n.node_id) for n in nodes if str(n.node_type) == 'CustomHlsl')
    code = sp.get_custom_hlsl_code(path, emitter, module, custom)
    target=(f'Life=LODDistance>={NEAR:.1f} ? {middle_life} : {original_life};' if middle_life==far_life else
            f'Life=LODDistance>={FAR:.1f} ? {far_life} : (LODDistance>={NEAR:.1f} ? {middle_life} : {original_life});')
    if target in code:
        return
    if 'LODDistance' not in code:
        read = next(str(n.node_id) for n in nodes if str(n.node_type) == 'MapGet' and
                    any(str(p.pin_name) == 'Engine.Owner.Position' for p in sp.get_node_pins(path, emitter, module, str(n.node_id))))
        for node, direction, param in ((read, 'Output', 'Engine.Owner.LODDistance'), (custom, 'Input', 'LODDistance')):
            present = {str(p.pin_name) for p in sp.get_node_pins(path, emitter, module, node)}
            if param not in present:
                require(sp.add_pin(path, emitter, module, node, direction, 'float', param).success, param)
        require(sp.connect_pins(path, emitter, module, read, 'Engine.Owner.LODDistance', custom, 'LODDistance'), 'Connect particle LOD distance')
    import re
    code,changed=re.subn(r'\bLife=(?:LODDistance[^;]+|'+re.escape(str(original_life))+r');',target,code,count=1)
    require(changed,'Unexpected continuous core lifetime: '+emitter)
    require(sp.set_custom_hlsl_code(path, emitter, module, custom, code), 'Silver core lifetime')
    require(unreal.GuLiCombatEffectAuthoringLibrary.finalize_scratch_pins(ASSETS.load_asset(path)), 'Finalize silver core')
    require(sp.apply_changes(path), 'Compile silver core')



def build_one(row):
    key=row['name'];source=row['path'];path=COPIES.get(key,source)
    if key=='NS_GroundBurningBurst':
        from runpy import run_path
        shell=run_path(str(ROOT/'Scripts/GroundMech/Rogue/author_burning_burst.py'))['build']()
        return {'path':path,'source':shell['source'],'bands_cm':{},'density':{},'medium':['BurningShell'],'far':['BurningShell'],
                'all_distances_identical':True,'shaders':False,'compile_errors':[]}
    obj=ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else ASSETS.duplicate_asset(source,path)
    require(obj and path.startswith('/Game/GuLiStrike/'),path)
    medium_names,far_names=LAYERS[key]
    shared_middle=far_names is None
    medium=set(medium_names.split())
    far=medium if shared_middle else set(far_names.split())
    base=set(row['enabled_emitters'])
    require(far and far<=medium<=base,(key,medium,far,base))
    if key=='NS_XuntianSilverGlow':
        continuous_flight_core(path,'SilverGlow','InitializeSilverGlow',.05,.08,.14)
    if key=='NS_WingmanFlightTrail':
        old_handles=emitters(path)
        migrate=ASSETS.get_metadata_tag(obj,'GuLi.Wingman.MiddleFar')!='SharedFlame014'
        for nozzle in ('LeftEngineFlame','RightEngineFlame'):
            continuous_flight_core(path,nozzle,'InitializeWingman'+nozzle,.1,.14,.14)
            # Duplicated Niagara emitters own inherited scratch copies. Rebuild the old
            # medium handles once, after updating the original flame lifetime.
            if migrate and nozzle+'_Reduced' in old_handles:
                require(SYSTEMS.remove_emitter(path,nozzle+'_Reduced'),'Refresh '+nozzle)
    if shared_middle:
        old=emitters(path)
        for e in base:
            retired=e+'_Minimal'
            if retired in old:
                require(SYSTEMS.remove_emitter(path,retired),'Remove superseded emitter '+retired)
    density=DENSITY.get(key,{})
    bands={}
    for e in sorted(base):
        rule=density.get(e)
        flash=e==FLASH.get(key)
        plume=(key=='NS_GroundExplosiveFlight_Toon' and e=='ExhaustPlume')
        if rule or flash or plume:
            # Duplicate before gating the original so all original parameter links
            # and render bindings are inherited. Only the density or shader changes.
            splits=[('Reduced',NEAR,MAX_VIEW,1)] if shared_middle else [('Reduced',NEAR,FAR,1),('Minimal',FAR,MAX_VIEW,2)]
            for tier,low,high,index in splits:
                if e not in (medium if index==1 else far):continue
                name=configure_copy(path,e,tier,low,high,rule[0] if rule else None,rule[index] if rule else None,
                                    flash_material(tier) if flash else None)
                bands[name]=[low,high]
            high=NEAR
        elif e in far:
            if not shared_middle:continue
            high=MAX_VIEW
        else:
            high=FAR if e in medium else NEAR
        distance_band(path,e,0,high)
        bands[e]=[0,high]
    if key=='NS_WingmanFlightTrail':
        ASSETS.set_metadata_tag(obj,'GuLi.Wingman.MiddleFar','SharedFlame014')
    require(bands, 'No distance controls: '+path)
    comp=SYSTEMS.compile_with_results(path)
    require(comp.success and not comp.errors,f'{path} {comp.errors}')
    require(ASSETS.save_loaded_asset(obj,False),'Save '+path)
    settings={str(q.setting_path):str(q.current_value) for q in SYSTEMS.get_all_editable_settings(path).rapid_iteration_parameters}
    for e,(low,high) in bands.items():
        for attr,expected in [('MinDistance',low),('MaxDistance',high)]:
            require(abs(float(settings[f'Constants.{e}.EmitterState.{attr}'])-expected)<1.,f'{path} {e} {attr}')
    return {'path':path,'source':source,'bands_cm':bands,'density':density,'medium':sorted(medium),'far':sorted(far),
            'shared_middle':shared_middle,'shaders':bool(FLASH.get(key)), 'compile_errors':list(comp.errors)}

def retarget_model_projectiles():
    """Replace the three object imports on the two existing projectile blueprints."""
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    options = unreal.AssetRegistryDependencyOptions()
    options.set_editor_property('include_hard_package_references', True)
    fx_names = ('NS_Sparks', 'NS_Sparks_Smaller', 'NS_Trails')
    bindings = {}
    for blueprint_name, expected in [('BP_Projectile_bullet', ('NS_Sparks',)), ('BP_Projectile_Rockets', fx_names)]:
        path = '/Game/MechaDestroyer/Blueprints/' + blueprint_name
        blueprint = ASSETS.load_asset(path)
        counts = {}
        for name in expected:
            original = ASSETS.load_asset('/Game/MechaDestroyer/FX/' + name)
            replacement = ASSETS.load_asset(COPIES[name])
            require(original and replacement, name)
            count = unreal.GuLiCombatEffectAuthoringLibrary.retarget_model_projectile_effect(blueprint, original, replacement)
            require(count >= 0, path + ' ' + name)
            counts[name] = count
        if any(counts.values()):
            unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
            require(ASSETS.save_loaded_asset(blueprint, False), path)
        registry.scan_modified_asset_files([str(ROOT / 'Content' / (path[6:] + '.uasset'))])
        dependencies = set(map(str,registry.get_dependencies(path, options)))
        for name in expected:
            require(COPIES[name] in dependencies and '/Game/MechaDestroyer/FX/'+name not in dependencies, (path, name, sorted(dependencies)))
        bindings[blueprint_name] = [COPIES[name] for name in expected]
    return bindings



def run(names=None):
    audit=json.loads((ROOT/'Progress/Evidence/20261127-默认对局特效LOD审计.json').read_text(encoding='utf8'))
    OUT.mkdir(parents=True,exist_ok=True)
    log=OUT/'authored-systems.json'
    result=json.loads(log.read_text(encoding='utf8')) if log.exists() else {}
    import traceback
    failed={}
    for row in audit['niagara_assets']:
        name=row['name']
        if name not in LAYERS or (names is not None and name not in names):continue
        try:
            result[name]=build_one(row)
        except Exception:
            failed[name]=traceback.format_exc()
        log.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
        (OUT/'author-errors.json').write_text(json.dumps(failed,ensure_ascii=False,indent=2),encoding='utf8')
    model_names={'NS_Sparks','NS_Sparks_Smaller','NS_Trails'}
    if not failed and model_names.issubset(result) and (names is None or model_names.intersection(names)):
        retarget_model_projectiles()
    return {'saved':len(result),'failed':failed}
