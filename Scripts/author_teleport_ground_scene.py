"""Save review markers beside the prototype's real construction/transit gameplay."""
import json
from pathlib import Path
import unreal

MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'TeleportGroundReview20260924'
FOLDER = 'GuLiStrike/Review/TeleportGround'
OUT = Path('D:/UE5.7/test1/Artifacts/TeleportGround20260924')
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = editor.get_editor_world()
assert world.get_path_name() == MAP + '.LVL_CommanderMassPrototype'
assert not level.is_in_play_in_editor(), 'Author only the editor world, outside PIE.'
actors = api.get_all_level_actors()
before = {a.get_path_name(): a.get_actor_transform().to_tuple() for a in actors}
existing = {a.get_actor_label(): a for a in actors}
owned = {a.get_actor_label(): a for a in actors if TAG in [str(t) for t in a.tags]}
entries = [existing[name] for name in ['CommanderPlayerStart_Red', 'StateTreeReview_Construction',
    'StateTreeReview_StrongholdAdvance', 'ConstructionReview_Mining']]
navs = [existing[name] for name in ['RecastNavMesh-Default', 'RecastNavMesh-CommanderSoldier']]


def marker(label, cls, location):
    a = owned.get(label)
    if a is None:
        assert label not in existing, 'Do not replace an unrelated actor.'
        a = api.spawn_actor_from_class(cls, unreal.Vector(*location))
        owned[label] = a
    assert a.get_class() == cls.static_class()
    a.modify()
    a.set_actor_label(label)
    a.set_folder_path(FOLDER)
    a.set_editor_property('tags', [TAG])
    a.set_editor_property('is_editor_only_actor', True)
    a.set_actor_location(unreal.Vector(*location), False, False)
    return a


with unreal.ScopedEditorTransaction('Prepare ground-only teleport landing review'):
    source = marker('TeleportGround_Source', unreal.Note, (3000, 72500, -1200))
    source.set_editor_property('text',
        '传送落地检查：先编译并加载2026-09-24屋顶修复。使用原型图正常指挥官入口、默认工程车与建筑。'
        '按T圈住工程车，收取后把目标放在R1C5兵营附近的空地，使范围包含屋顶；'
        '目标中心直接点屋顶应显示无效，范围覆盖屋顶时车辆应改选建筑外可走地面。'
        '落地后下达普通移动，确认车辆能离开；取消/超时回源也应使用同样的地面检查。')
    roof = marker('TeleportGround_Roof', unreal.Note, (1400, 80000, -1200))
    roof.set_editor_property('text',
        '普通建筑屋顶观察：运行时兵营名义中心XY=(1400,80000)，赠品清场可能偏移槽位。'
        '建筑保留实体碰撞，导航只导出NavArea_Null障碍，不生成屋顶可走面；角色禁止踏上与站立。'
        '传送和赠品挪人均检查真实支撑表面、该单位导航及同层高度；不得落到屋顶或其他单位上。'
        '此Note是观察标记，实际建筑由既有开局/占点赠品流程生成。')
    transit = marker('TeleportGround_TransitExit', unreal.Note, (1400, 60000, -1200))
    transit.set_editor_property('text',
        '据点出口观察：沿用真实部队占领R2C5并生成赠品，选工程车右键己方R2C5据点模型运输。'
        '出口周边含兵营等建筑时，工程车必须在建筑外可走地面落位；无安全位置保持空中等待，10Hz重试。'
        '矿厂坡道与卸货地板仍供正常地面行驶使用；落地后继续观察普通移动及矿车入厂卸货。'
        '占点生成赠品挪开车辆时同样不得把车辆放到邻楼顶部。')
    camera = marker('TeleportGround_Overview', unreal.CameraActor, (6500, 85000, 6000))
    camera.set_actor_rotation(unreal.Rotator(-48, -135, 0), False)

after = api.get_all_level_actors()
for a in after:
    if a.get_path_name() in before and TAG not in [str(t) for t in a.tags]:
        assert before[a.get_path_name()] == a.get_actor_transform().to_tuple()
assert level.save_current_level(), 'Map save failed.'
rows = []
for a in [*owned.values(), *entries, *navs]:
    r = {'label': a.get_actor_label(), 'path': a.get_path_name(), 'class': a.get_class().get_name(),
         'location': a.get_actor_location().to_tuple(), 'rotation': a.get_actor_rotation().to_tuple(),
         'scale': a.get_actor_scale3d().to_tuple(), 'editor_only': a.get_editor_property('is_editor_only_actor')}
    if isinstance(a, unreal.Note):
        r['text'] = str(a.get_editor_property('text'))
    if isinstance(a, unreal.RecastNavMesh):
        r['agent_radius'] = a.get_editor_property('agent_radius')
        r['agent_height'] = a.get_editor_property('agent_height')
    rows.append(r)
assert len(owned) == 4 and all(a.get_editor_property('is_editor_only_actor') for a in owned.values())
report = {'saved': True, 'map': MAP, 'actor_count': len(after), 'related_entities': rows,
          'unrelated_transforms_preserved': True,
          'entry': 'Existing commander T skill, construction vehicles, right-click friendly outpost transit, and gift clearance.',
          'validation': 'Editor entity readback only. Native compilation/loading is documented by separate build and loaded-config receipts; this script does not start PIE or automation.'}
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'scene-delivery.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'saved': True, 'map': MAP, 'read_entities': len(rows), 'actors': len(after)}))
