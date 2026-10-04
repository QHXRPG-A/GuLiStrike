"""Save the WM01 Q player-review layout and read its entities back. No PIE or automatic firing."""
import json
import math
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/WM01Guidance60'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'WM01GuidanceReview20260930'
MARKER = '\n[WM01 Q 每圈60发 20260930]'


def main():
    report = {'success': False, 'map': MAP, 'saved': False, 'runtime_validation': 'not_run'}
    try:
        level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        ed = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        assert not level.is_in_play_in_editor(), 'Preserve player session'
        assert world.get_path_name().split('.')[0] == MAP, 'Use the existing feature map'
        assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map edits'
        current = list(ed.get_all_level_actors())
        center_actor = next(a for a in current if a.get_actor_label() == 'WM01Missile_FixtureCenter')
        f4 = next(a for a in current if a.get_actor_label() == 'RogueCards_F4_Entry')
        center = center_actor.get_actor_location()
        navs = [a for a in current if isinstance(a, unreal.RecastNavMesh) and 'CommanderSoldier' in a.get_name()]
        assert len(navs) == 1, 'Expected the CommanderSoldier navigation data'
        assert not unreal.NavigationSystemV1.is_navigation_being_built_or_locked(world)
        soldier = next(r for r in json.loads((ROOT / 'data/Json/DT_GuLiStrikeCommander_Soldiers.json').read_text(encoding='utf-8')) if r['Id'] == 2)
        spacing = max(400.0, soldier['ModelWidthMeters'] * 100.0 + 20.0)

        def ground(x, y):
            wanted = unreal.Vector(center.x+x, center.y+y, center.z)
            point = unreal.NavigationSystemV1.project_point_to_navigation(world, wanted, navs[0], None, unreal.Vector(150, 150, 50000))
            assert point is not None and math.hypot(point.x-wanted.x, point.y-wanted.y) <= 150.01, 'Review position is off the commander navigation mesh'
            return point

        positions = [ground((i % 5-2)*spacing, (i // 5-2)*spacing) for i in range(25)]
        targets = {'A': ground(-2300, 3500), 'B': ground(0, 5800), 'C': ground(2300, 3500)}
        skill = next(r for r in json.loads((ROOT / 'data/Json/DT_GuLiStrikeSecondaryUnitSkills_Skills.json').read_text(encoding='utf-8')) if r['Name'] == 'WM01_HomingMissile')
        max_distance = max(math.dist(p.to_tuple(), t.to_tuple()) for p in positions for t in targets.values())
        assert max_distance <= skill['RangeCentimeters'], 'All 25 casters must reach A/B/C at the baseline range'

        def actor(cls, label, position):
            matches = [a for a in ed.get_all_level_actors() if a.get_actor_label() == label]
            assert len(matches) <= 1, label
            a = matches[0] if matches else ed.spawn_actor_from_class(cls, position)
            assert isinstance(a, cls), label
            a.set_actor_label(label)
            a.set_actor_location(position, False, True)
            a.set_folder_path('Review/WM01Guidance60')
            a.set_editor_property('tags', list(dict.fromkeys([str(t) for t in a.tags] + [TAG])))
            return a

        for i, p in enumerate(positions, 1):
            actor(unreal.TargetPoint, f'WM01Guidance_Slot_{i:02d}', p)
        for letter, p in targets.items():
            actor(unreal.TargetPoint, f'WM01Guidance_Target_{letter}', p)
            label = actor(unreal.TextRenderActor, f'WM01Guidance_Label_{letter}', p+unreal.Vector(0, 0, 35))
            label.set_actor_rotation(unreal.Rotator(90, 0, 0), True)
            text = label.get_component_by_class(unreal.TextRenderComponent)
            text.set_text(letter)
            text.set_world_size(220)
            text.set_text_render_color(unreal.Color(255, 216, 74, 255))
        center_actor.set_editor_property('tags', list(dict.fromkeys([str(t) for t in center_actor.tags] + [TAG, 'WM01GuidanceFixtureCenter'])))
        instructions = (
            '重防号 Q 每个共享引导圈最多60发，整台齐射不拆分；半径8米，保留单弹预警。\n'
            '本区保存25个出生位置与A/B/C目标标记；真实Mass单位由玩家主动准备，未替换地图默认部队。\n'
            '新开PIE并就绪指挥官，在控制台执行 gs.MissileFixture.Build 25 6；这是弹量/解锁准备，F4卡牌获取另行验证。\n'
            '关闭控制台，只框选该25台，6秒内先后指向A/B/C按Q，预期60/60/30发；第四次Q无新圈。只发射的单位进入6秒冷却。\n'
            '共享圈固定在各次按键位置，最后一枚结束后消失；移动鼠标不移动旧圈；同点重复Q独立清理。\n'
            '必须使用真实选择+Q，不使用 gs.MissileFixture.Fire（旧性能入口逐台独立施放）。\n'
            '每个补充配置重新开PIE：gs.MissileFixture.Build 25 7 -> 56/56/56/7；gs.MissileFixture.Build 2 60 -> 60/60。\n'
            'F4功能验证另开干净局：04.01解锁，05.01基础1发最多获得59次；到60后不再抽到，旧候选确认也重检。\n'
            '源码新增反射字段和准备参数须编译并重开后使用；资源导入状态见 Artifacts/WM01Guidance60/asset-readback.json。\n'
            '助手仅保存场景与核对实体/导航位置，未启动PIE、自动发射或进行效果验收。')
        note = actor(unreal.Note, 'WM01Guidance_Instructions', center+unreal.Vector(0, 1000, 100))
        note.set_editor_property('text', instructions)
        old = str(f4.get_editor_property('text'))
        f4.set_editor_property('text', old.split(MARKER)[0] + MARKER + '\n每圈60发的25台验证区见 WM01Guidance_Instructions；保留本入口的真实F4流程。雨点攻势最多59次，单台上限60发。')
        report['saved'] = bool(level.save_current_level())
        assert report['saved']
        report['entities'] = []
        for a in ed.get_all_level_actors():
            if a.actor_has_tag(TAG) or a.get_actor_label() == 'RogueCards_F4_Entry':
                entry = {'label': a.get_actor_label(), 'class': a.get_class().get_name(), 'path': a.get_path_name(),
                    'location': list(a.get_actor_location().to_tuple()), 'tags': [str(t) for t in a.tags]}
                if isinstance(a, unreal.Note):
                    entry['text'] = str(a.get_editor_property('text'))
                if isinstance(a, unreal.TextRenderActor):
                    entry['text'] = str(a.get_component_by_class(unreal.TextRenderComponent).text)
                report['entities'].append(entry)
        assert len([e for e in report['entities'] if e['label'].startswith('WM01Guidance_Slot_')]) == 25
        assert len([e for e in report['entities'] if e['label'].startswith('WM01Guidance_Target_')]) == 3
        assert len([e for e in report['entities'] if e['label'].startswith('WM01Guidance_Label_')]) == 3
        report.update(success=True, formation={'rows': 5, 'columns': 5, 'unit_type_id': 2, 'spacing_cm': spacing,
            'runtime_units_created': False, 'manual_prepare_command': 'gs.MissileFixture.Build 25 6'},
            maximum_target_distance_cm=max_distance, navigation_points_checked=28,
            default_deployments_unchanged=True, new_native_schema_loaded=hasattr(unreal.load_asset(skill['Configuration']), 'max_projectiles_per_activation'))
    except Exception:
        report['error'] = traceback.format_exc()
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'scene-readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps({k: v for k, v in report.items() if k != 'entities'}, ensure_ascii=False))


main()
