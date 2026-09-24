"""Prepare editor-only observation points for the existing Mass/building gameplay scene."""
import json
from pathlib import Path
import unreal

MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'MassSurfaceRepairReview20260924'
FOLDER = 'GuLiStrike/Review/MassNavigation/SurfaceRepair'
OUT = Path('D:/UE5.7/test1/Artifacts/MassNavRepair20260924')
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = editor.get_editor_world()
assert world.get_path_name() == MAP + '.LVL_CommanderMassPrototype'
assert not level.is_in_play_in_editor(), 'Author only the editor world, outside PIE.'
actors = api.get_all_level_actors()
before = {a.get_path_name(): (a.get_actor_location().to_tuple(), a.get_actor_scale3d().to_tuple()) for a in actors}
existing = {a.get_actor_label(): a for a in actors}
owned = {a.get_actor_label(): a for a in actors if TAG in [str(t) for t in a.tags]}
walls = [existing['MassNav_' + name] for name in ['CornerLong', 'CornerReturn', 'NarrowLeft', 'NarrowRight',
         'ClosedNorth', 'ClosedSouth', 'ClosedWest', 'ClosedEast']]
assert all(isinstance(a, unreal.StaticMeshActor) for a in walls)

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

with unreal.ScopedEditorTransaction('Prepare Mass 10 Hz navigation surface repair observations'):
    note = marker('MassNavRepair_GiftEdge', unreal.Note, (-17900, 60000, -1777.22))
    note.set_editor_property('text',
        '10Hz导航坐标恢复：本次重复现场赠品中心XY=(-17900,60000)，放置候选可偏移700cm。'
        '使用默认军队及正常赠品流程，进入PIE后观察约第20秒穿过这里的单位。'
        '建筑恢复碰撞后应重算完整导航范围，实体内部不得遗留可行走小岛。'
        '若重建使脚下NavMesh引用失效，每0.1秒检测，查询按世界帧预算推进；安全落点水平最多600cm、高差50cm，'
        '扫掠阻挡或无安全落点则继续等待。恢复后应继续原命令、原随机站位，绿线起终点保持。'
        '在恢复等待期间改令、S停止、运输或杀死单位，旧结果不得恢复旧任务。此Note不生成建筑、不启动PIE。')
    note = marker('MassNavRepair_FactoryEdge', unreal.Note, (-1523.50, -40727.17, -1485.49))
    note.set_editor_property('text',
        '第二现场观察点：原工厂中心XY=(-1400,-40000)，受阻单位XY=(-1523,-40727)，'
        '原10cm脚下重投影失败，最近导航面约28cm。使用正常建造/赠品流程观察相同的边缘导航重建。'
        '安全恢复必须同时通过NavMesh落点、建筑碰撞扫掠和矿体/机甲阻挡检查；无法安全恢复时保持任务。'
        '诊断：gs.GM.Commander.Nav.Soldier <ID>；get_move_response_diagnostics含recovery_detection_hz/steps/queries/repairs和nav_repair_pending。')
    camera = marker('MassNavRepair_Overview', unreal.CameraActor, (-23000, 65000, 6000))
    camera.set_actor_rotation(unreal.Rotator(-47, -44, 0), False)
    observe = existing['MassNav_OreMechObserve']
    observe.modify()
    observe.set_editor_property('text',
        '矿体与机甲：保留行进避障、绕行侧滞回、最后合法位置与真实速度回写。'
        '新增10Hz脚下导航失效恢复，建筑边缘观察点见SurfaceRepair文件夹的GiftEdge/FactoryEdge及Overview相机。'
        '复用正常B建造和赠品生成入口；需要先编译加载本次代码。'
        '恢复查询共用2ms/64位置/4路径预算，不新增个人寻路；失败继续原意图。'
        '墙角与Closed封闭区用于观察不可穿墙、受阻保留任务及S停止后旧线不复活。')

after = api.get_all_level_actors()
for a in after:
    if a.get_path_name() in before and TAG not in [str(t) for t in a.tags]:
        assert before[a.get_path_name()] == (a.get_actor_location().to_tuple(), a.get_actor_scale3d().to_tuple())
assert level.save_current_level(), 'Map save failed.'
rows = []
for a in api.get_all_level_actors():
    if a not in owned.values() and a not in walls and a != observe:
        continue
    r = {'label': a.get_actor_label(), 'path': a.get_path_name(), 'class': a.get_class().get_name(),
         'location': a.get_actor_location().to_tuple(), 'rotation': a.get_actor_rotation().to_tuple(),
         'scale': a.get_actor_scale3d().to_tuple(), 'editor_only': a.get_editor_property('is_editor_only_actor')}
    if isinstance(a, unreal.Note):
        r['text'] = str(a.get_editor_property('text'))
    if isinstance(a, unreal.StaticMeshActor):
        r['mesh'] = a.static_mesh_component.static_mesh.get_path_name()
        r['collision'] = str(a.static_mesh_component.get_collision_profile_name())
        assert r['collision'] == 'BlockAll'
    rows.append(r)
assert len(owned) == 3 and all(a.get_editor_property('is_editor_only_actor') for a in owned.values())
report = {'saved': True, 'map': MAP, 'actor_count': len(after), 'related_entities': rows,
          'unrelated_transforms_preserved': True, 'existing_collision_walls': len(walls),
          'trigger': 'Existing commander B construction and normal gift-building spawn gameplay.',
          'validation': 'Editor entity readback only. This script does not build C++ or start PIE; consult the separate build/load receipts.'}
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'scene-delivery.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'saved': True, 'map': MAP, 'read_entities': len(rows), 'actors': len(after)}))
