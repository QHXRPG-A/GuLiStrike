"""Import the two Ship attack tables and install the revision-4 Wingman V3 catalog.

Run in a stopped editor with Scripts/ue_exec.py. The Commander missile flight
effect remains shared; the requested marketplace explosion is duplicated into
the Wingman-owned FX directory before it is referenced. Only explicit targets
listed in the report are saved.
"""
import csv
import io
import json
import copy
import re
from pathlib import Path
import traceback
import unreal

import sys
from pathlib import Path
sys.path.insert(0, str(Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / "Scripts/Vfx"))
from vfx_registry import vfx_id, resource as vfx_resource, scale as vfx_scale, require_id, visual_variant

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
REPORT = ROOT / 'TestResults/WingmanAttack/deployment.json'
BASE = '/Game/GuLiStrike/Ship/Abilities'
COMMANDER_PROJECTILE = '/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_Missile'
WINGMAN_PROJECTILE = '/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundMissile'
WINGMAN_IMPACT_FIELD = '/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundExplosion'
WINGMAN_EXPLOSION_SOURCE = '/Game/AllExplosions/Niagara/Big/NS_Explosion_Big_17'
WINGMAN_EXPLOSION_SYSTEM = '/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanGroundExplosion_Big_17'
WINGMAN_EXPLOSION_VISUAL_SCALE = vfx_scale('WingmanBombardment')[0]
WINGMAN_TOON_EXPLOSION_SYSTEM = '/Game/GuLiStrike/FX/WingmanWeapons/StylizedExplosion/NS_WingmanGroundExplosion_Toon'
WINGMAN_REFERENCE_EXPLOSION_SYSTEM = vfx_resource('WingmanBombardment').split('.')[0]
WINGMAN_SHOCKWAVE_SYSTEM = '/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanGroundShockwave_Big_17'
WINGMAN_SHOCKWAVE_VISUAL_SCALE = 3.1
SHOCKWAVE_EMITTERS = {'refr_mesh', 'smoke_shockwave'}
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

def tag(name):
    container = unreal.GameplayTagContainer()
    container.import_text('(GameplayTags=("' + name + '"))')
    return list(unreal.GameplayTagLibrary.break_gameplay_tag_container(container))[0]

def asset(path, cls):
    found = unreal.load_asset(path)
    if found:
        if not isinstance(found, cls):
            raise RuntimeError('Unexpected asset class: ' + path)
        return found
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', cls)
    return TOOLS.create_asset(path.rsplit('/', 1)[1], path.rsplit('/', 1)[0], cls, factory)

def object_path(value):
    if value is None:
        return ''
    if hasattr(value, 'get_path_name'):
        return str(value.get_path_name())
    return str(value)

def canonical(path):
    return path + '.' + path.rsplit('/', 1)[1]

def require_close(label, actual, expected, tolerance=0.001):
    if abs(float(actual) - float(expected)) > tolerance:
        raise RuntimeError(f'Unknown {label}: actual={actual!r}, expected={expected!r}')

def require_existing_weapon(path, ground):
    weapon = unreal.load_asset(path)
    if weapon is None:
        return None
    if not isinstance(weapon, unreal.GuLiWingmanWeaponDefinition):
        raise RuntimeError('Unexpected asset class: ' + path)
    revision = int(weapon.get_editor_property('revision'))
    if revision not in ((1, 2) if ground else (1, 2, 3)):
        raise RuntimeError(f'Unknown weapon revision at {path}: {revision}')
    if weapon.get_editor_property('kind') != unreal.GuLiWingmanWeaponKind.BASIC_AUTOMATIC:
        raise RuntimeError('Unknown weapon kind at ' + path)
    handle = weapon.get_editor_property('attack_profile_row')
    expected_row = 'WingmanGroundMissile' if ground else 'WingmanMachineGun'
    if str(handle.get_editor_property('row_name')) != expected_row:
        raise RuntimeError(f'Unknown attack row at {path}: {handle}')
    projectile_path = object_path(weapon.get_editor_property('attack_projectile'))
    known_projectiles = {''}
    if ground:
        known_projectiles = {canonical(COMMANDER_PROJECTILE), canonical(WINGMAN_PROJECTILE)}
    if projectile_path not in known_projectiles:
        raise RuntimeError(f'Unknown projectile reference at {path}: {projectile_path}')
    if revision == 1 and ground and projectile_path != canonical(COMMANDER_PROJECTILE):
        raise RuntimeError('Revision 1 ground weapon is not the known Commander-projectile source')
    if revision == 2 and ground and projectile_path != canonical(WINGMAN_PROJECTILE):
        raise RuntimeError('Revision 2 ground weapon is not the migrated Wingman projectile')
    return weapon

