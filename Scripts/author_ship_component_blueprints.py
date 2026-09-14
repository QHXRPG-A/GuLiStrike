"""Author the user's ShipComponent catalogue without changing mesh/socket assets.

Call snapshot(), create_parts(), replace_catalogue(), then retire_placeholders().
The last step requires all asset and active source references to be migrated first.
"""
import hashlib
import json
from pathlib import Path

import unreal
import migrate_ship_part_visuals as visual_migration

PROJECT = Path('D:/UE5.7/test1')
OUT = PROJECT / 'outputs/ship-component-blueprints'
SPEC = Path('D:/学习文档/ship第一版组件系统.md')
DEST = '/Game/GuLiStrike/Ship/Parts'
MODELS = '/Game/Assets/Ships/ShipComponent'
OWNER = 'GuLi.ShipComponentBlueprints.20260912'
TAG = 'GuLi.Authoring.Owner'
SHIP_PATHS = ['/Game/GuLiStrike/Ship/BP_GuLiStrikeShip', '/Game/GuLiStrike/Ship/BP_CombatAvatarFly01']
OLD = ['/Game/GuLiStrike/Ship/' + n for n in
       ['BP_Engine_Standard', 'BP_Engine_Heavy', 'BP_Weapon_Laser', 'BP_Weapon_RocketPod']]


def write(name, value):
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def path(obj):
    return obj.get_path_name() if obj else None


def groups():
    """Literal document assignments; red boxes in image-1/2/3/4 were inspected."""
    return [
        dict(id='01', name='1级底部炮台组', role='Ground', prerequisites=[], exclusions=[],
             members=[dict(part='Autocannon', sockets=['bottom_mid_0', 'bottom_mid_1', 'bottom_mid_2'])]),
        dict(id='02', name='2级底部炮台组', role='Ground', prerequisites=['01'], exclusions=[],
             members=[dict(part='Twin_Barrel_Turret', sockets=['bottom_mid_0', 'bottom_mid_2'])]),
        dict(id='03', name='3级底部炮台组', role='Ground', prerequisites=['02'], exclusions=[],
             members=[dict(part='Triple_Barrel_Turret', sockets=['bottom_0', 'bottom_2', 'bottom_3', 'bottom_4', 'bottom_5'])]),
        dict(id='04', name='1级顶层对空炮台组', role='Air', prerequisites=[], exclusions=[],
             members=[dict(part='CIWS', sockets=['air_' + str(i) for i in range(13, 42)])]),
        dict(id='05', name='2级顶层对空炮台组', role='Air', prerequisites=['04'], exclusions=[],
             members=[dict(part='Single_Barrel_Turret', sockets=['air_' + str(i) for i in range(1, 14)]),
                      dict(part='CIWS', sockets=['air_' + str(i) for i in range(13, 42)])]),
        dict(id='06', name='1级顶层导弹仓', role='Ground', prerequisites=['03'], exclusions=['04'],
             members=[dict(part='Thor_MissilePod', sockets=['missilepod_1', 'missilepod_2', 'missilepod_3'])]),
        dict(id='07', name='2级顶层导弹仓', role='AirAndGround', prerequisites=['06'], exclusions=['04'],
             members=[dict(part='Thor_MissilePod_Lv2', sockets=['missilepod_1', 'missilepod_2', 'missilepod_3'])]),
    ]


