"""Save terrain-occlusion review markers in the commander map without starting gameplay."""
import json
import traceback
from pathlib import Path

import unreal

MAP = '/Game/Maps/LVL_CommanderMassPrototype'
LANDSCAPE = 'Landscape_CommanderIsland_1800m_v1'
TAG = 'GuLiGreenGuideLineOverlayReview20261004'
FOLDER = 'CommanderIsland/Review/GreenGuideLine'
MATERIAL = '/Game/GuLiStrike/Rendering/CommanderGuideLines/M_CommanderRouteLineOverlay'
ENTRY = 'QA_GreenGuideLine_Entry'
TERRAIN_LABELS = ['QA_GreenGuideLine_TerrainStart', 'QA_GreenGuideLine_TerrainGoal',
                  'QA_GreenGuideLine_TerrainOverview']
ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'outputs/commander-green-guide-line-20261004'
REPORT = {'success': False, 'map': MAP, 'saved': False,
          'new_native_code_executed': False, 'pie_started': False}


def entity(actor):
    row = {'label': actor.get_actor_label(), 'path': actor.get_path_name(),
           'class': actor.get_class().get_name(),
           'location_cm': list(actor.get_actor_location().to_tuple()),
           'rotation': list(actor.get_actor_rotation().to_tuple()),
           'scale': list(actor.get_actor_scale3d().to_tuple()),
           'editor_only': actor.get_editor_property('is_editor_only_actor'),
           'collision': actor.get_actor_enable_collision(),
           'tags': [str(t) for t in actor.tags], 'folder': str(actor.get_folder_path())}
    if isinstance(actor, unreal.Note):
        row['text'] = str(actor.get_editor_property('text'))
    if isinstance(actor, unreal.CameraActor):
        row['field_of_view'] = actor.camera_component.get_editor_property('field_of_view')
    return row


def run():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assert editor.get_game_world() is None, 'Do not interrupt an active play session.'
    world = editor.get_editor_world()
    assert world and world.get_path_name() == MAP + '.LVL_CommanderMassPrototype'
    api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = api.get_all_level_actors()
    by_label = {a.get_actor_label(): a for a in actors}
    entry = by_label.get(ENTRY)
    assert isinstance(entry, unreal.Note) and 'GuLiGreenGuideLineReview20261003' in map(str, entry.tags)
    for label in TERRAIN_LABELS:
        assert label not in by_label or TAG in map(str, by_label[label].tags)
    related = {ENTRY, *TERRAIN_LABELS}
    baseline = {a.get_path_name(): entity(a) for a in actors if a.get_actor_label() not in related}
    selection = api.get_selected_level_actors()
    material = unreal.load_asset(MATERIAL)
    assert isinstance(material, unreal.Material) and material.get_editor_property('disable_depth_test')

    def ground(x, y):
        sample = unreal.LandscapeService.get_height_at_location(LANDSCAPE, x, y)
        assert sample.valid and sample.height > 0, f'Expected land at ({x}, {y})'
        return unreal.Vector(x, y, sample.height)

    def get_actor(label, cls, position):
        actor = by_label.get(label)
        if actor is None:
            actor = api.spawn_actor_from_class(cls, position, transient=False)
        assert isinstance(actor, cls)
        actor.modify()
        actor.set_actor_label(label)
        actor.set_folder_path(FOLDER)
        actor.set_editor_property('tags', [TAG])
        actor.set_editor_property('is_editor_only_actor', True)
        actor.set_actor_hidden_in_game(True)
        actor.set_actor_enable_collision(False)
        actor.set_actor_location(position, False, False)
        return actor

    start = ground(-21681.57, 32687.03)
    goal = ground(-14057.17, 32233.32)
    get_actor(TERRAIN_LABELS[0], unreal.Note, start + unreal.Vector(0, 0, 500)).set_editor_property(
        'text', '绿线跨坡验收起点：先编译加载2026-10-04材质接入代码。'
        '玩家在真实指挥官玩法中把本方单位移到此处并按S，选中后向TerrainGoal下移动令。'
        '在近景、战术镜头及低角度让山坡遮住线的世界位置，绿线应仍连续叠加在地形上，单位端贴合模型中心。')
    get_actor(TERRAIN_LABELS[1], unreal.Note, goal + unreal.Vector(0, 0, 500)).set_editor_property(
        'text', '绿线跨坡验收目标：有效移动指令的共享点击点。'
        '单单位、重防号悬浮及至少150单位移动时，坡地不应造成绿线中段缺失；'
        '单位可以按真实导航绕行，绿线仍为模型中心到有效目标的直线。'
        '改令/拒绝新令及取消选中、停止、到达隐藏规则沿用原Entry验收。')
    camera = get_actor(TERRAIN_LABELS[2], unreal.CameraActor,
                       ground(-24900.0, 37960.0) + unreal.Vector(0, 0, 900))
    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(
        camera.get_actor_location(), (start + goal) * 0.5), False)
    camera.camera_component.set_editor_property('field_of_view', 60.0)
    entry.modify()
    original_text = str(entry.get_editor_property('text')).split('\n2026-10-04跨坡验收：', 1)[0]
    entry.set_editor_property('text', original_text + '\n2026-10-04跨坡验收：'
        '编译加载本次代码后，使用TerrainStart→TerrainGoal和TerrainOverview低角度观察位置。'
        '绿线跨山坡仍连续可见；颜色、粗细、模型中心和有效点击点保持。运行效果待玩家确认。')
    api.set_selected_level_actors(selection)
    unchanged = {a.get_path_name(): entity(a) for a in api.get_all_level_actors()
                 if a.get_actor_label() not in related}
    assert unchanged == baseline, 'An unrelated actor changed.'
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
    rows = [entity(a) for a in api.get_all_level_actors() if a.get_actor_label() in related]
    assert len(rows) == 4 and all(r['editor_only'] and not r['collision'] for r in rows)
    assert all(r['folder'] == FOLDER for r in rows)
    assert editor.get_game_world() is None
    REPORT.update(success=True, saved=True, entities=rows, entry=ENTRY,
                  observation_camera=TERRAIN_LABELS[2], material=MATERIAL,
                  material_disable_depth_test=True, unrelated_actors_preserved=len(baseline),
                  points_cm={'start': list(start.to_tuple()), 'goal': list(goal.to_tuple())})


try:
    run()
except Exception:
    REPORT['error'] = traceback.format_exc()
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'scene-delivery.json').write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({k: REPORT[k] for k in
    ['success', 'saved', 'entry', 'observation_camera', 'error'] if k in REPORT}, ensure_ascii=False))