def require_projectile_contract(projectile, commander):
    if not isinstance(projectile, unreal.GuLiProjectileEffectDefinition):
        raise RuntimeError('Unexpected projectile definition class: ' + object_path(projectile))
    if projectile.get_editor_property('flight_vfx_id') != vfx_id('MissileFlight'):
        raise RuntimeError('Unexpected missile flight VfxId')

def require_wingman_projectile_references(commander, wingman, allow_legacy_impact=False):
    if commander.get_editor_property('flight_vfx_id') != wingman.get_editor_property('flight_vfx_id'):
        raise RuntimeError('Wingman projectile no longer shares the Commander flight System')
    impact = object_path(wingman.get_editor_property('impact_field'))
    allowed = {canonical(WINGMAN_IMPACT_FIELD)}
    if allow_legacy_impact:
        allowed.add(object_path(commander.get_editor_property('impact_field')))
    if impact not in allowed:
        raise RuntimeError(f'Unknown Wingman impact field: {impact}')

def variant_signature(variant):
    return {
        'system': vfx_resource(variant.get_editor_property('vfx_id')),
        'scale': vfx_scale(variant.get_editor_property('vfx_id'))[0],
        'scale_parameter_name': str(variant.get_editor_property('scale_parameter_name')),
        'random_yaw': bool(variant.get_editor_property('random_yaw')),
        'maximum_lifetime': float(variant.get_editor_property('maximum_lifetime')),
        'additional_layers': [
            {'system': vfx_resource(layer.get_editor_property('vfx_id')),
             'scale': vfx_scale(layer.get_editor_property('vfx_id'))[0]}
            for layer in variant.get_editor_property('additional_layers')
        ],
    }

def expected_variant_signature():
    return {
        'system': canonical(WINGMAN_EXPLOSION_SYSTEM),
        'scale': WINGMAN_EXPLOSION_VISUAL_SCALE,
        'random_yaw': True,
        'maximum_lifetime': 3.0,
        'additional_layers': [{'system': canonical(WINGMAN_SHOCKWAVE_SYSTEM),
                               'scale': WINGMAN_SHOCKWAVE_VISUAL_SCALE}],
    }


def toon_variant_signature():
    """The approved four-emitter visual contract; never recreate the old extra wave."""
    return {
        'system': canonical(WINGMAN_TOON_EXPLOSION_SYSTEM),
        'scale': WINGMAN_EXPLOSION_VISUAL_SCALE,
        'random_yaw': True,
        'maximum_lifetime': 2.0,
        'additional_layers': [],
    }


def reference_variant_signature():
    return {'system': canonical(WINGMAN_REFERENCE_EXPLOSION_SYSTEM),
            'scale': WINGMAN_EXPLOSION_VISUAL_SCALE, 'scale_parameter_name': 'User.Area_Scale',
            'random_yaw': True, 'maximum_lifetime': 5.25, 'additional_layers': []}

def uses_toon_explosion(field):
    variants = list(field.get_editor_property('activation_variants')) if field else []
    return len(variants) == 1 and variant_signatures_match(
        variant_signature(variants[0]), toon_variant_signature())