def definitions():
    names = [
        ('Autocannon', '自动炮／1级底部炮台', True, True),
        ('Bottom_Twin_Barrel_Turret', '底部双联炮台', True, True),
        ('CIWS', '近防炮／顶层对空', True, True),
        ('High_Rate_Fire_Cannon', '高射速火炮', True, True),
        ('Single_Barrel_Turret', '单管炮台／2级顶层对空', True, True),
        ('Triple_Barrel_Turret', '三联炮台／3级底部', True, True),
        ('Twin_Barrel_Turret', '双联炮台／2级底部', True, True),
        ('Drone_LaunchBay', '无人机发射舱', False, False),
        ('Electronic_JammingDevice', '电子干扰装置', False, False),
        ('Incendiary_Bomb_LaunchBay', '燃烧弹发射舱', False, True),
        ('Missile_Bay', '导弹舱', False, True),
        ('Shield_Generator', '护盾发生器', False, False),
        ('Thor_MissilePod', 'Thor导弹舱／1级对地', False, True),
        ('Thor_MissilePod_Lv2', 'Thor导弹舱／2级对地对空', False, True),
    ]
    result = []
    for key, label, skeletal, weapon in names:
        source_key = key.removesuffix('_Lv2')
        mesh = MODELS + ('/Rigged/SKM_SC_' if skeletal else '/SM_SC_') + source_key
        records = [g for g in groups() if any(m['part'] == key for m in g['members'])]
        sockets = list(dict.fromkeys(s for g in records for m in g['members'] if m['part'] == key for s in m['sockets']))
        result.append(dict(key=key, name='BP_SC_' + key, path=DEST + '/BP_SC_' + key,
                           display_name=label, mesh=mesh, skeletal=skeletal, weapon=weapon,
                           compatible_sockets=sockets, groups=records))
    return result


def read_ship(bp_path):
    cdo = unreal.get_default_object(unreal.load_asset(bp_path).generated_class())
    hull = cdo.get_editor_property('hull_mesh')
    return dict(hull=path(hull.static_mesh), hull_transform=visual_migration.transform(hull.get_relative_transform()),
                catalogue=[path(p) for p in cdo.get_editor_property('part_catalogue')],
                defaults=[dict(socket=str(p.socket_name), part=path(p.part_class)) for p in cdo.get_editor_property('default_parts')])


def asset_file_stat(asset_path):
    disk = PROJECT / 'Content' / (asset_path.split('.')[0].removeprefix('/Game/') + '.uasset')
    stat = disk.stat()
    return dict(size=stat.st_size, mtime_ns=stat.st_mtime_ns)


