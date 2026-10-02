"""Save manual move-rejection review markers in the Commander source map."""
import json
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'outputs/commander-unreachable-move-20261002'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'GuLiUnreachableMoveReview20261002'
FOLDER = 'CommanderIsland/Review/UnreachableMove'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_editor_world()
assert world.get_path_name() == MAP + '.LVL_CommanderMassPrototype'
assert editor.get_game_world() is None
api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
selection = api.get_selected_level_actors()

points = json.loads((OUT / 'fixture-point-data.json').read_text(encoding='utf-8'))['result']['pairs']
rejected = next(p for p in points if p['name'] == 'unreachable')
accepted = next(p for p in points if p['name'] == 'old_goal')
assert rejected['query_succeeded'] and not rejected['start_to_end'] and not rejected['end_to_start']
assert accepted['query_succeeded'] and accepted['start_to_end'] and accepted['end_to_start']
start, target, old_goal = rejected['start_cm'], rejected['end_cm'], accepted['end_cm']
existing = {a.get_actor_label(): a for a in api.get_all_level_actors() if TAG in map(str, a.tags)}

specs = [
    ('QA_UnreachableMove_Entry', start,
     '不可达新指令验收：本次原生代码需先编译加载。真实地图仍有导航断区，'
     '严格烘焙认证未通过；本场景不改变门禁。\n'
     '将本方地面单位移动到 Start 附近，按 S 停止，再对 RejectedGoal 下移动令：'
     '预期本次目标线消失、选择和停止状态保留。\n'
     '再向 OldGoal 下合法移动令，途中对 RejectedGoal 下令：'
     '预期继续走向 OldGoal，旧目标绿线保留；也核对 Shift 不追加无效目标。\n'
     '此处为编辑器说明，不会启动 PIE 或自动验收。'),
    ('QA_UnreachableMove_Start', start,
     f'主区域落点（cm）：{start}。先将真实本方单位移到附近。'
     '来源是当前源世界 NavMesh 和 Landscape 的读取数据。'),
    ('QA_UnreachableMove_OldGoal', old_goal,
     f'合法旧目标（cm）：{old_goal}。当前与 Start 双向可达；'
     '先下此令，再点 RejectedGoal，单位应继续原路线。'),
    ('QA_UnreachableMove_RejectedGoal', target,
     f'应拒绝的新目标（cm）：{target}。当前与 Start 双向不连通；'
     '下令不能取消原任务、清空队列、改变速度或替换旧目标。'),
]


def configure(actor, label, location):
    actor.modify()
    actor.set_actor_label(label)
    actor.set_folder_path(FOLDER)
    actor.set_editor_property('tags', [TAG])
    actor.set_editor_property('is_editor_only_actor', True)
    actor.set_actor_hidden_in_game(True)
    actor.set_actor_enable_collision(False)
    actor.set_actor_location(location, False, False)


for label, point, text in specs:
    location = unreal.Vector(*point) + unreal.Vector(0, 0, 300)
    actor = existing.get(label)
    if actor is None:
        actor = api.spawn_actor_from_class(unreal.Note, location, transient=False)
    assert isinstance(actor, unreal.Note)
    configure(actor, label, location)
    actor.set_editor_property('text', text)

label = 'QA_UnreachableMove_Overview'
location = unreal.Vector(6000, -14000, 26000)
camera = existing.get(label)
if camera is None:
    camera = api.spawn_actor_from_class(unreal.CameraActor, location, transient=False)
configure(camera, label, location)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location, unreal.Vector(-3200, -1700, 900)), False)
camera.camera_component.set_editor_property('field_of_view', 60)
api.set_selected_level_actors(selection)
saved = unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
assert saved

entities = []
for actor in api.get_all_level_actors():
    if TAG not in map(str, actor.tags):
        continue
    assert actor.get_editor_property('is_editor_only_actor') and not actor.get_actor_enable_collision()
    row = {'label': actor.get_actor_label(), 'path': actor.get_path_name(),
           'class': actor.get_class().get_path_name(), 'location_cm': list(actor.get_actor_location().to_tuple()),
           'editor_only': True, 'collision': False, 'folder': str(actor.get_folder_path())}
    if isinstance(actor, unreal.Note):
        row['text'] = str(actor.get_editor_property('text'))
    entities.append(row)
assert len(entities) == 5
assert editor.get_game_world() is None
result = {'success': True, 'map': MAP, 'saved': saved, 'entities': entities,
          'points_cm': {'start': start, 'old_goal': old_goal, 'rejected_goal': target},
          'entry': 'QA_UnreachableMove_Entry', 'observation_camera': label,
          'authoring_only': True, 'new_native_code_executed': False,
          'pie_started': False, 'navigation_rebuilt': False, 'bake_certification_changed': False}
(OUT / 'scene-entities.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=False))
