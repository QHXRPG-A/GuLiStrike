"""Absolute, baseline-checked UE property migration; no mesh/source/vendor edits.

Native model-local child offsets are intentionally absent. Run once before the
source-engine rebuild and again afterwards for read-back/idempotence validation.
"""
import hashlib
import json
import math
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir())
exec((ROOT/'Scripts/Scale020/capture_editor_baseline.py').read_text(encoding='utf-8').split('def main():')[0])
BASE = json.loads((ROOT/'TestResults/Scale020/editor-baseline.json').read_text(encoding='utf-8'))
FX = json.loads((ROOT/'TestResults/Scale020/fx-blueprint-baseline.json').read_text(encoding='utf-8'))
ENTRIES = []
OBJECTS = {}
ASSETS = {}

def asset(path):
    if path not in ASSETS:
        rows=unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_package_name(path)
        ASSETS[path]=next((r.get_asset() for r in rows if str(r.asset_class_path.asset_name)!='ObjectRedirector'),None)
        if not ASSETS[path]:
            raise RuntimeError('Asset unavailable: '+path)
    return ASSETS[path]

def scaled(v, factor=.2):
    return [scaled(x,factor) for x in v] if isinstance(v,list) else v*factor

def add(path, component, field, old, factor=.2, target=None, space='world_cm'):
    target = scaled(old,factor) if target is None else target
    if old == target:
        return
    ENTRIES.append(dict(asset=path, component=component, field=field, old=old,
                        target=target, space=space, applied_by='serialized_property'))

def fields(path, props, names, prefix='', component=None):
    for name in names.split():
        if name not in props:
            raise RuntimeError('Missing baseline field: '+path+' '+prefix+name)
        add(path,component,prefix+name,props[name])

def compile_manifest():
    for path, row in BASE['assets'].items():
        p, cls = row['properties'], row['class']
        if cls == 'GuLiWingmanFormationDefinition':
            fields(path,p,'agent_radius_centimeters catch_up_distance_centimeters catch_up_flight_speed_centimeters_per_second cruise_flight_speed_centimeters_per_second inner_ring_height_centimeters inner_ring_radius_centimeters maximum_acceleration_centimeters_per_second_squared maximum_deceleration_centimeters_per_second_squared minimum_flight_speed_centimeters_per_second obstacle_look_ahead_centimeters outer_ring_height_centimeters outer_ring_radius_centimeters recovery_distance_centimeters separation_radius_centimeters')
            fields(path,p['swarm_orbit'],'boundary_return_speed_centimeters_per_second curl_strength_centimeters_per_second hull_exclusion_radius_centimeters inner_soft_radius_centimeters noise_spatial_scale_centimeters outer_soft_radius_centimeters preferred_radius_return_speed_centimeters_per_second swirl_speed_max_centimeters_per_second swirl_speed_min_centimeters_per_second vertical_half_extent_centimeters vertical_return_speed_centimeters_per_second','swarm_orbit.')
        elif cls == 'GuLiWingmanWeaponDefinition':
            fields(path,p,'projectile_speed_centimeters_per_second range_centimeters sweep_radius_centimeters')
            fields(path,p['attack'],'air_fire_start_distance air_fire_stop_distance explosion_radius flight_speed muzzle pull_up_height strip_length','attack.')
        elif cls == 'GuLiCombatEffectCatalog':
            fields(path,p,'laser_core_width laser_length maximum_visual_distance muzzle_length muzzle_light_radius muzzle_width tracer_light_radius tracer_width')
            for i,mount in enumerate(p['mounts']):
                fields(path,mount,'aim_offset',f'mounts.{i}.')
                for j,v in enumerate(mount['muzzles']):
                    add(path,None,f'mounts.{i}.muzzles.{j}',v)
        elif cls == 'GuLiGroundWarningStyle':
            fields(path,p,'projection_depth')
        elif cls == 'GuLiProjectileEffectDefinition':
            fields(path,p,'visual_scale')
            fields(path,p['motion'],'convergence_distance lateral_offset maximum_lift_height minimum_lift_height speed sweep_radius','motion.')
        elif cls == 'GuLiPointSkillConfiguration':
            fields(path,p,'launch_offset target_area_diameter_centimeters')
        elif cls == 'GuLiResourceEconomyConfig':
            fields(path,p,'factory_dock_offset_centimeters factory_maneuver_speed_centimeters_per_second mining_distance_centimeters')
        elif cls == 'GuLiSpellFieldDefinition':
            fields(path,p,'radius')
            # New property is applied only after loading the new native class.
            if hasattr(unreal.GuLiSpellFieldDefinition,'visual_reference_radius'):
                add(path,None,'visual_reference_radius',800.0,target=p['radius'],space='unscaled_art_reference_cm')
    cmc_names = ('avoidance_consideration_radius braking_deceleration_falling braking_deceleration_flying '
        'braking_deceleration_swimming braking_deceleration_walking crouched_half_height gravity_scale '
        'max_acceleration max_custom_movement_speed max_fly_speed max_out_of_water_step_height max_step_height '
        'max_swim_speed max_walk_speed max_walk_speed_crouched min_analog_walk_speed perch_additional_height '
        'perch_radius_threshold jump_z_velocity network_max_smooth_update_distance network_no_smooth_update_distance')
    for path,row in FX['blueprints'].items():
        if '/Rollback/' in path:
            continue
        if row['kind'] == 'GuLiStrikeProjectile':
            add(path,None,'net_cull_distance_squared',row['properties']['net_cull_distance_squared'],factor=.04,space='world_cm_squared')
        for name,c in row['components'].items():
            p,cls = c['properties'],c['class']
            if cls == 'CapsuleComponent':
                fields(path,p,'capsule_radius capsule_half_height',component=name)
            elif cls == 'SphereComponent':
                fields(path,p,'sphere_radius relative_location',component=name)
            elif cls == 'SpringArmComponent':
                fields(path,p,'target_arm_length probe_size socket_offset target_offset relative_location camera_lag_max_distance',component=name)
            elif 'MovementComponent' in cls and cls != 'ProjectileMovementComponent':
                fields(path,p,' '.join(n for n in cmc_names.split() if n in p),component=name)
            elif cls == 'ProjectileMovementComponent':
                fields(path,p,'initial_speed max_speed homing_acceleration_magnitude projectile_gravity_scale',component=name)
            elif name in ('CharacterMesh0','Mesh'):
                fields(path,p,'relative_scale3d relative_location',component=name)
            elif cls == 'GuLiShipTargetingRangeComponent':
                fields(path,p,' '.join(n for n in p if n == 'targeting_radius_centimeters'),component=name)

