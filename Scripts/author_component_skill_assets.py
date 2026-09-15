"""Author the first Ship build catalogue and commander tactics after the native migration.

Run with source UE5.7: -run=pythonscript -script=Scripts/author_component_skill_assets.py.
Existing Wingman weapon/formation values are references, never rewritten here.
"""
import json
from pathlib import Path
import unreal

SHIP_ROOT = '/Game/GuLiStrike/Ship'
BUILD_PATH = SHIP_ROOT + '/Build/DA_ShipBuild_V1'
HANGAR_PATH = SHIP_ROOT + '/Build/DA_ShipHangar_V1'
COMMANDER_PATH = '/Game/GuLiStrike/Commander/Skills/DA_CommanderSkills_V1'
SHIP_BP = SHIP_ROOT + '/BP_CombatAvatarFly01'
PART_BP = SHIP_ROOT + '/Parts/BP_SC_Drone_LaunchBay'


def asset(path, cls):
    existing = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if existing:
        assert isinstance(existing, cls), path + ': incompatible asset class'
        return existing
    directory, name = path.rsplit('/', 1)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', cls)
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, directory, cls, factory)


def value(cls, **fields):
    result = cls()
    for name, item in fields.items():
        result.set_editor_property(name, item)
    return result


def mounts(part, sockets):
    cls = unreal.load_class(None, SHIP_ROOT + '/Parts/' + part + '.' + part + '_C')
    assert cls, part
    return [value(unreal.GuLiShipGroupMount, socket_name=s, part_class=cls) for s in sockets]


def compile_blueprint(blueprint):
    assert unreal.GuLiSkillAuthoringLibrary.compile_skill_blueprint(blueprint), blueprint.get_path_name() + ': Blueprint compile failed'


