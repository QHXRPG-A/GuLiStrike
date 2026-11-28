"""Wire all three operational Xuntian mounts to the shared ground-strike flight presentation.

Run after importing the Fields, Vfx and Texts tables. Use build_trails=True when rebuilding the owned Niagara graphs.
"""
import csv
import json
import os
import tempfile
from pathlib import Path
from runpy import run_path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
DEST = '/Game/GuLiStrike/FX/Xuntian/Belly'
WING = '/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundMissile'
CATALOG = '/Game/GuLiStrike/Ship/Build/DA_ShipBuild_V1'
assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)


def table(name):
    with (ROOT/'Data/Json'/('DT_GuLiStrike'+name+'.csv')).open(encoding='utf-8-sig',newline='') as src:
        return {r['Name']:r for r in csv.DictReader(src)}


def data_asset(name, cls):
    path = DEST+'/'+name
    if assets.does_asset_exist(path):
        obj = assets.load_asset(path)
        assert isinstance(obj, cls), path
        return obj
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', cls)
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,DEST,cls,factory)


def color(hex_value):
    values = [int(hex_value[i:i+2],16)/255 for i in (0,2,4)]
    return unreal.LinearColor(*[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in values], 1)


def set_values(obj, **values):
    for name, value in values.items():
        obj.set_editor_property(name, value)


def ensure_classic_explosion_scale(resource):
    path = resource.split('.', 1)[0]
    ns, em, sp = unreal.NiagaraService, unreal.NiagaraEmitterService, unreal.NiagaraScratchPadService
    parameter = 'User.Area_Scale'
    emitter, module = 'Explosion_Flipbook', 'ScaleXuntianFlipbook'
    graph = (path, emitter, module)
    # Camera-facing Niagara sprites use world-space dimensions even when the component is scaled.
    if not ns.parameter_exists(path, parameter):
        assert ns.add_user_parameter(path, 'Area_Scale', 'Float', '1.0')
        assert ns.set_parameter(path, parameter, '1.0')
    assert float(ns.get_parameter(path, parameter).current_value) == 1.0
    if module not in [str(m.module_name) for m in em.list_modules(path, emitter)]:
        assert sp.create_scratch_module(path, emitter, 'ParticleSpawn', module).success
        nodes = sp.list_nodes(*graph)
        reader = next(str(n.node_id) for n in nodes if str(n.node_type) == 'MapGet')
        source = next(str(n.node_id) for n in nodes if str(n.node_type) == 'Input')
        if not any(str(c.to_node_id) == reader and str(c.to_pin) == 'Source' for c in sp.list_connections(*graph)):
            assert sp.connect_pins(*graph, source, 'Input', reader, 'Source')
        op = sp.add_op_node(*graph, 'Numeric::Mul')
        assert op.success
        for pin, kind, variable in [('A', 'vec2', 'Particles.SpriteSize'), ('B', 'float', parameter)]:
            assert sp.add_pin(*graph, reader, 'Output', kind, variable).success
            assert sp.connect_pins(*graph, reader, variable, str(op.node_id), pin)
        out = sp.add_module_output(*graph, 'Particles.SpriteSize', 'vec2')
        assert out.success and sp.connect_pins(*graph, str(op.node_id), 'Result', str(out.node_id), 'Particles.SpriteSize')
        assert sp.apply_changes(path)
        compiled = ns.compile_with_results(path)
        assert compiled.success and not compiled.errors, compiled.errors
        assert assets.save_loaded_asset(assets.load_asset(path), False)
    links = {(str(c.from_pin), str(c.to_pin)) for c in sp.list_connections(*graph)}
    assert {('Input', 'Source'), ('Particles.SpriteSize', 'A'), (parameter, 'B'), ('Result', 'Particles.SpriteSize')} <= links


def configure_impact_visual(impact, explosion):
    reference_cm = float(explosion['RadiusAtUnitScaleCm'])
    assert reference_cm > 0, explosion['Name']
    system = explosion['ResourcePath'].split('.', 1)[0]
    assert unreal.NiagaraService.parameter_exists(system, 'User.Area_Scale'), system
    variants = list(impact.activation_variants)
    if (len(variants) == 1 and variants[0].vfx_id == int(explosion['Id']) and
            str(variants[0].scale_parameter_name) == 'User.Area_Scale' and variants[0].random_yaw and
            variants[0].maximum_lifetime == 5 and impact.visual_reference_radius == reference_cm):
        return False
    variant = unreal.GuLiEffectVisualVariant(vfx_id=int(explosion['Id']), scale_parameter_name='User.Area_Scale',
                                             random_yaw=True, maximum_lifetime=5)
    set_values(impact, visual_reference_radius=reference_cm, activation_variants=[variant])
    return True


def sync_impact_visuals():
    """Restore the local source-pack module and propagate imported radius changes to the three fields."""
    rows = json.loads((ROOT/'Data/Json/DT_GuLiStrikeVfx_Effects.json').read_text(encoding='utf8'))
    explosions = {row['Name']: row for row in rows if row['Name'].startswith('XuntianBellyExplosion')}
    if not explosions:
        return []
    ensure_classic_explosion_scale(explosions['XuntianBellyExplosion1']['ResourcePath'])
    updated = []
    for tier in (1, 2, 3):
        field = assets.load_asset(DEST+'/DA_XuntianBellyImpact'+str(tier))
        if field and configure_impact_visual(field, explosions['XuntianBellyExplosion'+str(tier)]):
            assert assets.save_loaded_asset(field, False)
            updated.append(field.get_name())
    return updated