def close(a,b):
    if isinstance(a,list) and isinstance(b,list):
        return len(a)==len(b) and all(close(x,y) for x,y in zip(a,b))
    if isinstance(a,(float,int)) and isinstance(b,(float,int)):
        return math.isclose(a,b,rel_tol=2e-6,abs_tol=1e-5)
    return a==b

def resolve(e):
    key = (e['asset'],e['component'])
    if key not in OBJECTS:
        if e['asset'] in FX['blueprints']:
            cdo = unreal.get_default_object(asset(e['asset']).generated_class())
            if not cdo:
                raise RuntimeError('Blueprint CDO unavailable: '+e['asset'])
            OBJECTS[(e['asset'],None)] = cdo
            for c in cdo.get_components_by_class(unreal.ActorComponent):
                OBJECTS[(e['asset'],c.get_name())] = c
        else:
            OBJECTS[key] = asset(e['asset'])
    return OBJECTS[key]

def get(obj,keys):
    for key in keys:
        obj = obj[int(key)] if key.isdigit() else obj.get_editor_property(key)
    return obj

def set_nested(obj, keys, new):
    key = keys[0]
    current = obj[int(key)] if key.isdigit() else obj.get_editor_property(key)
    if len(keys)>1:
        updated = set_nested(current, keys[1:], new)
    else:
        updated = unreal.Vector(*new) if isinstance(current,unreal.Vector) else new
    if key.isdigit():
        obj[int(key)] = updated
    else:
        obj.set_editor_property(key,updated)
    return obj

def main():
    compile_manifest()
    readback=ROOT/'TestResults/Scale020/editor-property-readback.json'
    previous=json.loads(readback.read_text(encoding='utf-8')) if readback.exists() else {}
    native_schema='scale020' if hasattr(unreal.GuLiSpellFieldDefinition,'visual_reference_radius') else 'legacy'
    errors=[]
    pending=[]
    rebased=[]
    for e in ENTRIES:
        current=value(get(resolve(e),e['field'].split('.')))
        if close(current,e['target']):
            continue
        # UE omits a value equal to the *old* native CDO during pre-rebuild saving.
        # The 4000 -> 800 fallback radius equalled that CDO (800), so it reloads as
        # the new default 160. Rebase this one evidenced transition, once only.
        if (native_schema=='scale020' and previous.get('native_schema','legacy')=='legacy'
            and e['asset'] in previous.get('saved',[]) and e['field']=='radius'
            and e['old']==4000.0 and e['target']==800.0 and close(current,160.0)):
            pending.append(e)
            rebased.append({'entry':e,'reloaded_native_default':current})
        elif not close(current,e['old']):
            errors.append(dict(entry=e,current=current))
        else:
            pending.append(e)
    report={'version':1,'scale':.2,'entries':ENTRIES,'conflicts':errors,
            'baseline_sha256':hashlib.sha256((ROOT/'TestResults/Scale020/editor-baseline.json').read_bytes()).hexdigest()}
    out=ROOT/'TestResults/Scale020/editor-property-migration-v1.json'
    out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    if errors:
        raise RuntimeError('Preflight conflicts; no packages written: '+json.dumps(errors,ensure_ascii=False)[:5000])
    changed=set()
    for e in pending:
        set_nested(resolve(e),e['field'].split('.'),e['target'])
        if not close(value(get(resolve(e),e['field'].split('.'))),e['target']):
            raise RuntimeError('Read-back failure: '+str(e))
        changed.add(e['asset'])
    # A prior interrupted pass may already have the target in memory but not on disk.
    save_paths={e['asset'] for e in ENTRIES}
    for path in sorted(save_paths):
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset(path),only_if_is_dirty=False):
            raise RuntimeError('Cannot save '+path)
    result={'entries':len(ENTRIES),'properties_changed':len(pending),'saved':sorted(save_paths),'conflicts':len(errors),
            'native_schema':native_schema,'recompiled_default_rebases':rebased}
    (ROOT/'TestResults/Scale020/editor-property-readback.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    return result

unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))
