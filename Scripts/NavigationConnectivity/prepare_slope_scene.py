"""Prepare the existing Commander navigation probes and display; never bakes or plays."""
import json
import unreal

MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'GuLiNavigationConnectivityQA'
SLOPE_TAG = 'GuLiCommanderSlopeProfileQA'
LABELS = (
    'QA_NavConnectivity_FootA_135',
    'QA_NavConnectivity_FootB_138',
    'QA_NavConnectivity_PlateauA_135',
)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = editor.get_editor_world()
assert world.get_path_name() == MAP + '.LVL_CommanderMassPrototype', world.get_path_name()
assert editor.get_game_world() is None, 'Stop PIE before authoring the verification scene.'

inventory = actors.get_all_level_actors()
probes = [actor for actor in inventory if TAG in map(str, actor.tags)]
assert len(probes) == len(LABELS), 'Exactly three saved navigation probes are required.'
notes = {actor.get_actor_label(): actor for actor in probes}
assert set(notes) == set(LABELS), 'The three saved navigation probe notes are required.'
positions = {label: list(notes[label].get_actor_location().to_tuple()) for label in LABELS}


def point_command(start, end):
    return 'gs.Navigation.CheckPoints ' + ' '.join(f'{value:.10f}' for value in (*start, *end))


foot_pair = point_command(positions[LABELS[0]], positions[LABELS[1]])
uphill_pair = point_command(positions[LABELS[0]], positions[LABELS[2]])
texts = (
    '坡面导航修正验证：山脚 A / 单位 135 原位置\n'
    '目标预设：CommanderSoldier 半径150cm、台阶20cm、坡度44度；格子XY 19/19/9.5cm、Z 2cm。\n'
    '编译并重启后执行现有导航 Prepare，再核对山脚和高台连接。\n'
    '同山脚区域预期双向 Connected：\n' + foot_pair + '\n'
    '山脚至高台检查：\n' + uphill_pair + '\n'
    '合法缓坡应双向连接；真实陡坡隔离应定位断区并保持烘焙失败。\n'
    '完整图：gs.Navigation.CheckConnectivity。所有可行走地面必须属于同一强连通区域。\n'
    '本标记不执行烘焙、查询、PIE 或自动化测试。',
    '坡面导航修正验证：山脚 B / 单位 138 原位置\n'
    '与山脚 A 的双向连接应保留：\n' + foot_pair,
    '坡面导航修正验证：高台 A / 单位 135 原目标\n'
    '检查新预设下山脚至高台的连通性：\n' + uphill_pair + '\n'
    '此前旧参数下不连通。新预设效果待重建核对；Disconnected 时读取断区坐标及边界。',
)

for label, text in zip(LABELS, texts):
    note = notes[label]
    assert isinstance(note, unreal.Note) and note.get_editor_property('is_editor_only_actor'), label
    assert not note.get_actor_enable_collision(), label
    note.set_editor_property('tags', list(dict.fromkeys([*map(str, note.tags), SLOPE_TAG])))
    note.set_editor_property('text', text)

navigation = [actor for actor in inventory if isinstance(actor, unreal.RecastNavMesh)]
by_name = {actor.get_name(): actor for actor in navigation}
assert 'RecastNavMesh-CommanderSoldier' in by_name and 'RecastNavMesh-Default' in by_name
for name, nav in by_name.items():
    nav.set_editor_property('enable_drawing', name == 'RecastNavMesh-CommanderSoldier')
by_name['RecastNavMesh-CommanderSoldier'].set_editor_property('draw_offset', 50.0)

assert unreal.EditorLoadingAndSavingUtils.save_map(world, MAP), 'Commander map save failed.'
entities = []
for label, text in zip(LABELS, texts):
    note = notes[label]
    assert note.get_editor_property('text') == text and SLOPE_TAG in map(str, note.tags)
    assert list(note.get_actor_location().to_tuple()) == positions[label]
    entities.append({'label':label, 'path':note.get_path_name(), 'position_cm':positions[label],
                     'editor_only':bool(note.get_editor_property('is_editor_only_actor')),
                     'collision':note.get_actor_enable_collision(), 'text':text})

nav_rows = []
for name, nav in by_name.items():
    drawing = bool(nav.get_editor_property('enable_drawing'))
    assert drawing == (name == 'RecastNavMesh-CommanderSoldier'), name
    resolutions = [{key:float(value.get_editor_property(key))
                    for key in ('CellSize', 'CellHeight', 'AgentMaxStepHeight')}
                   for value in nav.get_editor_property('nav_mesh_resolution_params')]
    offset = float(nav.get_editor_property('draw_offset'))
    if name == 'RecastNavMesh-CommanderSoldier':
        assert offset == 50.0
    nav_rows.append({'name':name, 'drawing':drawing, 'draw_offset_cm':offset, 'resolutions':resolutions,
                     'ledge_filter':str(nav.get_editor_property('ledge_slope_filter_mode')),
                     'max_search_nodes':str(unreal.ActorService.get_property(name, 'DefaultMaxSearchNodes'))})

result = {'success':True, 'map':MAP, 'saved':True, 'entities':entities, 'navigation':nav_rows,
          'commands':{'foot_pair':foot_pair, 'uphill_pair':uphill_pair, 'whole_graph':'gs.Navigation.CheckConnectivity'},
          'authoring_only':True, 'navigation_bake_executed':False, 'point_queries_executed':False, 'pie_started':False}
unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=False))
