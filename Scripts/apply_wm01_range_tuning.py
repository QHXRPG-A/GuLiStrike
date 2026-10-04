"""Import WM01 range tuning and save its player review markers. Never starts PIE."""
import json
import math
import traceback
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/WM01Range20261001'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'WM01RangeReview20261001'
TABLES = ['DT_GuLiStrikeCommander_UnitSkills',
          'DT_GuLiStrikeSecondaryUnitSkills_Skills',
          'DT_GuLiStrikeSecondaryWeapons_Projectiles']


def apply_data(report):
    namespace = {'__name__': 'wm01_range_import'}
    source = (ROOT / 'Scripts/import_data_to_engine.py').read_text(encoding='utf8')
    source = source.split('report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}', 1)[0]
    exec(compile(source, 'import_data_to_engine.py', 'exec'), namespace)
    namespace['PROGRESS'] = str(OUT / 'table-import.log')
    manifest = json.loads((ROOT / 'data/Json/manifest.json').read_text(encoding='utf8'))
    report['tables'] = [namespace['import_table'](name, manifest['tables'][name]) for name in TABLES]
    assert all(entry.get('imported') for entry in report['tables']), report['tables']

    authored = json.loads((ROOT / 'data/Json/DT_GuLiStrikeSecondaryUnitSkills_Skills.json').read_text(encoding='utf8'))
    row = next(r for r in authored if r['Name'] == 'WM01_HomingMissile')
    catalog = unreal.load_asset('/Game/GuLiStrike/Commander/Skills/DA_CommanderSkills_V1')
    definitions = list(catalog.get_editor_property('skills'))
    matching = [s for s in definitions if str(s.skill_id) == row['Name']]
    assert len(matching) == 1
    matching[0].set_editor_property('range_centimeters', row['RangeCentimeters'])
    matching[0].set_editor_property('range_multiplier', row['RangeMultiplier'])
    assert str(matching[0].range_source_slot) == row['RangeSourceSlot'] == 'BasicAttack'
    catalog.set_editor_property('skills', definitions)
    issues = list(unreal.GuLiSkillAuthoringLibrary.validate_commander_catalog(catalog))
    assert not issues, str(issues)
    assert unreal.EditorAssetLibrary.save_loaded_asset(catalog, only_if_is_dirty=False)

    unit_table = unreal.load_asset('/Game/GuLiStrike/Data/' + TABLES[0])
    weapon_rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(unit_table))
    gun = next(r for r in weapon_rows if r['Name'] == 'WM01_Strafe')
    definition = next(s for s in catalog.skills if str(s.skill_id) == row['Name'])
    projectile = unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_Missile')
    profile = projectile.get_editor_property('motion_profile_row')
    assert str(profile.row_name) == 'WM01_Missile'
    assert profile.data_table.get_path_name().split('.')[0] == '/Game/GuLiStrike/Data/' + TABLES[2]
    motion = projectile.resolve_motion_settings()
    assert motion is not None
    report['readback'] = dict(
        gun_range_cm=gun['RangeCentimeters'],
        missile_range_cm=gun['RangeCentimeters'] * definition.range_multiplier,
        missile_authored_range_cm=definition.range_centimeters,
        missile_source_slot=str(definition.range_source_slot),
        missile_range_multiplier=definition.range_multiplier,
        missile_speed_cm_per_second=motion.speed,
        missile_lifetime_seconds=motion.maximum_lifetime,
        catalog_issues=list(map(str, issues)))
    assert report['readback']['gun_range_cm'] == 12000
    assert report['readback']['missile_range_cm'] == definition.range_centimeters == 24000
    assert motion.speed == 2400 and motion.maximum_lifetime == 20
    assert motion.maximum_lifetime > (24000 + 800) / motion.speed + motion.lift_seconds
    assert gun['ProjectileSpeedCentimetersPerSecond'] * gun['ProjectileLifetimeSeconds'] >= gun['RangeCentimeters']
    report['data_applied'] = True