def author():
    hangar = asset(HANGAR_PATH, unreal.GuLiShipHangarDefinition)
    actions = unreal.load_asset(SHIP_ROOT + '/Abilities/DA_ShipAbilitySet_WingmanV3')
    selected_ids = {'Ship.Ability.Formation.SwarmOrbit', 'Ship.Ability.Weapon.Wingman.MachineGun',
                    'Ship.Ability.Weapon.Wingman.GroundMissile'}
    selected = [g.get_editor_property('ability_id') for g in actions.get_editor_property('grants')
                if str(unreal.GameplayTagLibrary.get_tag_name(g.get_editor_property('ability_id'))) in selected_ids]
    assert len(selected) == 3, 'The existing V3 formation/weapon contract must be available.'
    hangar.set_editor_property('ability_set', actions)
    hangar.set_editor_property('loadout', value(unreal.GuLiShipAbilityLoadoutState, ability_ids=selected))

    # Draft configurations retain known model mappings. Unconfirmed layouts are recorded, not guessed.
    rows = [
        ('01', 'Turret', 1, '', '机炮', 'BP_SC_Autocannon', ['bottom_mid_0', 'bottom_mid_1', 'bottom_mid_2']),
        ('02', 'Turret', 2, '01', '双联火炮', 'BP_SC_Twin_Barrel_Turret', ['bottom_mid_0', 'bottom_mid_2']),
        ('03', 'Turret', 3, '02', '三联火炮', 'BP_SC_Triple_Barrel_Turret', ['bottom_0', 'bottom_2', 'bottom_3', 'bottom_4', 'bottom_5']),
        ('04', 'AirGuns', 1, '', '近防炮', 'BP_SC_CIWS', ['air_' + str(i) for i in range(13, 42)]),
        ('05', 'AirGuns', 2, '04', '防空炮组', 'BP_SC_Single_Barrel_Turret', []),
        ('06', 'MissilePods', 1, '', '雷神导弹舱', 'BP_SC_Thor_MissilePod', ['missilepod_1', 'missilepod_2', 'missilepod_3']),
        ('07', 'MissilePods', 2, '06', '雷神导弹舱二级', 'BP_SC_Thor_MissilePod_Lv2', ['missilepod_1', 'missilepod_2', 'missilepod_3']),
        ('08', 'Hangar', 1, '', '僚机仓', 'BP_SC_Drone_LaunchBay', ['wingman_bay_1', 'wingman_bay_2']),
        ('09', 'ElectronicSupport', 1, '', '电子干扰装置', 'BP_SC_Electronic_JammingDevice', ['group_buff_generator']),
        ('10', 'Shield', 1, '', '护盾发生器', 'BP_SC_Shield_Generator', ['shield_generator']),
        ('11', 'BombBay', 1, '', '燃烧弹发射仓', 'BP_SC_Incendiary_Bomb_LaunchBay', ['tracking_missile_launch_bay_1', 'tracking_missile_launch_bay_2']),
        ('12', 'BombBay', 2, '11', '燃烧弹发射仓二级', 'BP_SC_Incendiary_Bomb_LaunchBay', ['tracking_missile_launch_bay_1', 'tracking_missile_launch_bay_2']),
        ('13', 'BombBay', 3, '12', '燃烧弹发射仓三级', 'BP_SC_Incendiary_Bomb_LaunchBay', ['tracking_missile_launch_bay_1', 'tracking_missile_launch_bay_2']),
    ]
    groups, upgrades = [], []
    for node, route, level, predecessor, label, model, sockets in rows:
        executable = node == '08'
        notes = '' if executable else '草稿：战斗执行未实现，不进入奖励池；数值继续引用原数据表。'
        if node in ('01', '02'):
            notes += ' 原方案 bottom_mid_* 在当前舰体上不存在，挂点布局待确认。'
        if node == '04':
            notes += ' 原方案 air_13–41 中 air_18 在当前舰体上不存在。'
        if node == '05':
            notes += (' 原方案 BP_SC_Single_Barrel_Turret 占 air_1–13，BP_SC_CIWS 占 air_13–41，'
                      'air_13 重复且 air_18 缺失；整组挂点暂留空，待确认完整布局。')
        capability = value(unreal.GuLiShipCapabilityDefinition, capability_id=route)
        if executable:
            capability.set_editor_property('component_class', unreal.GuLiShipHangarCapabilityComponent)
            capability.set_editor_property('configuration', hangar)
        groups.append(value(unreal.GuLiShipPartGroupDefinition, configuration_id='Group_' + node,
                            group_id=route, mounts=mounts(model, sockets), capabilities=[capability],
                            executable=executable, authoring_notes=notes))
        upgrade = value(unreal.GuLiShipUpgradeNode, node_id=node, display_name=label, route_id=route,
                        level=level, replaces_node_id=predecessor, group_configuration_id='Group_' + node)
        if node == '06':
            requirement = value(unreal.GuLiShipRuleNode, op=unreal.GuLiShipRuleOp.CHOSEN_NODE, subject_id='03')
            upgrade.set_editor_property('acquisition_requirements', value(unreal.GuLiShipRuleExpression, root=0, nodes=[requirement]))
        upgrades.append(upgrade)
    catalog = asset(BUILD_PATH, unreal.GuLiShipBuildCatalog)
    catalog.set_editor_property('revision', 1)
    catalog.set_editor_property('groups', groups)
    catalog.set_editor_property('upgrades', upgrades)
    catalog.set_editor_property('exclusions', [value(unreal.GuLiShipExclusionRule, rule_id=rule, routes=routes) for rule, routes in [
        ('AirGunsVsMissilePods', ['AirGuns', 'MissilePods']), ('HangarVsBombBay', ['Hangar', 'BombBay']),
        ('ElectronicSupportVsShield', ['ElectronicSupport', 'Shield'])]])

    part = unreal.load_asset(PART_BP)
    part_cdo = unreal.get_default_object(unreal.load_class(None, PART_BP + '.BP_SC_Drone_LaunchBay_C'))
    part_cdo.set_editor_property('compatible_sockets', ['wingman_bay_1', 'wingman_bay_2'])
    compile_blueprint(part)
    for path in (SHIP_ROOT + '/BP_GuLiStrikeShip', SHIP_BP):
        blueprint = unreal.load_asset(path)
        cdo = unreal.get_default_object(unreal.load_class(None, path + '.' + path.rsplit('/', 1)[1] + '_C'))
        cdo.get_ship_assembly().set_editor_property('catalog', catalog)
        compile_blueprint(blueprint)
        assert unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False), path

    commander = asset(COMMANDER_PATH, unreal.GuLiCommanderSkillCatalog)
    teleport = value(unreal.GuLiActiveSkillDefinition, skill_id='Teleport', scope=unreal.GuLiActiveSkillScope.GLOBAL,
                     target_mode=unreal.GuLiActiveSkillTargetMode.TWO_POINT, cooldown_seconds=0.0,
                     maximum_level=4, executor_class=unreal.GuLiTeleportSkillExecutor)
    commander.set_editor_property('skills', [teleport])
    commander.set_editor_property('unit_skills', {})
    commander.set_editor_property('global_skills', ['Teleport'])
    ship_class = unreal.load_class(None, SHIP_BP + '.BP_CombatAvatarFly01_C')
    issues = list(unreal.GuLiSkillAuthoringLibrary.validate_ship_catalog(catalog, ship_class))
    issues += list(unreal.GuLiSkillAuthoringLibrary.validate_commander_catalog(commander))
    assert not issues, '\n'.join(str(i) for i in issues)
    for item in (part, hangar, catalog, commander):
        assert unreal.EditorAssetLibrary.save_loaded_asset(item, only_if_is_dirty=False), item.get_path_name()
    # Strip removed GA fields on resave, while retaining all existing numeric data and references.
    for path in unreal.EditorAssetLibrary.list_assets(SHIP_ROOT + '/Abilities', recursive=True, include_folder=False):
        item = unreal.load_asset(path)
        if isinstance(item, unreal.GuLiShipAbilitySet):
            assert unreal.EditorAssetLibrary.save_loaded_asset(item, only_if_is_dirty=False), path
    migrated_blueprints = []
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for directory in (SHIP_ROOT, '/Game/Commander', '/Game/GuLiStrike/Commander'):
        for data in registry.get_assets_by_path(directory, recursive=True):
            if str(data.asset_class_path.asset_name) not in ('Blueprint', 'WidgetBlueprint'):
                continue
            blueprint = data.get_asset()
            compile_blueprint(blueprint)
            assert unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False), blueprint.get_path_name()
            migrated_blueprints.append(blueprint.get_path_name())
    report = {'success': True, 'ship_catalog': BUILD_PATH, 'commander_catalog': COMMANDER_PATH,
              'executable_nodes': ['08'], 'hangar_mounts': ['wingman_bay_1', 'wingman_bay_2'], 'hangar_capabilities': 1,
              'compiled_blueprints': migrated_blueprints}
    output = Path(unreal.Paths.project_dir()) / 'TestResults/ComponentSkills'
    output.mkdir(parents=True, exist_ok=True)
    (output / 'asset-authoring.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    author()