def variant_signatures_match(actual, expected, tolerance=0.001):
    return (
        actual['system'] == expected['system']
        and actual.get('scale_parameter_name', 'None') == expected.get('scale_parameter_name', 'None')
        and abs(actual['scale'] - expected['scale']) <= tolerance
        and actual['random_yaw'] == expected['random_yaw']
        and abs(actual['maximum_lifetime'] - expected['maximum_lifetime']) <= tolerance
        and len(actual['additional_layers']) == len(expected['additional_layers'])
        and all(a['system'] == e['system'] and abs(a['scale'] - e['scale']) <= tolerance
                for a, e in zip(actual['additional_layers'], expected['additional_layers']))
    )

def require_wingman_impact_contract(field, allow_legacy_scale=False):
    if not isinstance(field, unreal.GuLiSpellFieldDefinition):
        raise RuntimeError('Unexpected Wingman impact-field class: ' + object_path(field))
    require_close('Wingman impact gameplay radius', field.get_editor_property('radius'), 800.0)
    require_close('Wingman impact art reference radius', field.get_editor_property('visual_reference_radius'), 4000.0)
    require_close('Wingman impact dissipation', field.get_editor_property('dissipation_seconds'), 3.0)
    if field.get_editor_property('timing') != unreal.GuLiSpellFieldTiming.INSTANT:
        raise RuntimeError('Wingman impact field is not Instant')
    variants = list(field.get_editor_property('activation_variants'))
    allowed = [reference_variant_signature()]
    actual = variant_signature(variants[0]) if len(variants) == 1 else None
    if actual is None or not any(variant_signatures_match(actual, item) for item in allowed):
        raise RuntimeError('Unknown Wingman impact visual variant')

def compile_niagara(path):
    result = unreal.NiagaraService.compile_with_results(path)
    errors = list(result.get_editor_property('errors'))
    if not bool(result.get_editor_property('success')) or errors:
        raise RuntimeError(f'Niagara compile failed for {path}: {errors}')
    return {
        'success': True,
        'errors': errors,
        'warnings': list(result.get_editor_property('warnings')),
        'emitters': [
            str(item.get_editor_property('emitter_name'))
            for item in unreal.NiagaraService.list_emitters(path)
        ],
    }

def partition_explosion_layers():
    """Separate component transforms preserve both shockwave graphs exactly, including their local-space scale inputs."""
    source_emitters = {str(x.emitter_name): bool(x.is_enabled)
                       for x in unreal.NiagaraService.list_emitters(WINGMAN_EXPLOSION_SOURCE)}
    if not SHOCKWAVE_EMITTERS.issubset(source_emitters):
        raise RuntimeError('The source no longer contains the known shockwave emitters')
    result = {}
    for path, wave_only in ((WINGMAN_EXPLOSION_SYSTEM, False), (WINGMAN_SHOCKWAVE_SYSTEM, True)):
        if not LIB.does_asset_exist(path) and not LIB.duplicate_asset(WINGMAN_EXPLOSION_SOURCE, path):
            raise RuntimeError('Could not duplicate ' + path)
        emitters = {str(x.emitter_name): bool(x.is_enabled) for x in unreal.NiagaraService.list_emitters(path)}
        if set(emitters) != set(source_emitters):
            raise RuntimeError('Unknown emitter layout in ' + path)
        expected = {name: enabled and ((name in SHOCKWAVE_EMITTERS) == wave_only)
                    for name, enabled in source_emitters.items()}
        for name, enabled in expected.items():
            if emitters[name] != enabled and not unreal.NiagaraService.enable_emitter(path, name, enabled):
                raise RuntimeError('Could not set emitter state: ' + name)
        result[path] = compile_niagara(path)
        actual = {str(x.emitter_name): bool(x.is_enabled) for x in unreal.NiagaraService.list_emitters(path)}
        if actual != expected:
            raise RuntimeError('Emitter partition readback mismatch in ' + path)
        result[path]['enabled_emitters'] = sorted(name for name, enabled in actual.items() if enabled)
    return result

