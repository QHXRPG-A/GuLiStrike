"""Prepare and save manual Mass soft-avoidance review markers; never start gameplay or build navigation."""
import json
import math
import traceback
from pathlib import Path
import unreal

MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'MassSoftAvoidanceReview20260930'
FOLDER = 'GuLiStrike/Review/MassSoftAvoidance'
ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/MassSoftAvoidance20260930'
CENTER = (-20000.0, 72000.0)
report = {'success': False, 'map': MAP, 'runtime_verified': False,
          'native_build_executed': False, 'gameplay_started': False}


def record(actor):
    row = {'label': actor.get_actor_label(), 'path': actor.get_path_name(),
           'class': actor.get_class().get_name(),
           'location': list(actor.get_actor_location().to_tuple()),
           'rotation': list(actor.get_actor_rotation().to_tuple()),
           'scale': list(actor.get_actor_scale3d().to_tuple())}
    if isinstance(actor, unreal.Note):
        row['text'] = actor.get_editor_property('text')
    if isinstance(actor, unreal.StaticMeshActor):
        component = actor.static_mesh_component
        row['mesh'] = component.static_mesh.get_path_name()
        row['collision'] = str(component.get_collision_enabled())
    return row


def run():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assert editor.get_game_world() is None, 'A game session is running; leave it untouched.'
    world = editor.get_editor_world()
    assert world.get_path_name() == MAP + '.LVL_CommanderMassPrototype'
    api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = api.get_all_level_actors()
    owned = {a.get_actor_label(): a for a in actors if TAG in [str(t) for t in a.tags]}
    original = {a.get_path_name(): record(a) for a in actors if a not in owned.values()}
    assert not any(a.get_class().get_name() == 'GuLiCommanderDeploymentPoint' for a in actors)
    definition = unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap')
    layout_hash = definition.get_editor_property('layout_hash')
    preview_before = unreal.GuLiResourceAuthoringLibrary.get_initial_army_spawn_layout_json()
    assert preview_before, 'The existing army layout must resolve before authoring.'
    reservations = json.loads(preview_before)['reservations']
    clusters = [(c.get_editor_property('center'), c.get_editor_property('obstacle_radius_centimeters'))
                for c in definition.get_editor_property('clusters')]
    clear_radius = min([math.hypot(CENTER[0] - c.x, CENTER[1] - c.y) - r for c, r in clusters]
                       + [math.hypot(CENTER[0] - r['x'], CENTER[1] - r['y']) - r['radius_cm']
                          for r in reservations])
    assert clear_radius > 6500.0, 'Review area must remain clear of army, facilities and ore reservations.'
    ring = unreal.load_asset('/Game/Commander/Units/SM_CommanderUnitRing')
    material = unreal.load_asset('/Engine/BasicShapes/BasicShapeMaterial')
    assert ring and material
    bounds = ring.get_bounding_box()
    native_x = max(abs(bounds.min.x), abs(bounds.max.x))
    native_y = max(abs(bounds.min.y), abs(bounds.max.y))

    def ground(x, y):
        hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 30000),
            unreal.Vector(x, y, -15000), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True,
            list(owned.values()), unreal.DrawDebugTrace.NONE)
        assert hit and hit.to_tuple()[0], f'No ground below ({x}, {y})'
        return hit.to_tuple()[5]

    def get_actor(label, cls, location):
        actor = owned.get(label)
        if not actor:
            actor = api.spawn_actor_from_class(cls, location)
            owned[label] = actor
        assert actor.get_class() == cls.static_class(), label
        actor.modify()
        actor.set_actor_label(label)
        actor.set_folder_path(FOLDER)
        actor.set_editor_property('tags', [TAG])
        actor.set_actor_location(location, False, False)
        return actor

    c = ground(*CENTER)
    assembly = definition.get_editor_property('spawn_anchors').get_editor_property('red_assembly')
    notes = [
        ('Entry', assembly + unreal.Vector(0, 0, 900),
         'Mass软避障验收：验收前确认已加载本次原生代码。保留原军队红蓝各250名，不自动启动PIE。'
         '玩家自行开局后，选本方队伍按S停止自动推进，取25台重防号或混编队伍到西侧观察区。'
         '区域中心(-20000,72000)，编辑器定位MassSoft_Overview相机；W/E/S/N标记距中心各40米。'),
        ('Gather25', c + unreal.Vector(-1000, -1000, 500),
         '①选25台重防号，在中心标记集结并连续改令。圈外径12.5米；允许短暂穿入，'
         '开阔场景侵入深度超过双方半径和20%不可持续超过1秒，压力解除约1秒恢复分散。'),
        ('Mixed', c + unreal.Vector(1000, -1000, 500),
         '②混编重防号和普通单位向中心集结；各自圈等于自身Mass半径，不读取模型包围盒。'
         '普通默认圈外径3米；模型尺寸、表内额外净距保持原值且额外净距不启用。'),
        ('Crossing', c + unreal.Vector(-1000, 1000, 500),
         '③用Ctrl+1/2保存两组友军。第一组W→E，第二组S→N，连续下令使队伍在中心交叉。'
         '观察切向通行与分散，不能靠圈边硬卡或强制位置弹开。'),
        ('HeadOn', c + unreal.Vector(1000, 1000, 500),
         '④两组友军分别在W、E，互换目的地迎面通行；观察双方相对前进方向一致靠右避让。'
         '可各取12/13台重防号；站位均留在中心65米范围内的开阔地。'),
        ('StoppedPush', c + unreal.Vector(0, -1600, 500),
         '⑤在中心停一组友军并按S；另一组W→E穿过。停止友军可被慢推（最高自身速度25%），'
         '压力消失停在新位置，S停止保留、不恢复旧绿线或自动推进。另测静止敌军不被敌方压力推移。'),
        ('CircleSize', ground(CENTER[0], CENTER[1] + 5500) + unreal.Vector(0, 0, 500),
         '尺寸参考：Radius12m5与Radius3m为固定12.5米/3米外径参考圈，无碰撞、不参与导航。'
         '将对应单位移到圈中心比较外缘（目标误差≤1厘米）。圈和模型应共享显示中心；'
         '相位/死亡/外控锁定应沿用排除，导航失败不得穿环境障碍。'),
    ]
    markers = [('C', 0, 0, 75), ('W', -4000, 0, 75), ('E', 4000, 0, 75),
               ('S', 0, -4000, 75), ('N', 0, 4000, 75),
               ('Radius12m5', 0, 5500, 625), ('Radius3m', -1200, 5500, 150)]
    with unreal.ScopedEditorTransaction('Prepare Mass ring and soft avoidance manual review'):
        for label, location, message in notes:
            actor = get_actor('MassSoft_' + label, unreal.Note, location)
            actor.set_editor_property('is_editor_only_actor', True)
            actor.set_editor_property('text', message)
        for label, dx, dy, radius in markers:
            actor = get_actor('MassSoft_' + label, unreal.StaticMeshActor,
                              ground(CENTER[0] + dx, CENTER[1] + dy) + unreal.Vector(0, 0, 8))
            component = actor.static_mesh_component
            component.set_static_mesh(ring)
            component.set_material(0, material)
            component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            component.set_cast_shadow(False)
            actor.set_actor_scale3d(unreal.Vector(radius / native_x, radius / native_y, 0.004))
        camera = get_actor('MassSoft_Overview', unreal.CameraActor,
                           c + unreal.Vector(0, 10000, 17000))
        camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), c), False)
        camera.set_editor_property('is_editor_only_actor', True)
        camera.camera_component.set_editor_property('field_of_view', 60.0)

    after_actors = api.get_all_level_actors()
    unrelated = {a.get_path_name(): record(a) for a in after_actors if TAG not in [str(t) for t in a.tags]}
    assert unrelated == original, 'An unrelated actor changed.'
    assert definition.get_editor_property('layout_hash') == layout_hash
    preview_after = unreal.GuLiResourceAuthoringLibrary.get_initial_army_spawn_layout_json()
    assert json.loads(preview_after) == json.loads(preview_before), 'Army layout changed.'
    validation = unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    assert validation.get_editor_property('success'), validation.get_editor_property('message')
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level(), 'Map save failed.'

    # Re-read only this scene's entities after saving; this does not verify runtime behavior.
    rows = [record(a) for a in api.get_all_level_actors() if TAG in [str(t) for t in a.tags]]
    assert len(rows) == len(notes) + len(markers) + 1
    radii = {label: radius for label, _, _, radius in markers}
    for row in rows:
        key = row['label'].removeprefix('MassSoft_')
        if key in radii:
            row['outer_diameter_cm'] = [row['scale'][0] * native_x * 2, row['scale'][1] * native_y * 2]
            assert max(abs(d - radii[key] * 2) for d in row['outer_diameter_cm']) <= 1.0
            assert row['collision'] == str(unreal.CollisionEnabled.NO_COLLISION)
    report.update(success=True, saved=True, actors=rows, center=list(c.to_tuple()),
                  clear_radius_cm=clear_radius, native_ring_radii_cm=[native_x, native_y],
                  population={'red': 250, 'blue': 250, 'deployment_overrides': 0},
                  army_layout_preserved=True, unrelated_actors_preserved=len(original),
                  resource_layout_hash=layout_hash,
                  resource_validation={k: str(validation.get_editor_property(k)) for k in
                      ['success', 'message', 'initial_soldier_count', 'validated_initial_soldier_count']})


try:
    run()
except Exception:
    report['error'] = traceback.format_exc()
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'scene-delivery.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({k: report[k] for k in
    ['success', 'saved', 'population', 'unrelated_actors_preserved', 'error'] if k in report}, ensure_ascii=False))
