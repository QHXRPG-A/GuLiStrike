"""Import the two Ship attack tables and install the V3 wingman skill catalog.

Run in a stopped editor with Scripts/ue_exec.py. Existing formation and VFX
definitions are referenced verbatim. Only the assets listed in the report save.
"""
import csv
import io
import json
from pathlib import Path
import traceback
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
REPORT = ROOT / 'TestResults/WingmanAttack/deployment.json'
BASE = '/Game/GuLiStrike/Ship/Abilities'
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

def import_table(name):
    rows = json.loads((ROOT / ('Data/Json/' + name + '.json')).read_text(encoding='utf-8'))
    manifest = json.loads((ROOT / 'Data/Json/manifest.json').read_text(encoding='utf-8'))
    struct = unreal.find_object(None, manifest['tables'][name]['struct'])
    if not struct:
        raise RuntimeError('Generated row structure not loaded: ' + name)
    fields = list(rows[0])
    def cell(value):
        if isinstance(value, dict):
            return '(X=%s,Y=%s,Z=%s)' % (value['X'], value['Y'], value['Z'])
        return str(value)
    stream = io.StringIO()
    writer = csv.writer(stream, lineterminator='\n')
    writer.writerow(fields)
    for row in rows:
        writer.writerow([cell(row[key]) for key in fields])
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
    table = unreal.load_asset('/Game/GuLiStrike/Data/' + name)
    exported = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    by_name = {row['Name']: row for row in exported}
    for row in rows:
        for key, value in row.items():
            actual = by_name[row['Name']][key]
            if isinstance(value, (int, float)):
                if abs(float(actual) - value) > 0.001:
                    raise RuntimeError(f'Table readback mismatch {name}/{row["Name"]}/{key}')
            elif isinstance(value, str) and str(actual) != value:
                raise RuntimeError(f'Table text mismatch {name}/{key}')
    return table, exported

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
    report = {'success': False, 'commandlet': b_commandlet, 'level_actor_count': len(actors), 'saved': [], 'tables': {}}
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    touched = {BASE + '/DA_ShipAbilitySet_WingmanV3',
               BASE + '/Weapons/DA_WingmanWeapon_MachineGun', BASE + '/Weapons/DA_WingmanWeapon_GroundMissile',
               '/Game/GuLiStrike/Ship/BP_GuLiStrikeShip', '/Game/GuLiStrike/Ship/BP_CombatAvatarFly01',
               '/Game/GuLiStrike/Data/DT_GuLiStrikeShip_WingmanWeapons',
               '/Game/GuLiStrike/Data/DT_GuLiStrikeShip_WingmanTargeting'}
    conflicts = [str(p.get_name()) for p in dirty if str(p.get_name()) in touched]
    if conflicts:
        raise RuntimeError('Unsaved edits in deployment targets: ' + str(conflicts))
    old_set = unreal.load_asset(BASE + '/DA_ShipAbilitySet_WingmanV1')
    projectile = unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_Missile')
    if not old_set or not projectile:
        raise RuntimeError('Existing formation catalog or WM01 missile is missing')
    report['reused_projectile'] = projectile.get_path_name()
    report['reused_impact_field'] = str(projectile.get_editor_property('impact_field'))
    weapons, report['tables']['weapons'] = import_table('DT_GuLiStrikeShip_WingmanWeapons')
    targeting, report['tables']['targeting'] = import_table('DT_GuLiStrikeShip_WingmanTargeting')
    grants = list(old_set.get_editor_property('grants'))
    for ground in (False, True):
        suffix = 'GroundMissile' if ground else 'MachineGun'
        weapon = asset(BASE + '/Weapons/DA_WingmanWeapon_' + suffix, unreal.GuLiWingmanWeaponDefinition)
        weapon.set_editor_property('kind', unreal.GuLiWingmanWeaponKind.BASIC_AUTOMATIC)
        weapon.set_editor_property('revision', 1)
        weapon.set_editor_property('attack_profile_row', unreal.DataTableRowHandle(data_table=weapons, row_name='Wingman' + suffix))
        if ground:
            weapon.set_editor_property('attack_projectile', projectile)
        grants.append(unreal.GuLiShipAbilityGrant(
            ability_id=tag('Ship.Ability.Weapon.Wingman.' + suffix),
            slot=unreal.GuLiShipAbilitySlot.BASIC_WEAPON,
            weapon_slot_id='GroundWeapon' if ground else 'AirWeapon', skill_id='Wingman.' + suffix,
            ability_class=(unreal.GuLiShipWingmanGroundMissileAbility if ground else unreal.GuLiShipWingmanMachineGunAbility).static_class(),
            weapon_definition=weapon))
    catalog = asset(BASE + '/DA_ShipAbilitySet_WingmanV3', unreal.GuLiShipAbilitySet)
    catalog.set_editor_property('grants', grants)
    catalog.set_editor_property('revision', 3)
    catalog.set_editor_property('wingman_type_id', old_set.get_editor_property('wingman_type_id'))
    for path in ('/Game/GuLiStrike/Ship/BP_GuLiStrikeShip', '/Game/GuLiStrike/Ship/BP_CombatAvatarFly01'):
        blueprint = unreal.load_asset(path)
        if not blueprint:
            raise RuntimeError('Production ship Blueprint missing: ' + path)
        cdo = unreal.get_default_object(blueprint.generated_class())
        cdo.set_editor_property('ship_ability_set', catalog)
        cdo.set_editor_property('wingman_targeting_row', unreal.DataTableRowHandle(data_table=targeting, row_name='Default'))
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        cdo = unreal.get_default_object(blueprint.generated_class())
        if cdo.get_editor_property('ship_ability_set') != catalog:
            raise RuntimeError('Blueprint compile lost catalog reference: ' + path)
    mesh = unreal.load_asset('/Game/GuLiStrike/Wingman/SM_Wingman_Mass')
    report['wingman_mesh_bounds'] = str(mesh.get_bounding_box()) if mesh else None
    for path in sorted(touched):
        if not LIB.save_asset(path, only_if_is_dirty=False):
            raise RuntimeError('Asset save failed: ' + path)
        report['saved'].append(path)
    report['grant_tags'] = [str(unreal.GameplayTagLibrary.get_tag_name(g.get_editor_property('ability_id'))) for g in grants]
    report['success'] = True
    return report

try:
    result = main()
except Exception:
    result = {'success': False, 'error': traceback.format_exc()}
REPORT.parent.mkdir(parents=True, exist_ok=True)
REPORT.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('Wingman attack deployment: ' + json.dumps(result, ensure_ascii=False))