def snapshot():
    if (OUT / 'baseline.json').exists():
        return verify_preservation()
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = registry.get_assets_by_path(MODELS, True)
    meshes = {path(a.get_asset()): visual_migration.mesh_snapshot(a.get_asset()) for a in assets
              if str(a.asset_class_path.asset_name) in ('StaticMesh', 'SkeletalMesh')}
    skeletons = {str(a.package_name): asset_file_stat(str(a.package_name)) for a in assets
                 if str(a.asset_class_path.asset_name) == 'Skeleton'}
    ships = {p: read_ship(p) for p in SHIP_PATHS}
    for ship in ships.values():
        if ship['hull'] not in meshes:
            meshes[ship['hull']] = visual_migration.mesh_snapshot(unreal.load_asset(ship['hull']))
    data = dict(owner=OWNER, source_spec=str(SPEC), source_spec_sha256=hashlib.sha256(SPEC.read_bytes()).hexdigest(),
                source_image_sha256={f'image-{i}.png': hashlib.sha256((SPEC.parent / f'image-{i}.png').read_bytes()).hexdigest() for i in range(1, 5)},
                ships=ships, meshes=meshes, skeletons=skeletons,
                dirty_content=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
                dirty_maps=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
    write('baseline.json', data)
    return verify_preservation()


def verify_preservation():
    before = json.loads((OUT / 'baseline.json').read_text(encoding='utf-8'))
    for mesh_path, expected in before['meshes'].items():
        actual = visual_migration.mesh_snapshot(unreal.load_asset(mesh_path))
        assert actual == expected, ('Mesh or socket changed', mesh_path)
    for skeleton_path, expected in before['skeletons'].items():
        assert asset_file_stat(skeleton_path) == expected, ('Skeleton changed', skeleton_path)
    for bp_path, expected in before['ships'].items():
        actual = read_ship(bp_path)
        assert actual['hull'] == expected['hull'] and actual['hull_transform'] == expected['hull_transform'], bp_path
    result = dict(preserved=True, meshes=len(before['meshes']), skeletons=len(before['skeletons']),
                  sockets=sum(len(m['sockets']) for m in before['meshes'].values()))
    write('socket-preservation.json', result)
    return result


def description(row):
    lines = [row['display_name'], '来源：ship第一版组件系统.md', '模型：' + row['mesh']]
    for g in row['groups']:
        lines.append('组' + g['id'] + ' ' + g['name'] + '；火力=' + g['role'] +
                     '；依赖=' + (','.join(g['prerequisites']) or '无') + '；互斥=' + (','.join(g['exclusions']) or '无'))
    if not row['groups']:
        lines.append('文档尚未分配组别和安装Socket；CompatibleSockets保持空。')
    lines.append('分组依赖、互斥和目标类型为设计记录，升级判定与自动索敌尚未接入。')
    lines.append('数值沿用原生默认值；PartId留空，待正式配置数据表。')
    return '\n'.join(lines)


def compile_save(bp_path):
    compiled = visual_migration.compile_checked(bp_path)
    assert unreal.EditorAssetLibrary.save_loaded_asset(unreal.load_asset(bp_path), False), ('Save failed', bp_path)
    return compiled


def create_parts():
    verify_preservation()
    rows = definitions()
    assert len(rows) == 14
    # Check every source and destination before creating the first blueprint.
    for row in rows:
        assert unreal.EditorAssetLibrary.does_asset_exist(row['mesh']), row['mesh']
        if unreal.EditorAssetLibrary.does_asset_exist(row['path']):
            existing = unreal.load_asset(row['path'])
            assert unreal.EditorAssetLibrary.get_metadata_tag(existing, TAG) == OWNER, ('Existing user asset', row['path'])
    projectile = unreal.load_asset('/Game/GuLiStrike/Ship/BP_ShipProjectile').generated_class()
    result = {}
    for row in rows:
        bp = unreal.load_asset(row['path']) if unreal.EditorAssetLibrary.does_asset_exist(row['path']) else None
        if not bp:
            parent = unreal.GuLiStrikeWeaponPart if row['weapon'] else unreal.GuLiStrikeShipPartComponent
            factory = unreal.BlueprintFactory()
            factory.set_editor_property('parent_class', parent)
            bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(row['name'], DEST, unreal.Blueprint, factory)
            assert bp, row['path']
            unreal.EditorAssetLibrary.set_metadata_tag(bp, TAG, OWNER)
        bp.set_editor_property('blueprint_description', description(row))
        bp.set_editor_property('blueprint_category', 'Ship Parts')
        unreal.EditorAssetLibrary.set_metadata_tag(bp, 'GuLi.Spec.Groups', json.dumps(row['groups'], ensure_ascii=False))
        unreal.EditorAssetLibrary.set_metadata_tag(bp, 'GuLi.Spec.Source', str(SPEC))
        cdo = unreal.get_default_object(bp.generated_class())
        assert isinstance(cdo, unreal.GuLiStrikeShipPartComponent)
        assert isinstance(cdo, unreal.GuLiStrikeWeaponPart) == row['weapon']
        cdo.set_editor_property('visual_type', unreal.GuLiStrikeShipPartVisualType.SKELETAL_MESH if row['skeletal'] else unreal.GuLiStrikeShipPartVisualType.STATIC_MESH)
        cdo.set_editor_property('skeletal_mesh', unreal.load_asset(row['mesh']) if row['skeletal'] else None)
        cdo.set_editor_property('static_mesh', None if row['skeletal'] else unreal.load_asset(row['mesh']))
        cdo.set_editor_property('override_materials', [])
        cdo.set_editor_property('compatible_sockets', row['compatible_sockets'])
        cdo.set_editor_property('part_relative_transform', unreal.Transform())
        cdo.set_editor_property('part_display_name', row['display_name'])
        cdo.set_editor_property('part_id', unreal.Name('None'))
        muzzle_names = [s['name'] for s in visual_migration.mesh_snapshot(unreal.load_asset(row['mesh']))['sockets']]
        if row['weapon']:
            cdo.set_editor_property('projectile_class', projectile)
            cdo.set_editor_property('muzzle_socket_name', muzzle_names[0] if muzzle_names else unreal.Name('None'))
        result[row['path']] = dict(compile=compile_save(row['path']), mesh=row['mesh'], sockets=row['compatible_sockets'],
                                   muzzle_sockets=muzzle_names, groups=[g['id'] for g in row['groups']])
    write('created-parts.json', result)
    write('catalogue.json', dict(owner=OWNER, definitions=rows, groups=groups()))
    return validate_parts()


def validate_parts():
    result = {}
    hull = unreal.load_asset(read_ship(SHIP_PATHS[-1])['hull'])
    hull_names = {s['name'] for s in visual_migration.mesh_snapshot(hull)['sockets']}
    for row in definitions():
        bp = unreal.load_asset(row['path'])
        assert bp
        cdo = unreal.get_default_object(bp.generated_class())
        expected_type = unreal.GuLiStrikeShipPartVisualType.SKELETAL_MESH if row['skeletal'] else unreal.GuLiStrikeShipPartVisualType.STATIC_MESH
        assert cdo.get_editor_property('visual_type') == expected_type, row['path']
        asset = cdo.get_editor_property('skeletal_mesh' if row['skeletal'] else 'static_mesh')
        assert path(asset).split('.')[0] == row['mesh'], row['path']
        assert [str(s) for s in cdo.get_editor_property('compatible_sockets')] == row['compatible_sockets'], row['path']
        assert not cdo.get_editor_property('override_materials'), row['path']
        assert visual_migration.transform(cdo.get_editor_property('part_relative_transform')) == visual_migration.transform(unreal.Transform()), row['path']
        assert bp.get_editor_property('blueprint_description') == description(row), row['path']
        assert json.loads(unreal.EditorAssetLibrary.get_metadata_tag(bp, 'GuLi.Spec.Groups')) == row['groups'], row['path']
        if row['weapon']:
            muzzle = cdo.get_editor_property('muzzle_socket_name')
            assert str(muzzle) == 'None' or asset.find_socket(muzzle), row['path']
        result[row['path']] = dict(valid=True, assigned_sockets=len(row['compatible_sockets']),
                                   missing_hull_sockets=[s for s in row['compatible_sockets'] if s not in hull_names])
    report = dict(parts=result, preservation=verify_preservation(), known_group_overlap={'05': ['air_13']})
    write('parts-validation.json', report)
    return report


def replace_catalogue():
    validate_parts()
    classes = [unreal.load_asset(row['path']).generated_class() for row in definitions()]
    results = {}
    for bp_path in SHIP_PATHS:
        bp = unreal.load_asset(bp_path)
        cdo = unreal.get_default_object(bp.generated_class())
        kept = [p for p in cdo.get_editor_property('default_parts') if not p.part_class or path(p.part_class).split('.')[0] not in OLD]
        cdo.set_editor_property('default_parts', kept)
        cdo.set_editor_property('part_catalogue', classes)
        results[bp_path] = dict(compile=compile_save(bp_path), state=read_ship(bp_path))
    write('ship-catalogues.json', results)
    return results


def retire_placeholders():
    """User authorized removal. Refuse deletion while references remain."""
    validate_parts()
    expected_catalogue = [row['path'] + '.' + row['name'] + '_C' for row in definitions()]
    for bp_path in SHIP_PATHS:
        state = read_ship(bp_path)
        assert state['catalogue'] == expected_catalogue, bp_path
        assert not any(p['part'] and p['part'].split('.')[0] in OLD for p in state['defaults']), bp_path
    test_source = (PROJECT / 'Source/GuLiStrike/Gameplay/Ship/Tests/GuLiShipPartVisualTests.cpp').read_text(encoding='utf-8-sig')
    assert not any(p in test_source for p in OLD), 'Migrate the existing test fixture reference first'
    for bp_path in OLD:
        if unreal.EditorAssetLibrary.does_asset_exist(bp_path):
            refs = list(unreal.EditorAssetLibrary.find_package_referencers_for_asset(bp_path, True))
            assert not refs, (bp_path, refs)
    result = {}
    for bp_path in OLD:
        if unreal.EditorAssetLibrary.does_asset_exist(bp_path):
            assert unreal.EditorAssetLibrary.delete_asset(bp_path), bp_path
        result[bp_path] = not unreal.EditorAssetLibrary.does_asset_exist(bp_path)
    write('retired-placeholders.json', result)
    verify_preservation()
    return result
