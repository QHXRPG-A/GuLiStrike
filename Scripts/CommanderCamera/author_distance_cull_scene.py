"""Save a camera-tier review entry beside the existing WM01 fixture; never starts PIE."""
import json
import math
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'CommanderDistanceCullReview20260930'
OUT = ROOT / globals().get('GULI_DISTANCE_SCENE_REPORT',
                          'ArtSource/WarMachineHover_20260930/LOD2/scene-readback.json')
NATIVE_READY = bool(globals().get('GULI_DISTANCE_NATIVE_READY', False))
report = {'success': False, 'saved': False, 'map': MAP, 'runtime_pie': 'not_run',
          'native_build': 'built_and_loaded' if NATIVE_READY else 'not_run_new_lod_native_pending', 'actors': []}

try:
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert not level.is_in_play_in_editor(), 'PIE is active; do not interrupt the player.'
    assert world.get_path_name().split('.')[0] == MAP, 'The designated map must already be open.'
    by_label = {}
    for actor in api.get_all_level_actors():
        by_label.setdefault(actor.get_actor_label(), []).append(actor)

    def existing(label):
        matches = by_label.get(label, [])
        assert len(matches) == 1, label
        return matches[0]

    fixture = existing('WarMachineHover_PlayablePreview')
    meshes = fixture.get_components_by_class(unreal.InstancedStaticMeshComponent)
    assert len(meshes) == 1 and meshes[0].get_instance_count() == 13
    assert meshes[0].get_editor_property('NumCustomDataFloats') == 51
    for label in ['RogueCards_F4_Entry', 'CommanderCamera_Instructions',
                  'CommanderCamera_Observe_Tactical300', 'CommanderCamera_Observe_Tactical700']:
        existing(label)
    camera_rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(
        unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Camera')))
    camera = next(row for row in camera_rows if row['Name'] == 'Default')
    origin = fixture.get_actor_location()
    focus = origin + unreal.Vector(0, 0, 120)

    def upsert(cls, label, position):
        matches = by_label.get(label, [])
        assert len(matches) <= 1, label
        actor = matches[0] if matches else api.spawn_actor_from_class(cls, position)
        assert isinstance(actor, cls), label
        actor.modify()
        actor.set_actor_label(label)
        actor.set_actor_location(position, False, True)
        actor.set_folder_path('Review/CommanderDistanceCull')
        actor.set_editor_property('is_editor_only_actor', True)
        actor.set_editor_property('tags', list(dict.fromkeys([*actor.tags, unreal.Name(TAG)])))
        return actor

    pitch = float(camera['TacticalPitchDegrees'])
    for suffix, key in [('Tactical300', 'TacticalStartHeightMeters'),
                        ('Tactical700', 'TacticalMaximumHeightMeters')]:
        height = float(camera[key]) * 100
        position = focus + unreal.Vector(-height / math.tan(math.radians(pitch)), 0, height)
        actor = upsert(unreal.CameraActor, 'CommanderDistanceCull_Observe_' + suffix, position)
        actor.set_actor_rotation(unreal.Rotator(pitch=-pitch, yaw=0, roll=0), True)
        actor.camera_component.set_field_of_view(float(camera['FieldOfViewDegrees']))
        actor.camera_component.set_editor_property('constrain_aspect_ratio', False)

    note = upsert(unreal.Note, 'CommanderLOD_DistanceCull_Instructions', origin + unreal.Vector(-1500, -1000, 0))
    readiness = ('本轮GuLiStrikeEditor已编译并重开加载；正式Niagara已保存，可由玩家进入指挥官复验。'
                 if NATIVE_READY else
                 '前置：本轮C++尚未编译。需先获准构建并加载最新GuLiStrikeEditor；已有正式Niagara已保存。')
    note.set_editor_property('text',
        '指挥官视觉距离裁剪修复（2026-09-30）\n' + readiness +
        '场景数据回读不代表运行效果验收。\n'
        '规则：第一档近景、第二档战术不再受旧200m/360m/血条60m独立距离裁剪；'
        '第三档总览隐藏世界模型、喷焰、拖尾、战斗特效和世界血条，保留原总览UI。\n'
        '真实入口：本图Play进入指挥官，gs.GM.Commander.Camera.Debug 1显示档位。'
        'Height命令完整前缀gs.GM.Commander.Camera.Height，先35，再300、700；'
        '观察CameraActor仅用于编辑器取景，不替代真实CameraPawn档位。\n'
        '样本：WarMachineHover_PlayablePreview位于(8000,-16000,0)cm，保留13个51float样本；'
        '本入口旁的Tactical300/700观察镜头指向该区域。\n'
        '1. 在第一、第二档观察盘底倒三角始终贴盘；移动起步短尾约1秒渐长到速度对应5–15米，停下0.30秒淡出。'
        '用真实WM01部队移动、停下、转弯复验；静态样本不能代替实战。\n'
        '2. 300m和700m下令实战攻击，观察枪口焰、弹道、持续场、受击、死亡爆炸和残骸；'
        '选中/受伤血条和选择圈不因旧距离消失。解锁导弹仓继续沿RogueCards_F4_Entry真实F4流程，Q导弹沿原技能入口。\n'
        '3. 向外滚轮进入第三档，总览隐藏上述世界表现；向内返回第二档恢复持续表现，新开火正常，不补播旧爆炸。'
        '镜头可继续按CommanderCamera_Instructions检查边缘和平移。\n'
        '4. 有僚机/地面机甲参加战斗时，第二档观察飞行尾迹/火箭跳喷口；本次只改显示规则，非指挥官视图保留原距离后备。\n'
        '请反馈档位、镜头高度及具体消失对象；助手未启动PIE、自动化或性能测试。')

    report['saved'] = bool(level.save_current_level())
    assert report['saved'], 'Map save failed'
    related = [a for a in api.get_all_level_actors() if a.actor_has_tag(TAG)]
    assert len(related) == 3
    for actor in sorted(related, key=lambda a: a.get_actor_label()):
        entry = {'label': actor.get_actor_label(), 'class': actor.get_class().get_name(),
                 'object': actor.get_path_name(), 'location_cm': list(actor.get_actor_location().to_tuple()),
                 'rotation': list(actor.get_actor_rotation().to_tuple()),
                 'editor_only': bool(actor.get_editor_property('is_editor_only_actor'))}
        if isinstance(actor, unreal.CameraActor):
            entry['fov'] = float(actor.camera_component.field_of_view)
        if isinstance(actor, unreal.Note):
            entry['text'] = str(actor.get_editor_property('text'))
        report['actors'].append(entry)
    mesh = fixture.get_components_by_class(unreal.InstancedStaticMeshComponent)[0]
    report['fixture'] = {'label': fixture.get_actor_label(), 'location_cm': list(origin.to_tuple()),
                         'instances': mesh.get_instance_count(),
                         'custom_floats': mesh.get_editor_property('NumCustomDataFloats'),
                         'mesh': mesh.get_editor_property('static_mesh').get_path_name()}
    assert report['fixture']['instances'] == 13 and report['fixture']['custom_floats'] == 51
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(
        unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects')))
    report['vfx_rows'] = [row for row in rows if row['Id'] in [10, 48]]
    report['camera_row'] = camera
    report['game_mode'] = world.get_world_settings().get_editor_property('default_game_mode').get_path_name()
    report['success'] = True
except Exception:
    report['error'] = traceback.format_exc()
OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({key: report.get(key)
    for key in ['success', 'saved', 'map', 'fixture', 'error']}))
