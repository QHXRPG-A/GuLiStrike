"""Save editor-only navigation probe notes in the real Commander map; never plays or bakes."""
import json
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'GuLiNavigationConnectivityQA'
FOLDER = 'CommanderIsland/Review/NavigationConnectivity'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = editor.get_editor_world()
assert world.get_path_name() == MAP + '.LVL_CommanderMassPrototype', world.get_path_name()
assert editor.get_game_world() is None, 'Scene authoring requires PIE to be stopped.'

evidence = json.loads((ROOT / 'outputs/commander-uphill-routing-20261001/connectivity-native-audit.json').read_text(encoding='utf-8-sig'))['result']
queries = {query['id']: query for query in evidence['runtime_queries']}
foot_a, foot_b, top_a = queries[135]['start'], queries[138]['start'], queries[135]['goal']


def point_command(start, end):
    return 'gs.Navigation.CheckPoints ' + ' '.join(f'{value:.10f}' for value in (*start, *end))


connected = point_command(foot_a, foot_b)
disconnected = point_command(foot_a, top_a)
specs = [
    ('QA_NavConnectivity_FootA_135', foot_a,
     '导航连通性验证：山脚 A / 原选中单位 135\n'
     '新代码编译并重启后，在编辑器 Output Log 执行下列只读命令。\n'
     '同山脚区域预期 Connected（正反均可达）：\n' + connected + '\n'
     '山脚到高台此前为 Disconnected；坡面修正重建后重新核对双向连接：\n' + disconnected + '\n'
     '完整导航图检查：gs.Navigation.CheckConnectivity\n'
     '实际导航 Prepare 有多个可行走分区时必须报错；本标记不执行烘焙或 PIE。'),
    ('QA_NavConnectivity_FootB_138', foot_b,
     '导航连通性验证：山脚 B / 原选中单位 138\n'
     '与山脚 A 的两点查询预期 Connected。坐标单位厘米，投影范围 X/Y 10、Z 50。\n' + connected),
    ('QA_NavConnectivity_PlateauA_135', top_a,
     '导航连通性验证：高台 A / 原单位 135 目标\n'
     '此前记录：端点均有导航覆盖，正反路径为部分路径，未耗尽搜索节点。\n'
     '坡面新预设重建后核对连接；真实陡坡断区应保持失败并给出定位。\n' + disconnected),
]

inventory = actors.get_all_level_actors()
existing = [actor for actor in inventory if TAG in map(str, actor.tags)]
by_label = {actor.get_actor_label(): actor for actor in existing}
assert len(by_label) == len(existing), 'Duplicate connectivity probe labels.'
assert set(by_label) <= {spec[0] for spec in specs}, 'Unexpected existing probe; no unrelated objects will be deleted.'
selection = actors.get_selected_level_actors()

for label, position, text in specs:
    note = by_label.get(label)
    if note is None:
        note = actors.spawn_actor_from_class(unreal.Note, unreal.Vector(*position), transient=False)
        assert note is not None, label
    assert isinstance(note, unreal.Note), label
    note.set_actor_label(label)
    note.set_folder_path(FOLDER)
    assert note.set_actor_location(unreal.Vector(*position), False, False)
    note.set_editor_property('tags', [TAG])
    note.set_editor_property('is_editor_only_actor', True)
    note.set_actor_hidden_in_game(True)
    note.set_actor_enable_collision(False)
    note.set_editor_property('text', text)

actors.set_selected_level_actors(selection)
saved = unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
assert saved, 'Commander map save failed.'

readback = [actor for actor in actors.get_all_level_actors() if TAG in map(str, actor.tags)]
assert len(readback) == len(specs)
expected = {label: (position, text) for label, position, text in specs}
entities = []
for note in readback:
    label = note.get_actor_label()
    position, text = expected[label]
    assert isinstance(note, unreal.Note) and note.get_editor_property('is_editor_only_actor')
    actual = list(note.get_actor_location().to_tuple())
    assert max(abs(a - b) for a, b in zip(actual, position)) < .01
    assert note.get_editor_property('text') == text
    assert note.get_world().get_path_name() == world.get_path_name()
    assert str(note.get_folder_path()) == FOLDER
    assert not note.get_actor_enable_collision()
    entities.append({'label': label, 'path': note.get_path_name(), 'class': note.get_class().get_path_name(),
                     'position_cm': actual, 'editor_only': True, 'collision': False, 'text': text})

result = {'success': True, 'map': MAP, 'saved': saved, 'probe_count': len(entities), 'entities': entities,
          'commands': {'connected_pair': connected, 'disconnected_pair': disconnected,
                       'whole_graph': 'gs.Navigation.CheckConnectivity'},
          'authoring_only': True, 'new_native_code_executed': False, 'pie_started': False, 'navigation_bake_executed': False}
destination = ROOT / 'outputs/navigation-connectivity-20261002/scene-entities.json'
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=False))