def tag_name(value):
    return str(unreal.GameplayTagLibrary.get_tag_name(value))

def grant_signature(grant):
    return {
        'ability_id': tag_name(grant.get_editor_property('ability_id')),
        'slot': str(grant.get_editor_property('slot')),
        'weapon_slot_id': str(grant.get_editor_property('weapon_slot_id')),
        'skill_id': str(grant.get_editor_property('skill_id')),
        'input_tag': tag_name(grant.get_editor_property('input_tag')),
        'formation_definition': object_path(grant.get_editor_property('formation_definition')),
        'weapon_definition': object_path(grant.get_editor_property('weapon_definition')),
        'profile_revision': int(grant.get_editor_property('profile_revision')),
        'cooldown_group_id': str(grant.get_editor_property('cooldown_group_id')),
    }

def make_attack_grant(ground, weapon):
    suffix = 'GroundMissile' if ground else 'MachineGun'
    return unreal.GuLiShipAbilityGrant(
        ability_id=tag('Ship.Ability.Weapon.Wingman.' + suffix),
        slot=unreal.GuLiShipAbilitySlot.BASIC_WEAPON,
        weapon_slot_id='GroundWeapon' if ground else 'AirWeapon',
        skill_id='Wingman.' + suffix,
        weapon_definition=weapon,
    )

def row_handle_signature(handle):
    return (
        object_path(handle.get_editor_property('data_table')),
        str(handle.get_editor_property('row_name')),
    )

def values_match(actual, expected, tolerance=0.001):
    if isinstance(expected, dict):
        if isinstance(actual, dict):
            values = [actual.get(axis, 0.0) for axis in ('X', 'Y', 'Z')]
        else:
            values = [float(value) for value in re.findall(
                r'[-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?', str(actual))]
        return len(values) >= 3 and all(
            abs(float(values[index]) - float(expected[axis])) <= tolerance
            for index, axis in enumerate(('X', 'Y', 'Z'))
        )
    if isinstance(expected, bool):
        return bool(actual) == expected
    if isinstance(expected, (int, float)):
        try:
            return abs(float(actual) - float(expected)) <= tolerance
        except (TypeError, ValueError):
            return False
    return str(actual) == str(expected)

def table_rows_match(source_rows, exported_rows):
    source_by_name = {row['Name']: row for row in source_rows}
    exported_by_name = {row['Name']: row for row in exported_rows}
    if set(source_by_name) != set(exported_by_name):
        return False
    return all(
        key in exported_by_name[name]
        and values_match(exported_by_name[name][key], expected)
        for name, source in source_by_name.items()
        for key, expected in source.items()
    )

def legacy_rows(name, target_rows):
    rows = copy.deepcopy(target_rows)
    by_name = {row['Name']: row for row in rows}
    if name == 'DT_GuLiStrikeShip_Tuning':
        by_name['Dreadnought']['BaseMaxSpeed'] = 5400.0
        by_name['Dreadnought']['BaseAcceleration'] = 600.0
        by_name['Dreadnought']['YawRate'] = 40.0
    elif name == 'DT_GuLiStrikeShip_WingmanWeapons':
        machine = by_name['WingmanMachineGun']
        machine['Note'] = '三维往返缠斗；仅接近段机头射界满足时开火'
        machine['AttackPattern'] = 'AirDogfight'
        machine['CooldownSeconds'] = 0.5
        for row in by_name.values():
            row['AirFireStartDistanceCentimeters'] = 0.0
            row['AirFireStopDistanceCentimeters'] = 0.0
            row['AirBurstDurationSeconds'] = 0.0
            row['AirOrbitCooldownSeconds'] = 0.0
    return rows

