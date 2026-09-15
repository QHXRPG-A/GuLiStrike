"""Read-only cold-load audit for Ship assemblies and commander skills."""
import json
import os
import runpy
from pathlib import Path
import unreal


def audit():
    root = Path(unreal.Paths.project_dir())
    ship_path = '/Game/GuLiStrike/Ship/BP_CombatAvatarFly01'
    ship_class = unreal.load_class(None, ship_path + '.BP_CombatAvatarFly01_C')
    ship = unreal.get_default_object(ship_class)
    catalog = unreal.load_asset('/Game/GuLiStrike/Ship/Build/DA_ShipBuild_V1')
    commander = unreal.load_asset('/Game/GuLiStrike/Commander/Skills/DA_CommanderSkills_V1')
    hangar = unreal.load_asset('/Game/GuLiStrike/Ship/Build/DA_ShipHangar_V1')
    issues = list(unreal.GuLiSkillAuthoringLibrary.validate_ship_catalog(catalog, ship_class))
    issues += list(unreal.GuLiSkillAuthoringLibrary.validate_commander_catalog(commander))
    checks = {
        'catalog_validation': not issues,
        'ship_default_uses_catalog': ship.get_ship_assembly().get_editor_property('catalog') == catalog,
        'no_default_hangar_grant': ship.get_hangar_capability() is None,
        'thirteen_upgrade_nodes': len(catalog.get_editor_property('upgrades')) == 13,
        'empty_initial_unit_skills': len(commander.get_editor_property('unit_skills')) == 0,
        'global_teleport_granted': 'Teleport' in [str(x) for x in commander.get_editor_property('global_skills')],
        'hangar_reuses_v3_actions': hangar.get_editor_property('ability_set').get_name() == 'DA_ShipAbilitySet_WingmanV3',
    }
    executable = [g for g in catalog.get_editor_property('groups') if g.get_editor_property('executable')]
    checks['one_executable_hangar_group'] = len(executable) == 1 and str(executable[0].get_editor_property('group_id')) == 'Hangar'
    if len(executable) == 1:
        sockets = [str(m.get_editor_property('socket_name')) for m in executable[0].get_editor_property('mounts')]
        checks['two_mounts_one_capability'] = sockets == ['wingman_bay_1', 'wingman_bay_2'] and len(executable[0].get_editor_property('capabilities')) == 1
    legacy = []
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for directory in ('/Game/GuLiStrike', '/Game/Commander'):
        for item in registry.get_assets_by_path(directory, recursive=True):
            if str(item.asset_class_path.asset_name) != 'Blueprint':
                continue
            parent = str(item.get_tag_value('ParentClass'))
            if any(name in parent for name in ('GameplayAbility', 'GuLiShipGameplayAbility', 'GuLiArmySkillAbility', 'GuLiTeleportAbility')):
                legacy.append(str(item.package_name))
    checks['no_legacy_gameplay_ability_blueprint_parents'] = not legacy
    snapshot = runpy.run_path(str(root / 'Scripts/snapshot_ship_wingman_contract.py'))['capture']('after')
    baseline = root / 'TestResults/ComponentSkills/wingman-contract-before.json'
    checks['wingman_weapon_contract_unchanged'] = baseline.exists() and json.loads(baseline.read_text(encoding='utf-8')) == snapshot
    report = {'passed': all(checks.values()), 'pid': os.getpid(), 'checks': checks,
              'issues': [str(x) for x in issues], 'legacy_blueprints': legacy,
              'ship_sockets': sorted(str(x) for x in ship.get_editor_property('hull_mesh').get_all_socket_names())}
    destination = root / 'TestResults/ComponentSkills'
    destination.mkdir(parents=True, exist_ok=True)
    (destination / 'asset-audit.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))
    assert report['passed'], 'Component skill asset audit failed; see TestResults/ComponentSkills/asset-audit.json'
    return report


if __name__ == '__main__':
    audit()