def export_manifests():
    dest = ROOT/'Config/Hybrid'
    verified = {}
    with tempfile.TemporaryDirectory(dir=dest) as stage:
        for key,population,trial in [('200',200,False),('600',600,False),('NoSurvivors',600,True)]:
            filename = 'RuntimeManifest-'+key+'.json'
            path = Path(stage)/filename
            result = json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population,str(path),trial))
            assert result['success'], result
            result = json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(path)))
            assert result['success'], result
            document=json.loads(path.read_text(encoding='utf-8-sig'))
            rules = {r['id']:r for r in document['rules']}
            for tier,base in enumerate([80,100,120],1):
                field = 'XuntianBellyL'+str(tier)
                ground=next(r for k,r in rules.items() if k.startswith('settle/effect/runtime/') and k.endswith(field))
                assert float(ground['damage'])==base and abs(float(rules[ground['id']+'/air']['damage'])-base*.7)<.001, ground
            verified[filename]=len(rules)
        for filename in verified:
            os.replace(Path(stage)/filename,dest/filename)
    return verified


def author(build_trails=False):
    assert not unreal.WidgetService.is_pie_running()
    wing = assets.load_asset(WING)
    assert wing and wing.flight_presentation_profile and wing.ground_warning_style
    if build_trails:
        smoke,glow=run_path(str(ROOT/'Scripts/Vfx/author_xuntian_belly_trails.py'))['build_trails']()
    else:
        smoke=assets.load_asset(DEST+'/NS_XuntianBlackSmoke')
        glow=assets.load_asset(DEST+'/NS_XuntianSilverGlow')
    assert smoke and glow
    vfx=table('Vfx_Effects')
    ensure_classic_explosion_scale(vfx['XuntianBellyExplosion1']['ResourcePath'])
    fields=table('SpellFields_Fields')
    flight=wing.flight_presentation_profile
    catalog=assets.load_asset(CATALOG)
    groups=list(catalog.groups)
    report=[]
    for tier,style in enumerate(['Gold','Violet','Silver'],1):
        field='XuntianBellyL'+str(tier)
        row=fields[field]
        explosion=vfx['XuntianBellyExplosion'+str(tier)]
        assert isinstance(assets.load_asset(explosion['ResourcePath']),unreal.NiagaraSystem),explosion
        tint=color(vfx['GroundEnhanced'+style]['TrailTintSRGB'])
        radius=float(row['RadiusCentimeters'])
        impact=data_asset('DA_XuntianBellyImpact'+str(tier),unreal.GuLiSpellFieldDefinition)
        set_values(impact,config_id=field,timing=unreal.GuLiSpellFieldTiming.INSTANT,radius=radius,use_target_hit_centers=True,use_ship_hulls=True,
                   air_target_damage_multiplier=.7)
        configure_impact_visual(impact, explosion)
        profile=data_asset('DA_XuntianBellyFlight'+str(tier),unreal.GuLiProjectileFlightPresentationProfile)
        for prop in ('batch_system','legacy_core_diameter','maximum_trail_length','pulse_period','trail_fade_seconds'):
            profile.set_editor_property(prop,flight.get_editor_property(prop))
        set_values(profile,world_smoke=smoke,head_glow=glow if tier==3 else None,core_color=tint,trail_color=tint)
        shell=data_asset('DA_XuntianBellyShell'+str(tier),unreal.GuLiXuntianShellDefinition)
        # Copy the native struct before editing. Python exposes object-owned structs by reference.
        motion=unreal.GuLiProjectileMotionSettings()
        for prop in ('lift_seconds','minimum_lift_height','maximum_lift_height','lateral_offset','vertical_curve',
                     'longitudinal_curve','convergence_distance','turn_rate','sweep_radius'):
            motion.set_editor_property(prop,wing.motion.get_editor_property(prop))
        set_values(motion,speed=14000,maximum_lifetime=14)
        set_values(shell,level=tier,round_seconds=1 if tier==1 else 3,burst_interval=.5,field_row=field,
                   impact_field=impact,motion=motion,intercept_enemy_bodies=True,flight_vfx_id=wing.flight_vfx_id,
                   flight_presentation_profile=profile,ground_warning_style=wing.ground_warning_style,trail_fade_seconds=wing.trail_fade_seconds)
        for obj in (impact,profile,shell):
            assert assets.save_loaded_asset(obj,False),obj
        group=next(g for g in groups if str(g.configuration_id)=='Group_0'+str(tier))
        capability=unreal.GuLiShipCapabilityDefinition(capability_id='Turret',component_class=unreal.GuLiXuntianBellyBattery,configuration=shell)
        set_values(group,capabilities=[capability],executable=True,authoring_notes='')
        report.append({'tier':tier,'sockets':[str(m.socket_name) for m in group.mounts],'radius':radius,'damage':row['Damage'],
                       'air_damage':float(row['Damage'])*impact.air_target_damage_multiplier,'explosion':explosion['ResourcePath']})
    set_values(catalog,groups=groups,revision=max(3,catalog.revision))
    assert assets.save_loaded_asset(catalog,False)
    return {'tiers':report,'manifests':export_manifests()}
