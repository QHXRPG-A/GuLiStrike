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


def ship_group_layouts():
    """The mount lists used by the build catalogue and the part class defaults."""
    ciws_side_slots = [f'air_{index}' for index in range(14, 42) if index != 18]
    missile_slots = ['missilepod_1', 'missilepod_2', 'missilepod_3']
    bomb_slots = ['tracking_missile_launch_bay_1', 'tracking_missile_launch_bay_2']
    return {
        '01': [('Autocannon', ['bottom_0', 'bottom_1', 'bottom_2'])],
        '02': [('Twin_Barrel_Turret', ['bottom_0', 'bottom_2'])],
        '03': [('Triple_Barrel_Turret', ['bottom_0', 'bottom_2', 'bottom_3', 'bottom_4', 'bottom_5'])],
        '04': [('CIWS', ['air_13', *ciws_side_slots])],
        '05': [('Single_Barrel_Turret', [f'air_{index}' for index in range(1, 14)]),
               ('CIWS', ciws_side_slots)],
        '06': [('Thor_MissilePod', missile_slots)],
        '07': [('Thor_MissilePod_Lv2', missile_slots)],
        '08': [('Drone_LaunchBay', ['wingman_bay_1', 'wingman_bay_2'])],
        '09': [('Electronic_JammingDevice', ['group_buff_generator'])],
        '10': [('Shield_Generator', ['shield_generator'])],
        '11': [('Incendiary_Bomb_LaunchBay', bomb_slots)],
        '12': [('Incendiary_Bomb_LaunchBay', bomb_slots)],
        '13': [('Incendiary_Bomb_LaunchBay', bomb_slots)],
    }


def configured_mounts(layout):
    return [mount for part, slots in layout for mount in mounts('BP_SC_' + part, slots)]


def sync_ship_part_sockets(layouts):
    required = {}
    for layout in layouts.values():
        for part, slots in layout:
            required.setdefault(part, {}).update(dict.fromkeys(slots))
    changed = []
    for part, slots in required.items():
        blueprint = unreal.load_asset(SHIP_ROOT + '/Parts/BP_SC_' + part)
        existing = unreal.get_default_object(blueprint.generated_class())
        target = list(slots)
        if [str(slot) for slot in existing.get_editor_property('compatible_sockets')] == target:
            continue
        existing.modify()
        existing.set_editor_property('compatible_sockets', target)
        blueprint.modify()
        compile_blueprint(blueprint)
        saved = unreal.get_default_object(blueprint.generated_class())
        assert [str(slot) for slot in saved.get_editor_property('compatible_sockets')] == target, part
        assert unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False), part
        changed.append(part)
    return changed