def import_table(name):
    rows = json.loads((ROOT / ('Data/Json/' + name + '.json')).read_text(encoding='utf-8'))
    manifest = json.loads((ROOT / 'Data/Json/manifest.json').read_text(encoding='utf-8'))
    struct = unreal.find_object(None, manifest['tables'][name]['struct'])
    if not struct:
        raise RuntimeError('Generated row structure not loaded: ' + name)
    table_path = '/Game/GuLiStrike/Data/' + name
    existing = unreal.load_asset(table_path)
    if existing:
        existing_rows = json.loads(
            unreal.DataTableFunctionLibrary.export_data_table_to_json_string(existing))
        if table_rows_match(rows, existing_rows):
            return existing, existing_rows, False
        if not table_rows_match(legacy_rows(name, rows), existing_rows):
            raise RuntimeError('Unknown hand-edited DataTable values at ' + table_path)
    # Preserve every generated column, including optional fields that are absent
    # from the first row (for example the Dreadnought-only Note field).
    fields = []
    for row in rows:
        for key in row:
            if key not in fields:
                fields.append(key)
    def cell(value):
        if isinstance(value, dict):
            return '(X=%s,Y=%s,Z=%s)' % (value['X'], value['Y'], value['Z'])
        return str(value)
    stream = io.StringIO()
    writer = csv.writer(stream, lineterminator='\n')
    writer.writerow(fields)
    for row in rows:
        writer.writerow([cell(row.get(key, '')) for key in fields])
    sidecar = ROOT / ('TestResults/WingmanAttack/' + name + '.csv')
    sidecar.write_text(stream.getvalue(), encoding='utf-8')
    settings = unreal.CSVImportSettings()
    settings.set_editor_property('import_row_struct', struct)
    settings.set_editor_property('import_type', unreal.CSVImportType.ECSV_DATA_TABLE)
    factory = unreal.CSVImportFactory()
    factory.set_editor_property('automated_import_settings', settings)
    task = unreal.AssetImportTask()
    for key, value in dict(factory=factory, filename=str(sidecar), destination_path='/Game/GuLiStrike/Data',
                           destination_name=name, automated=True, replace_existing=True, save=False).items():
        task.set_editor_property(key, value)
    TOOLS.import_asset_tasks([task])
    table = unreal.load_asset(table_path)
    exported = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    if not table_rows_match(rows, exported):
        raise RuntimeError('Table readback mismatch after import: ' + name)
    return table, exported, True

