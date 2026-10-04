"""Save green-guide-line review notes in the real commander map without starting gameplay."""
import json
import traceback
from pathlib import Path

import unreal

MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'GuLiGreenGuideLineReview20261003'
FOLDER = 'CommanderIsland/Review/GreenGuideLine'
ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'outputs/commander-green-guide-line-20261003'
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
           'folder': str(actor.get_folder_path())}
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
    fixtures = ['QA_UnreachableMove_Start', 'QA_UnreachableMove_OldGoal',
                'QA_UnreachableMove_RejectedGoal']
    assert all(name in by_label for name in fixtures), 'Existing commander review markers are required.'
    start, goal, rejected = [by_label[name].get_actor_location() - unreal.Vector(0, 0, 300)
                             for name in fixtures]
    owned = {a.get_actor_label(): a for a in actors if TAG in map(str, a.tags)}
    baseline = {a.get_path_name(): entity(a) for a in actors if a not in owned.values()}
    selection = api.get_selected_level_actors()

    def get_actor(label, cls, position):
        actor = owned.get(label)
        if actor is None:
            actor = api.spawn_actor_from_class(cls, position, transient=False)
            owned[label] = actor
        assert actor and isinstance(actor, cls)
        actor.modify()
        actor.set_actor_label(label)
        actor.set_folder_path(FOLDER)
        actor.set_editor_property('tags', [TAG])
        actor.set_editor_property('is_editor_only_actor', True)
        actor.set_actor_hidden_in_game(True)
        actor.set_actor_enable_collision(False)
        actor.set_actor_location(position, False, False)
        return actor

    specs = [
        ('Entry', start + unreal.Vector(0, 0, 600),
         '绿色引导线：需先编译加载本次代码，玩家自行进入真实指挥官玩法。'
         '先把本方单位移到Start附近并按S停止，选单兵向Goal下移动令。'
         '预期单位端持续连到画面模型中心，目标端保持有效命令的点击点。'
         '重防号悬浮和转向时也应贴合；再选择至少150人确认已出现的线全部逐帧跟随。'
         '取消选中、停止、到达后隐藏；模型表现恢复后能重新显示。'),
        ('Start', start + unreal.Vector(0, 0, 500),
         '从此处开始观察：先将真实本方单位移到附近并停止。'
         '单兵和重防号分别向Goal下令，在近景观察模型中心连接，含转向、预测和悬浮。'),
        ('Goal', goal + unreal.Vector(0, 0, 500),
         '沿用已有合法目标。绿色线指向当前有效指令的点击点，'
         '各单位最终随机站位可以不同；单位移动时只更新其模型中心一端。'
         '已显示线不受128条新增预算限制。'),
        ('RejectedGoal', rejected + unreal.Vector(0, 0, 500),
         '沿用已有不可达目标：合法移动途中点此处，预期拒绝新令并保留旧目标绿线。'
         '很近的新点击沿用当前复用规则；有效远处新令才切换目标。'
         '不可达性沿用既有场景记录，本脚本不重建导航或运行游戏。'),
    ]
    for suffix, position, text in specs:
        get_actor('QA_GreenGuideLine_' + suffix, unreal.Note, position).set_editor_property('text', text)
    midpoint = (start + goal) * 0.5
    camera = get_actor('QA_GreenGuideLine_Overview', unreal.CameraActor,
                       midpoint + unreal.Vector(15000, -35000, 65000))
    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), midpoint), False)
    camera.camera_component.set_editor_property('field_of_view', 60.0)
    api.set_selected_level_actors(selection)
    unchanged = {a.get_path_name(): entity(a) for a in api.get_all_level_actors()
                 if TAG not in map(str, a.tags)}
    assert unchanged == baseline, 'An unrelated actor changed.'
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
    rows = [entity(a) for a in api.get_all_level_actors() if TAG in map(str, a.tags)]
    assert len(rows) == 5 and all(r['editor_only'] and not r['collision'] for r in rows)
    assert all(r['folder'] == FOLDER for r in rows)
    assert editor.get_game_world() is None
    REPORT.update(success=True, saved=True, entities=rows,
                  entry='QA_GreenGuideLine_Entry', observation_camera='QA_GreenGuideLine_Overview',
                  source_fixture=fixtures, unrelated_actors_preserved=len(baseline),
                  points_cm={'start': list(start.to_tuple()), 'goal': list(goal.to_tuple()),
                             'rejected_goal': list(rejected.to_tuple())})


try:
    run()
except Exception:
    REPORT['error'] = traceback.format_exc()
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'scene-delivery.json').write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({k: REPORT[k] for k in
    ['success', 'saved', 'entry', 'observation_camera', 'error'] if k in REPORT}, ensure_ascii=False))