def sync_ship_mounts():
    """Apply the layout to the saved Ship catalogue without rebuilding other skills."""
    layouts = ship_group_layouts()
    catalog = unreal.load_asset(BUILD_PATH)
    ship_class = unreal.load_class(None, SHIP_BP + '.BP_CombatAvatarFly01_C')
    hull = unreal.get_default_object(ship_class).get_hull_mesh_component()
    hull_slots = {str(slot) for slot in hull.get_all_socket_names()}
    groups = catalog.get_editor_property('groups')
    assert {str(g.configuration_id) for g in groups} == {'Group_' + node for node in layouts}
    for node, layout in layouts.items():
        slots = [slot for _, part_slots in layout for slot in part_slots]
        assert len(slots) == len(set(slots)), (node, slots)
        assert set(slots) <= hull_slots, (node, sorted(set(slots) - hull_slots))
    changed_parts = sync_ship_part_sockets(layouts)
    changed_groups = []
    for index, group in enumerate(groups):
        node = str(group.configuration_id).removeprefix('Group_')
        expected = configured_mounts(layouts[node])
        actual = group.get_editor_property('mounts')
        actual_mounts = [(str(m.socket_name), m.part_class) for m in actual]
        expected_mounts = [(str(m.socket_name), m.part_class) for m in expected]
        if actual_mounts == expected_mounts:
            continue
        group.set_editor_property('mounts', expected)
        notes = str(group.get_editor_property('authoring_notes')).split(' 原方案', 1)[0]
        group.set_editor_property('authoring_notes', notes)
        groups[index] = group
        changed_groups.append(node)
    if changed_groups:
        catalog.modify()
        catalog.set_editor_property('groups', groups)
        catalog.set_editor_property('revision', catalog.revision + 1)
        issues = list(unreal.GuLiSkillAuthoringLibrary.validate_ship_catalog(catalog, ship_class))
        assert not issues, '\n'.join(str(issue) for issue in issues)
        assert unreal.EditorAssetLibrary.save_loaded_asset(catalog, False)
    return {'groups': changed_groups, 'parts': changed_parts, 'revision': catalog.revision}


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

    layouts = ship_group_layouts()
    rows = [
        ('01', 'Turret', 1, '', '机炮'),
        ('02', 'Turret', 2, '01', '双联火炮'),
        ('03', 'Turret', 3, '02', '三联火炮'),
        ('04', 'AirGuns', 1, '', '近防炮'),
        ('05', 'AirGuns', 2, '04', '防空炮组'),
        ('06', 'MissilePods', 1, '', '雷神导弹舱'),
        ('07', 'MissilePods', 2, '06', '雷神导弹舱二级'),
        ('08', 'Hangar', 1, '', '僚机仓'),
        ('09', 'ElectronicSupport', 1, '', '电子干扰装置'),
        ('10', 'Shield', 1, '', '护盾发生器'),
        ('11', 'BombBay', 1, '', '燃烧弹发射仓'),
        ('12', 'BombBay', 2, '11', '燃烧弹发射仓二级'),
        ('13', 'BombBay', 3, '12', '燃烧弹发射仓三级'),
    ]
    groups, upgrades = [], []
    for node, route, level, predecessor, label in rows:
        belly = node in ('01', '02', '03')
        executable = belly or node == '08'
        notes = '' if executable else '草稿：战斗执行未实现，不进入奖励池；数值继续引用原数据表。'
        capability = value(unreal.GuLiShipCapabilityDefinition, capability_id=route)
        if belly:
            shell = unreal.load_asset('/Game/GuLiStrike/FX/Xuntian/Belly/DA_XuntianBellyShell' + str(level))
            assert shell, 'Run author_xuntian_belly.py first'
            capability.set_editor_property('component_class', unreal.GuLiXuntianBellyBattery)
            capability.set_editor_property('configuration', shell)
        elif executable:
            capability.set_editor_property('component_class', unreal.GuLiShipHangarCapabilityComponent)
            capability.set_editor_property('configuration', hangar)
        groups.append(value(unreal.GuLiShipPartGroupDefinition, configuration_id='Group_' + node,
                            group_id=route, mounts=configured_mounts(layouts[node]), capabilities=[capability],
                            executable=executable, authoring_notes=notes))
        upgrade = value(unreal.GuLiShipUpgradeNode, node_id=node, display_name=label, route_id=route,
                        level=level, replaces_node_id=predecessor, group_configuration_id='Group_' + node)
        if node == '06':
            requirement = value(unreal.GuLiShipRuleNode, op=unreal.GuLiShipRuleOp.CHOSEN_NODE, subject_id='03')
            upgrade.set_editor_property('acquisition_requirements', value(unreal.GuLiShipRuleExpression, root=0, nodes=[requirement]))
        upgrades.append(upgrade)
    catalog = asset(BUILD_PATH, unreal.GuLiShipBuildCatalog)
    catalog.set_editor_property('revision', max(catalog.revision, 2))
    catalog.set_editor_property('groups', groups)
    catalog.set_editor_property('upgrades', upgrades)
    catalog.set_editor_property('exclusions', [value(unreal.GuLiShipExclusionRule, rule_id=rule, routes=routes) for rule, routes in [
        ('AirGunsVsMissilePods', ['AirGuns', 'MissilePods']), ('HangarVsBombBay', ['Hangar', 'BombBay']),
        ('ElectronicSupportVsShield', ['ElectronicSupport', 'Shield'])]])

    sync_ship_part_sockets(layouts)
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
    # Merge the global tactic; never erase subsequently authored unit Q skills.
    skills = [s for s in commander.get_editor_property('skills') if str(s.get_editor_property('skill_id')) != 'Teleport']
    commander.set_editor_property('skills', skills + [teleport])
    global_skills = list(commander.get_editor_property('global_skills'))
    if 'Teleport' not in [str(s) for s in global_skills]:
        global_skills.append('Teleport')
    commander.set_editor_property('global_skills', global_skills)
    ship_class = unreal.load_class(None, SHIP_BP + '.BP_CombatAvatarFly01_C')
    issues = list(unreal.GuLiSkillAuthoringLibrary.validate_ship_catalog(catalog, ship_class))
    issues += list(unreal.GuLiSkillAuthoringLibrary.validate_commander_catalog(commander))
    assert not issues, '\n'.join(str(i) for i in issues)
    for item in (hangar, catalog, commander):
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
    # Unit skill values are owned by the independent Excel source; preserve global tactics.
    unit_author = Path(unreal.Paths.project_dir()) / 'Scripts/author_secondary_unit_skill_assets.py'
    exec(compile(unit_author.read_text(encoding='utf-8'), str(unit_author), 'exec'), {'__name__': 'secondary_skill_authoring'})


if __name__ == '__main__':
    author()