def main():
    command_line = unreal.SystemLibrary.get_command_line().lower()
    b_commandlet = '-run=pythonscript' in command_line
    if not b_commandlet:
        level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if level.is_in_play_in_editor():
            raise RuntimeError('Stop PIE before asset deployment')
        # Orient before changing the live editor; no level actors are changed.
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    else:
        actors = []
    report = {'success': False, 'commandlet': b_commandlet, 'level_actor_count': len(actors),
              'saved': [], 'tables': {}, 'table_imported': {}}
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    touched = {BASE + '/DA_ShipAbilitySet_WingmanV3',
               BASE + '/Weapons/DA_WingmanWeapon_MachineGun', BASE + '/Weapons/DA_WingmanWeapon_GroundMissile',
               WINGMAN_PROJECTILE, WINGMAN_IMPACT_FIELD, WINGMAN_EXPLOSION_SYSTEM, WINGMAN_SHOCKWAVE_SYSTEM,
               '/Game/GuLiStrike/Ship/BP_GuLiStrikeShip', '/Game/GuLiStrike/Ship/BP_CombatAvatarFly01',
               '/Game/GuLiStrike/Data/DT_GuLiStrikeShip_Tuning',
               '/Game/GuLiStrike/Data/DT_GuLiStrikeShip_WingmanWeapons',
               '/Game/GuLiStrike/Data/DT_GuLiStrikeShip_WingmanTargeting'}
    conflicts = [str(p.get_name()) for p in dirty if str(p.get_name()) in touched]
    if conflicts:
        raise RuntimeError('Unsaved edits in deployment targets: ' + str(conflicts))
    old_set = unreal.load_asset(BASE + '/DA_ShipAbilitySet_WingmanV1')
    commander_projectile = unreal.load_asset(COMMANDER_PROJECTILE)
    if not old_set or not commander_projectile:
        raise RuntimeError('Existing formation catalog or WM01 missile is missing')
    require_projectile_contract(commander_projectile, commander=True)
    existing_impact = unreal.load_asset(WINGMAN_IMPACT_FIELD)
    if existing_impact: require_wingman_impact_contract(existing_impact)
    report['reference_niagara'] = compile_niagara(vfx_resource('WingmanBombardment'))

    machine_path = BASE + '/Weapons/DA_WingmanWeapon_MachineGun'
    ground_path = BASE + '/Weapons/DA_WingmanWeapon_GroundMissile'
    existing_machine = require_existing_weapon(machine_path, ground=False)
    existing_ground = require_existing_weapon(ground_path, ground=True)
    wingman_projectile = unreal.load_asset(WINGMAN_PROJECTILE)
    if wingman_projectile:
        require_projectile_contract(wingman_projectile, commander=False)
        require_wingman_projectile_references(
            commander_projectile, wingman_projectile, allow_legacy_impact=True)

    catalog_path = BASE + '/DA_ShipAbilitySet_WingmanV3'
    existing_catalog = unreal.load_asset(catalog_path)
    if existing_catalog:
        if not isinstance(existing_catalog, unreal.GuLiShipAbilitySet):
            raise RuntimeError('Unexpected catalog class: ' + catalog_path)
        if int(existing_catalog.get_editor_property('revision')) not in (3, 4):
            raise RuntimeError('Unknown Wingman V3 catalog revision')
        old_grants = list(old_set.get_editor_property('grants'))
        if not existing_machine or not existing_ground:
            raise RuntimeError('Wingman V3 catalog exists without both known weapon definitions')
        catalog_grants = list(existing_catalog.get_editor_property('grants'))
        expected_grants = old_grants + [
            make_attack_grant(False, existing_machine),
            make_attack_grant(True, existing_ground),
        ]
        if [grant_signature(g) for g in catalog_grants] != [
            grant_signature(g) for g in expected_grants
        ]:
            raise RuntimeError('Unknown changes in Wingman V3 grants')

    impact_field = asset(WINGMAN_IMPACT_FIELD, unreal.GuLiSpellFieldDefinition)
    impact_field.set_editor_property("config_id", "WingmanGroundMissile")
    if abs(float(impact_field.get_editor_property('radius')) - 800.0) > 0.001:
        impact_field.set_editor_property('radius', 800.0)
    impact_field.set_editor_property('visual_reference_radius', 4000.0)
    if impact_field.get_editor_property('timing') != unreal.GuLiSpellFieldTiming.INSTANT:
        impact_field.set_editor_property('timing', unreal.GuLiSpellFieldTiming.INSTANT)
    if abs(float(impact_field.get_editor_property('dissipation_seconds')) - 3.0) > 0.001:
        impact_field.set_editor_property('dissipation_seconds', 3.0)
    visual = visual_variant('WingmanBombardment', 'User.Area_Scale', True, 5.25)
    current_variants = list(impact_field.get_editor_property('activation_variants'))
    if len(current_variants) != 1 or not variant_signatures_match(variant_signature(current_variants[0]), reference_variant_signature()):
        impact_field.set_editor_property('activation_variants', [visual])

    if wingman_projectile is None:
        LIB.make_directory(WINGMAN_PROJECTILE.rsplit('/', 1)[0])
        if not LIB.duplicate_asset(COMMANDER_PROJECTILE, WINGMAN_PROJECTILE):
            raise RuntimeError('Could not create Wingman projectile from Commander WM01')
        wingman_projectile = unreal.load_asset(WINGMAN_PROJECTILE)
    # This point-target weapon freezes motion from SecondaryWeapons/WingmanWeapons.
    # Do not inherit the Commander's homing-projectile profile when duplicating FX.
    if wingman_projectile.get_editor_property('motion_profile_row').get_editor_property('data_table'):
        wingman_projectile.set_editor_property('motion_profile_row', unreal.DataTableRowHandle())
    if wingman_projectile.get_editor_property('flight_vfx_id') != vfx_id('MissileFlight'):
        wingman_projectile.modify()
        wingman_projectile.set_editor_property('flight_vfx_id', vfx_id('MissileFlight'))
    if object_path(wingman_projectile.get_editor_property('impact_field')) != canonical(WINGMAN_IMPACT_FIELD):
        wingman_projectile.set_editor_property('impact_field', impact_field)

    report['commander_projectile'] = commander_projectile.get_path_name()
    report['commander_impact_field'] = object_path(commander_projectile.get_editor_property('impact_field'))
    tuning, report['tables']['tuning'], report['table_imported']['tuning'] = import_table('DT_GuLiStrikeShip_Tuning')
    weapons, report['tables']['weapons'], report['table_imported']['weapons'] = import_table('DT_GuLiStrikeShip_WingmanWeapons')
    targeting, report['tables']['targeting'], report['table_imported']['targeting'] = import_table('DT_GuLiStrikeShip_WingmanTargeting')
    grants = list(old_set.get_editor_property('grants'))
    for ground in (False, True):
        suffix = 'GroundMissile' if ground else 'MachineGun'
        weapon = asset(BASE + '/Weapons/DA_WingmanWeapon_' + suffix, unreal.GuLiWingmanWeaponDefinition)
        if weapon.get_editor_property('kind') != unreal.GuLiWingmanWeaponKind.BASIC_AUTOMATIC:
            weapon.set_editor_property('kind', unreal.GuLiWingmanWeaponKind.BASIC_AUTOMATIC)
        expected_revision = 2 if ground else 3
        if int(weapon.get_editor_property('revision')) != expected_revision:
            weapon.set_editor_property('revision', expected_revision)
        expected_handle = unreal.DataTableRowHandle(data_table=weapons, row_name='Wingman' + suffix)
        if row_handle_signature(weapon.get_editor_property('attack_profile_row')) != row_handle_signature(expected_handle):
            weapon.set_editor_property('attack_profile_row', expected_handle)
        if ground and object_path(weapon.get_editor_property('attack_projectile')) != canonical(WINGMAN_PROJECTILE):
            weapon.set_editor_property('attack_projectile', wingman_projectile)
        grants.append(make_attack_grant(ground, weapon))
    catalog = asset(catalog_path, unreal.GuLiShipAbilitySet)
    if [grant_signature(g) for g in catalog.get_editor_property('grants')] != [
        grant_signature(g) for g in grants
    ]:
        catalog.set_editor_property('grants', grants)
    if int(catalog.get_editor_property('revision')) != 4:
        catalog.set_editor_property('revision', 4)
    if str(catalog.get_editor_property('wingman_type_id')) != str(old_set.get_editor_property('wingman_type_id')):
        catalog.set_editor_property('wingman_type_id', old_set.get_editor_property('wingman_type_id'))
    hangar = unreal.load_asset('/Game/GuLiStrike/Ship/Build/DA_ShipHangar_V1')
    if hangar is None:
        raise RuntimeError('Author the Ship component catalogue before updating Wingman actions.')
    hangar.set_editor_property('ability_set', catalog)
    unreal.EditorAssetLibrary.save_loaded_asset(hangar)
    for path in ('/Game/GuLiStrike/Ship/BP_GuLiStrikeShip', '/Game/GuLiStrike/Ship/BP_CombatAvatarFly01'):
        blueprint = unreal.load_asset(path)
        if not blueprint:
            raise RuntimeError('Production ship Blueprint missing: ' + path)
        cdo = unreal.get_default_object(blueprint.generated_class())
        current_targeting = cdo.get_editor_property('wingman_targeting_row')
        expected_targeting = unreal.DataTableRowHandle(data_table=targeting, row_name='Default')
        if row_handle_signature(current_targeting) not in {
            ('', 'None'), row_handle_signature(expected_targeting)
        }:
            raise RuntimeError('Unknown Wingman targeting row at ' + path + ': ' + str(current_targeting))
        blueprint_changed = False
        if row_handle_signature(current_targeting) != row_handle_signature(expected_targeting):
            cdo.set_editor_property('wingman_targeting_row', expected_targeting)
            blueprint_changed = True
        if blueprint_changed:
            unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        cdo = unreal.get_default_object(blueprint.generated_class())
    mesh = unreal.load_asset('/Game/GuLiStrike/Wingman/SM_Wingman_Mass')
    report['wingman_mesh_bounds'] = str(mesh.get_bounding_box()) if mesh else None

    # Read back the intentional sharing boundary before saving anything.
    require_projectile_contract(commander_projectile, commander=True)
    require_projectile_contract(wingman_projectile, commander=False)
    require_wingman_projectile_references(commander_projectile, wingman_projectile)
    require_wingman_impact_contract(impact_field)
    machine_revision = int(unreal.load_asset(machine_path).get_editor_property('revision'))
    ability_set_revision = int(catalog.get_editor_property('revision'))
    if machine_revision != 3:
        raise RuntimeError('Machine-gun definition did not retain revision 3')
    migrated_ground = unreal.load_asset(ground_path)
    if int(migrated_ground.get_editor_property('revision')) != 2:
        raise RuntimeError('Ground-missile definition did not retain revision 2')
    if ability_set_revision != 4:
        raise RuntimeError('Wingman V3 AbilitySet did not retain revision 4')
    if object_path(migrated_ground.get_editor_property('attack_projectile')) != canonical(WINGMAN_PROJECTILE):
        raise RuntimeError('Ground weapon did not retain the Wingman-only projectile')
    report['revisions'] = {
        'machine_gun': machine_revision,
        'ground_missile': int(migrated_ground.get_editor_property('revision')),
        'ability_set': ability_set_revision,
    }
    dirty_after_readback = {
        str(package.get_name())
        for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    }
    save_paths = sorted(path for path in touched if path in dirty_after_readback)
    for path in save_paths:
        if not LIB.save_asset(path, only_if_is_dirty=True):
            raise RuntimeError('Asset save failed: ' + path)
        report['saved'].append(path)
    report['grant_tags'] = [str(unreal.GameplayTagLibrary.get_tag_name(g.get_editor_property('ability_id'))) for g in grants]
    report['projectiles'] = {
        'commander': {
            'path': commander_projectile.get_path_name(),
            'visual_scale': vfx_scale(commander_projectile.get_editor_property('flight_vfx_id'))[0],
        },
        'wingman': {
            'path': wingman_projectile.get_path_name(),
            'visual_scale': vfx_scale(wingman_projectile.get_editor_property('flight_vfx_id'))[0],
            'flight_system': vfx_resource(wingman_projectile.get_editor_property('flight_vfx_id')),
            'impact_field': object_path(wingman_projectile.get_editor_property('impact_field')),
        },
    }
    report['impact'] = {
        'path': impact_field.get_path_name(),
        'reference_radius': float(impact_field.get_editor_property('radius')),
        'dissipation_seconds': float(impact_field.get_editor_property('dissipation_seconds')),
        'variants': [
            variant_signature(item)
            for item in impact_field.get_editor_property('activation_variants')
        ],
    }
    report['success'] = True
    return report

try:
    result = main()
except Exception:
    result = {'success': False, 'error': traceback.format_exc()}
REPORT.parent.mkdir(parents=True, exist_ok=True)
REPORT.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('Wingman attack deployment: ' + json.dumps(result, ensure_ascii=False))
if hasattr(unreal, 'MCPythonHelper'):
    unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=False))
