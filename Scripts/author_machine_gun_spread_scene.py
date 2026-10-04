"""Extend the saved machine-gun comparison area with editor-only 10-degree guides.

No PIE or image capture. The rays show a geometric angle reference; live combat
acceptance uses the existing armies in the commander map.
"""
import json
import math
from pathlib import Path
import traceback
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'ArtSource/MachineGunSpread_20260930'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'MachineGunSpreadReview20260930'
FOLDER = 'Review/MachineGunEffects_20260930/Spread'
api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
result = {'success': False, 'pie_started': False, 'images_read': False,
          'player_acceptance': 'pending', 'actors': [], 'lanes': []}
try:
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert not level.is_in_play_in_editor()
    world = editor.get_editor_world()
    assert world.get_path_name() == MAP + '.LVL_CommanderMassPrototype'
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Map has unsaved work'
    actors = api.get_all_level_actors()
    anchors = [a for a in actors if a.get_actor_label() == 'MachineGunReview_Ground'
               and 'MachineGunReview20260930' in list(map(str, a.tags))]
    assert len(anchors) == 1, 'Existing machine-gun comparison area is required'
    owned = {a.get_actor_label(): a for a in actors if TAG in list(map(str, a.tags))}
    origin = anchors[0].get_actor_location() + unreal.Vector(0, 8000, 25)
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    assert cube
    expected_actors = {}

    def spawn(cls, label, position):
        actor = owned.get(label) or api.spawn_actor_from_class(cls, position)
        assert actor is not None and isinstance(actor, cls), label
        actor.set_actor_label(label)
        actor.set_actor_location(position, False, False)
        actor.set_editor_property('tags', [TAG])
        actor.set_editor_property('is_editor_only_actor', True)
        actor.set_folder_path(FOLDER)
        expected_actors[label] = actor
        return actor

    def mesh(label, position, scale):
        actor = spawn(unreal.StaticMeshActor, label, position)
        actor.static_mesh_component.set_static_mesh(cube)
        actor.set_actor_scale3d(scale)
        actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        actor.static_mesh_component.set_cast_shadow(False)
        return actor

    def rod(label, start, end):
        delta = end - start
        length = math.sqrt(sum(v * v for v in delta.to_tuple()))
        actor = mesh(label, (start + end) * 0.5, unreal.Vector(length / 100.0, 0.04, 0.04))
        actor.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(start, end), False)
        return actor

    floor = mesh('MachineGunSpread_Ground', origin + unreal.Vector(0, 1600, -25), unreal.Vector(80, 55, 0.5))
    floor.set_actor_rotation(unreal.Rotator(), False)
    for name, unit, x, distance in [('Sweeper', 1, -2000, 2000.0), ('WM01', 2, 1000, 3000.0)]:
        apex = origin + unreal.Vector(x, 0, 800)
        center = apex + unreal.Vector(0, distance, 0)
        radius = distance * math.tan(math.radians(5.0))
        endpoints = {'Center': center, 'Left5': center + unreal.Vector(-radius, 0, 0),
                     'Right5': center + unreal.Vector(radius, 0, 0),
                     'Down5': center + unreal.Vector(0, 0, -radius),
                     'Up5': center + unreal.Vector(0, 0, radius)}
        ray_labels = []
        for suffix, end in endpoints.items():
            label = 'MachineGunSpread_' + name + '_' + suffix
            rod(label, apex, end)
            ray_labels.append(label)
        rod('MachineGunSpread_' + name + '_DiameterHorizontal', endpoints['Left5'], endpoints['Right5'])
        rod('MachineGunSpread_' + name + '_DiameterVertical', endpoints['Down5'], endpoints['Up5'])
        note = spawn(unreal.Note, 'MachineGunSpread_' + name + '_Instructions', apex)
        chinese_name = '扫荡者' if unit == 1 else '重防号'
        note.set_editor_property('text', f'{chinese_name} / UnitTypeId={unit}：机枪总夹角10°，水平和竖直边界均为±5°。'
                                 f'轴向距离{distance / 100:g}m，截面半径{radius:.3f}cm。'
                                 '这是统一800cm参考高度的静态几何标尺，不代表真实枪口高度或采样轨迹。'
                                 '真实子弹从枪口独立采样后沿直线飞行；伤害、命中和灯光沿实际弹道。')
        result['lanes'].append({'unit_type_id': unit, 'label': chinese_name, 'axis': [0, 1, 0],
                                'apex': list(apex.to_tuple()), 'length_cm': distance,
                                'section_radius_cm': radius, 'full_cone_degrees': 10.0,
                                'ray_labels': ray_labels})
    note = spawn(unreal.Note, 'MachineGunSpread_PlayerInstructions', origin + unreal.Vector(-3200, -700, 200))
    instructions = ('机枪10°随机弹道：仅扫荡者和重防号，源表GuLiStrikeSecondaryWeapons.xlsx/UnitSkills，'
                    'ProjectileSpreadAngleDegrees为总夹角。所有参考物EditorOnly、无碰撞，不参与战斗。'
                    '玩家手动进入本地图，指挥官就绪后使用原有双方军队，在20m/30m射程内连续交火；'
                    '分别检查两兵种、双方弹色、密集交火：每发方向有随机差异，随后直线飞行，'
                    '命中位置和随弹照明跟随实际弹头，停止后无残留，活动灯保持既有6盏全局预算。'
                    '枪管仍瞄准原方向，僚机、玩家地面机甲及导弹不应用本次散布。'
                    '参考标尺位于旧机枪对照区旁，按标签MachineGunSpread定位。'
                    '本轮仅编译、技能表和保存实体回读；未启动PIE、未读图，实战与视觉待玩家验收。')
    note.set_editor_property('text', instructions)
    result['instructions'] = instructions
    assert level.save_current_level()
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    # Read back actual serialized actor/component properties, including angular bounds.
    actual = {a.get_actor_label(): a for a in api.get_all_level_actors() if TAG in list(map(str, a.tags))}
    assert set(actual) == set(expected_actors)
    for label, actor in sorted(actual.items()):
        assert actor.get_editor_property('is_editor_only_actor')
        entry = {'label': label, 'path': actor.get_path_name(), 'class': actor.get_class().get_name(),
                 'position': list(actor.get_actor_location().to_tuple()),
                 'scale': list(actor.get_actor_scale3d().to_tuple()),
                 'rotation': list(actor.get_actor_rotation().to_tuple()),
                 'editor_only': True, 'folder': str(actor.get_folder_path()), 'tags': list(map(str, actor.tags))}
        if isinstance(actor, unreal.StaticMeshActor):
            component = actor.static_mesh_component
            assert component.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION
            assert not component.get_editor_property('cast_shadow')
            entry.update(mesh=component.get_editor_property('static_mesh').get_path_name(),
                         collision='NoCollision', cast_shadow=False)
        if isinstance(actor, unreal.Note):
            entry['text'] = actor.get_editor_property('text')
        result['actors'].append(entry)
    for lane in result['lanes']:
        angles = {}
        for label in lane['ray_labels']:
            direction = actual[label].get_actor_forward_vector()
            angle = math.degrees(math.acos(max(-1.0, min(1.0, direction.y))))
            expected = 0.0 if label.endswith('_Center') else 5.0
            assert math.isclose(angle, expected, abs_tol=0.001), (label, angle)
            angles[label] = angle
        lane['saved_ray_angles_degrees'] = angles
    result.update(success=True, map=world.get_path_name(), saved=True, anchor=anchors[0].get_path_name(),
                  origin=list(origin.to_tuple()), actor_count=len(actual), folder=FOLDER)
except Exception:
    result['error'] = traceback.format_exc()
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'scene-readback.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({k: result.get(k) for k in ('success', 'saved', 'actor_count', 'error')}, ensure_ascii=False))
