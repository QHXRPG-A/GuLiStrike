"""Update the existing WM01 Q range markers; never start gameplay or change skill data."""
import json
import math
import traceback
from pathlib import Path

import unreal

MAP = '/Game/Maps/LVL_CommanderMassPrototype'
OUT = Path('D:/UE5.7/test1/Artifacts/WM01HorizontalRange20261028')
MARKER = '\n[WM01 Q horizontal range 20261028]'


def main():
    report = dict(success=False, saved=False, map=MAP, runtime_validation='not_run',
                  visual_validation='player_pending')
    try:
        level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        assert not level.is_in_play_in_editor(), 'Preserve the player session'
        assert world.get_path_name().split('.')[0] == MAP, 'Use the existing feature map'
        assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved maps'
        assert not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved assets'
        editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        actors = list(editor.get_all_level_actors())

        def existing(*labels):
            matches = [a for a in actors if a.get_actor_label() in labels]
            assert len(matches) == 1, labels
            return matches[0]

        source = existing('WM01Range_Source')
        source_label = existing('WM01Range_Label_Source')
        assert isinstance(source, unreal.TargetPoint) and isinstance(source_label, unreal.TextRenderActor)
        previous_origin = source.get_actor_location()
        navs = [a for a in actors if isinstance(a, unreal.RecastNavMesh)]
        assert navs and not unreal.NavigationSystemV1.is_navigation_being_built_or_locked(world)
        origin = unreal.NavigationSystemV1.project_point_to_navigation(
            world, previous_origin, navs[0], None, unreal.Vector(150, 150, 50000))
        assert origin is not None
        assert math.hypot(origin.x - previous_origin.x, origin.y - previous_origin.y) < 2
        planned = []
        for old, new, distance in [('QInside230m', 'QInside239m', 23900),
                                   ('QOutside250m', 'QOutside241m', 24100)]:
            wanted = origin + unreal.Vector(distance, 0, 0)
            point = unreal.NavigationSystemV1.project_point_to_navigation(
                world, wanted, navs[0], None, unreal.Vector(150, 150, 50000))
            assert point is not None
            assert math.hypot(point.x - wanted.x, point.y - wanted.y) < 2
            target = existing('WM01Range_' + old, 'WM01Range_' + new)
            label = existing('WM01Range_Label_' + old, 'WM01Range_Label_' + new)
            assert isinstance(target, unreal.TargetPoint) and isinstance(label, unreal.TextRenderActor)
            text = label.get_component_by_class(unreal.TextRenderComponent)
            assert text is not None
            planned.append((new, point, target, label, text))
        note = existing('WM01Range_PlayerInstructions')
        missile_note = existing('WM01Missile_CandidateInstructions')
        assert isinstance(note, unreal.Note) and isinstance(missile_note, unreal.Note)

        source.set_actor_location(origin, False, True)
        source_label.set_actor_location(origin + unreal.Vector(0, 0, 35), False, True)
        for name, point, target, label, text in planned:
            target.set_actor_label('WM01Range_' + name)
            target.set_actor_location(point, False, True)
            # Keep the existing label's presentation; only its content and placement change.
            label.set_actor_label('WM01Range_Label_' + name)
            label.set_actor_location(point + unreal.Vector(0, 0, 35), False, True)
            text.set_text(name + ' (XY)')
        command = f'gs.GM.Skill.Spawn Red 2 {origin.x:.2f} {origin.y:.2f} {origin.z:.2f}'
        instructions = (
            '重防号 WM01 / UnitTypeId=2：Q 水平射程 240 米，普攻保持现值。\n'
            '确认加载本轮 Q 水平射程代码后，由玩家在本图新开 PIE，选择并就绪红队指挥官。\n'
            '源点生成：' + command + '\n'
            '通过 F4 获取 04.01「重防导弹仓」，选中源点重防号并保持原地。\n'
            '按 Q 点击 QInside239m 标签下的地面：水平 239 米，应成功发射；'
            '30 米低空高度不应再使此点超出射程。\n'
            '等该台 6 秒冷却结束，按 Q 点击 QOutside241m 标签下的地面：水平 241 米，'
            '应拒绝且不扣冷却；立即再次点击 239 米标记应仍可施放。\n'
            '判定只使用单位逻辑位置与点击圆心的 XY 距离，恰好 240 米包含在内；'
            '弹道继续使用真实高度，落点散布仍为半径 8 米。\n'
            '数值来源：SecondaryUnitSkills/Skills 的 WM01_HomingMissile，'
            'RangeSourceSlot=BasicAttack，RangeMultiplier=2，最终普攻 120 米时 Q=240 米；'
            '射程升级仍沿用既有继承。源槽非空时固定 RangeCentimeters=24000 不参与计算。\n'
            '标记与说明已保存；助手未启动游玩，运行与视觉验收待玩家确认。')
        note.set_editor_property('text', instructions)
        missile_text = str(missile_note.get_editor_property('text')).split(MARKER)[0]
        missile_note.set_editor_property('text', missile_text + MARKER +
            '\nQ 基础水平射程 240 米，忽略高差；现行边界入口见 WM01Range_PlayerInstructions，'
            'QInside239m / QOutside241m。普攻数值保持；编译加载状态见本轮开发记录。')
        report['saved'] = bool(level.save_current_level())
        assert report['saved'], 'Map save failed'
        labels = {source.get_actor_label(), source_label.get_actor_label(),
                  note.get_actor_label(), missile_note.get_actor_label()}
        labels.update(a.get_actor_label() for _, _, target, label, _ in planned for a in (target, label))
        related = [a for a in editor.get_all_level_actors() if a.get_actor_label() in labels]
        assert len(related) == 8
        report['entities'] = []
        for a in sorted(related, key=lambda a: a.get_actor_label()):
            p = a.get_actor_location()
            row = dict(label=a.get_actor_label(), object=a.get_path_name(),
                       type=a.get_class().get_name(), location=list(p.to_tuple()))
            if isinstance(a, unreal.TargetPoint):
                row['horizontal_distance_cm'] = math.hypot(p.x - origin.x, p.y - origin.y)
            if isinstance(a, unreal.Note):
                row['text'] = str(a.get_editor_property('text'))
            if isinstance(a, unreal.TextRenderActor):
                row['text'] = str(a.get_component_by_class(unreal.TextRenderComponent).text)
            report['entities'].append(row)
        report['spawn_command'] = command
        report['game_mode'] = world.get_world_settings().get_editor_property('default_game_mode').get_path_name()
        assert 'GuLiCommanderGameMode' in report['game_mode']
        report['success'] = True
    except Exception:
        report['error'] = traceback.format_exc()
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / 'scene-delivery.json'
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(dict(
        success=report['success'], saved=report['saved'], entities=len(report.get('entities', [])),
        error=report.get('error'), report=str(path))))


main()