def author_scene(report, level, world):
    ed = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    current = list(ed.get_all_level_actors())
    existing = next(a for a in current if a.get_actor_label() == 'WM01Missile_FixtureCenter')
    navs = [a for a in current if isinstance(a, unreal.RecastNavMesh)]
    assert navs and not unreal.NavigationSystemV1.is_navigation_being_built_or_locked(world)
    center = existing.get_actor_location()

    def ground(offset):
        wanted = center + unreal.Vector(offset, 0, 0)
        point = unreal.NavigationSystemV1.project_point_to_navigation(
            world, wanted, navs[0], None, unreal.Vector(150, 150, 50000))
        assert point is not None and math.hypot(point.x - wanted.x, point.y - wanted.y) <= 150.01
        return point

    positions = {'Source': ground(0), 'GunInside110m': ground(11000),
                 'GunOutside130m': ground(13000), 'QInside230m': ground(23000),
                 'QOutside250m': ground(25000)}
    origin = positions['Source']
    for name, limit in [('GunInside110m', 12000), ('QInside230m', 24000)]:
        assert math.dist(origin.to_tuple(), positions[name].to_tuple()) < limit
    for name, limit in [('GunOutside130m', 12000), ('QOutside250m', 24000)]:
        assert math.dist(origin.to_tuple(), positions[name].to_tuple()) > limit

    def actor(cls, label, location):
        matches = [a for a in ed.get_all_level_actors() if a.get_actor_label() == label]
        assert len(matches) <= 1, label
        a = matches[0] if matches else ed.spawn_actor_from_class(cls, location)
        assert isinstance(a, cls), label
        a.set_actor_label(label)
        a.set_actor_location(location, False, True)
        a.set_folder_path('Review/WM01Range')
        a.set_editor_property('tags', [unreal.Name(TAG)])
        return a

    for name, position in positions.items():
        actor(unreal.TargetPoint, 'WM01Range_' + name, position)
        label = actor(unreal.TextRenderActor, 'WM01Range_Label_' + name, position + unreal.Vector(0, 0, 35))
        label.set_actor_rotation(unreal.Rotator(90, 0, 0), True)
        text = label.get_component_by_class(unreal.TextRenderComponent)
        text.set_text(name)
        text.set_world_size(160)
        text.set_text_render_color(unreal.Color(255, 216, 74, 255))

    def spawn_command(team, name):
        p = positions[name]
        return f'gs.GM.Skill.Spawn {team} 2 {p.x:.2f} {p.y:.2f} {p.z:.2f}'

    instructions = (
        '重防号 WM01 / UnitTypeId=2 射程验收：机枪30→120米，Q导弹96→240米。\n'
        '新开本图单机PIE，就绪红队指挥官；每个距离场景使用新局，保持测试单位原地。\n'
        '源点生成：' + spawn_command('Red', 'Source') + '\n'
        '机枪界内：' + spawn_command('Blue', 'GunInside110m') + '\n'
        '机枪界外：' + spawn_command('Blue', 'GunOutside130m') + '\n'
        '分别观察该重防号：110米自动开火，130米不以该目标开火；不要把其他单位攻击计入。'
        '控制台Spawn输出ID，gs.GM.Skill.Soldier <ID>可查看该兵的目标与射击次数。\n'
        'Q：F4获得04.01重防导弹仓，选中源点单台，按Q点击QInside230m地面标记；'
        '应发射并飞抵落点。等待其6秒冷却，再按Q点击QOutside250m；应拒绝且不扣冷却。\n'
        'Q圆心范围为最终BasicAttack×2；基础随机落点半径8米，最多248米。'
        '导弹速度24米/秒，最长飞行20秒；远距允许超过旧8秒继续飞行。\n'
        '静态数据及实体已核对；游戏效果待玩家验证，助手未启动PIE。')
    note = actor(unreal.Note, 'WM01Range_PlayerInstructions', origin + unreal.Vector(0, 1200, 400))
    note.set_editor_property('text', instructions)
    old_note = next(a for a in current if a.get_actor_label() == 'WM01Missile_CandidateInstructions')
    old_text = str(old_note.get_editor_property('text'))
    old_note.set_editor_property('text', old_text.replace('×3.2', '×2').replace('96m', '240m'))
    report['scene_saved'] = bool(level.save_current_level())
    assert report['scene_saved']

    entities = []
    for a in ed.get_all_level_actors():
        if not a.actor_has_tag(TAG):
            continue
        entry = dict(label=a.get_actor_label(), object=a.get_path_name(),
                     cls=a.get_class().get_name(), location=list(a.get_actor_location().to_tuple()),
                     tags=[str(t) for t in a.tags])
        if isinstance(a, unreal.Note):
            entry['text'] = str(a.get_editor_property('text'))
        if isinstance(a, unreal.TextRenderActor):
            entry['text'] = str(a.get_component_by_class(unreal.TextRenderComponent).text)
        if isinstance(a, unreal.TargetPoint):
            entry['distance_cm'] = math.dist(origin.to_tuple(), a.get_actor_location().to_tuple())
        entities.append(entry)
    assert len(entities) == 11
    report['entities'] = sorted(entities, key=lambda e: e['label'])
    report['spawn_commands'] = {name: spawn_command('Red' if name == 'Source' else 'Blue', name) for name in positions}
    report['game_mode'] = world.get_world_settings().get_editor_property('default_game_mode').get_path_name()
    assert 'GuLiCommanderGameMode' in report['game_mode']
    report['map'] = world.get_path_name().split('.')[0]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    report = dict(success=False, data_applied=False, scene_saved=False, runtime_validation='not_run')
    try:
        level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        assert not level.is_in_play_in_editor(), 'Preserve the player session'
        assert world.get_path_name().split('.')[0] == MAP, 'Use the designated feature map'
        assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map edits'
        apply_data(report)
        author_scene(report, level, world)
        report['success'] = True
    except Exception:
        report['error'] = traceback.format_exc()
    path = OUT / 'handoff.json'
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    unreal.MCPythonHelper.submit_result(json.dumps(dict(
        success=report['success'], data_applied=report['data_applied'],
        scene_saved=report['scene_saved'], readback=report.get('readback'),
        entities=len(report.get('entities', [])), error=report.get('error'), report=str(path))))


main()
