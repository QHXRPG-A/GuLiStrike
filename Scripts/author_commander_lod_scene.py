"""Save commander LOD observation cameras and instructions; never start or stop PIE.

Uses the existing real F4 entry and missile fixture. New native commands require
an authorized build and a subsequent editor reload before player validation.
"""
import json
import sys
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'CommanderLODReview20260929'
REPORT_PATH = ROOT / 'Artifacts/CommanderLOD/20260930/scene-readback.json'


def main():
    report = {'success': False, 'saved': False, 'map': MAP, 'actors': [],
              'runtime_validation': 'not_run', 'performance_validation': 'not_run'}
    try:
        level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        assert not level.is_in_play_in_editor(), 'Leave the player session untouched; run authoring after PIE ends.'
        assert world.get_path_name().split('.')[0] == MAP, 'Load the designated existing map first.'
        # Native-only UCLASS types need not have an unreal Python alias.
        native_lod_class = unreal.find_object(None, '/Script/GuLiStrike.GuLiCommanderLODSubsystem')
        native_ready = native_lod_class is not None
        fixture_source = ROOT / 'Source/GuLiStrikeEditor/Private/GuLiMissileFixture.cpp'
        fixture_module = ROOT / 'Binaries/Win64/UnrealEditor-GuLiStrikeEditor.dll'
        fixture_pending = not fixture_module.exists() or fixture_source.stat().st_mtime > fixture_module.stat().st_mtime
        report['fixture_native_rebuild_pending'] = fixture_pending
        runtime_evidence = ROOT / 'Artifacts/CommanderLOD/20260930/pie-summary.json'
        if runtime_evidence.exists():
            report['runtime_validation'] = json.loads(runtime_evidence.read_text(encoding='utf-8'))['status']
            report['runtime_evidence'] = str(runtime_evidence)
        scripts_path = str(ROOT / 'Scripts')
        if scripts_path not in sys.path:
            sys.path.insert(0, scripts_path)
        from wm01_missile_visual_config import read_profile
        profile, visual_parameters = read_profile(unreal)
        report['visual_profile_row'] = profile['Name']
        report['visual_parameters'] = visual_parameters
        existing = editor.get_all_level_actors()
        by_label = {}
        for item in existing:
            by_label.setdefault(item.get_actor_label(), []).append(item)
        required = ['RogueCards_F4_Entry', 'WM01Missile_FixtureCenter',
                    'WM01Missile_CandidateInstructions', 'WM01Missile_Camera_Overview', 'WM01Missile_Camera_Near']
        required += [f'WM01Missile_Case_{count}x{salvo}' for count in (100, 500) for salvo in (1, 4, 8)]
        for label in required:
            assert len(by_label.get(label, [])) == 1, f'Missing or ambiguous existing fixture actor: {label}'

        def upsert(cls, label, location, tags):
            matches = by_label.get(label, [])
            assert len(matches) <= 1, label
            item = matches[0] if matches else editor.spawn_actor_from_class(cls, unreal.Vector(*location))
            assert isinstance(item, cls), label
            item.set_actor_label(label)
            item.set_actor_location(unreal.Vector(*location), False, True)
            item.set_folder_path('Review/CommanderLOD')
            retained = [str(t) for t in item.tags if str(t) not in [TAG, *tags]]
            item.set_editor_property('tags', [unreal.Name(t) for t in [*retained, TAG, *tags]])
            return item

        center = by_label['WM01Missile_FixtureCenter'][0].get_actor_location()
        focus = unreal.Vector(center.x, center.y - 2000, center.z + 200)
        for tier, offset in enumerate(((0, -4000, 2500), (0, -11000, 6000), (0, -16500, 8000))):
            location = (focus.x + offset[0], focus.y + offset[1], focus.z + offset[2])
            camera = upsert(unreal.CameraActor, f'CommanderLOD_Camera_LOD{tier}', location, [f'CommanderLODCamera{tier}'])
            camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(unreal.Vector(*location), focus), True)
            camera.camera_component.set_field_of_view(65)
            camera.camera_component.set_editor_property('constrain_aspect_ratio', True)
            camera.camera_component.set_editor_property('aspect_ratio', 16 / 9)

        note = upsert(unreal.Note, 'CommanderLOD_Instructions', (center.x, center.y + 4500, center.z + 100), [])
        native_status = ('当前编辑器已加载公共LOD原生类；运行范围与结果以开发记录为准。\n'
                         if native_ready else '需最新原生代码构建并重新加载；当前地图保存与实体回读不代表运行通过。\n')
        if fixture_pending:
            native_status += '夹具间距与驻留修正源码尚未编译加载。旧版Build 100实际仅生成36/37台且会自动前进；不能当作完整人数验收。\n'
        note.set_editor_property('text',
            '指挥官统一自动三级 LOD；本次只有导弹接入，模型/升级特效/悬浮/血条待后续接入。\n'
            + native_status +
            '玩法入口仍为 RogueCards_F4_Entry；性能布景仍为现有六组 100/500 × 1/4/8。\n'
            '手动验证：新局指挥官就绪，v3为正式默认表现，执行 gs.MissileFixture.Build 100 1；'
            '用 gs.MissileFixture.Camera lod0 / lod1 / lod2 切换三个观察位置，gs.MissileFixture.Fire 发射（遵守冷却）。\n'
            '加载修正版夹具后，Build应回报success=true、fixture_units与stopped_units均等于请求人数；当前WM01间距按权威避让直径加20cm计算，为1270cm。人数不符时重新准备场景。\n'
            '飞行中 gs.Commander.LOD.Stats WM01MissileBatches 查看目标/实际档位、裁剪、预算和交接等待；'
            'gs.MissileFixture.Metrics 查看导弹容量；统计对象是空间批次，不是单枚导弹。\n'
            '相机名称表示观察用途，不强制档位；以含历史烟迹的包围盒、当前镜头和预算决定实际结果，多个档位可并存。\n'
            'LOD0/1/2 对应 24/8/0 段烟迹；寿命2.4秒、交接0.15秒。烟迹/尾焰尺寸读取当前Projectiles表，LOD不改写视觉数值。\n'
            '统一配置：gs.Commander.LOD.List；Get near.enter_distance_cm；Set near.enter_distance_cm 7000；Reset all。'
            '以上 Get/Set/Reset 均需完整 gs.Commander.LOD. 前缀，修改只作用当前 World，不写盘。\n'
            '恢复原固定对照镜头：gs.MissileFixture.Camera overview。WM01原生默认使用v3，无需候选或资产开关；旧表现对照用 gs.MissileCluster.Enabled 0，恢复用 1。\n'
            '检查缩放边界往返、预算不足、命中消散、屏外恢复、槽位复用与新局清理。运行和性能验收状态以开发记录为准。')

        marker = '\n[指挥官统一 LOD 20260929]'
        for label in ('RogueCards_F4_Entry', 'WM01Missile_CandidateInstructions'):
            item = by_label[label][0]
            previous = str(item.get_editor_property('text')).split(marker)[0]
            item.set_editor_property('text', previous + marker + '\n公共三级 LOD 和 lod0/lod1/lod2 观察镜头见 CommanderLOD_Instructions。' + native_status.strip())

        report['saved'] = bool(level.save_current_level())
        assert report['saved'], 'Map save failed'
        related = [a for a in editor.get_all_level_actors() if a.actor_has_tag(TAG) or a.get_actor_label() in required]
        assert len(related) == 15
        for item in sorted(related, key=lambda a: a.get_actor_label()):
            row = {'label': item.get_actor_label(), 'class': item.get_class().get_name(),
                   'object': item.get_path_name(), 'location': list(item.get_actor_location().to_tuple()),
                   'rotation': list(item.get_actor_rotation().to_tuple()), 'tags': [str(t) for t in item.tags]}
            if isinstance(item, unreal.Note):
                row['text'] = str(item.get_editor_property('text'))
            if isinstance(item, unreal.CameraActor):
                row['fov'] = float(item.camera_component.field_of_view)
                row['aspect_ratio'] = float(item.camera_component.aspect_ratio)
            report['actors'].append(row)
        for tier in range(3):
            cameras = [a for a in related if a.actor_has_tag(f'CommanderLODCamera{tier}')]
            assert len(cameras) == 1 and isinstance(cameras[0], unreal.CameraActor)
        assert 'gs.Commander.LOD.Stats' in str(note.get_editor_property('text'))
        projectile = unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_Missile')
        report['projectile'] = projectile.get_path_name()
        report['production_cluster_enabled'] = bool(projectile.uses_missile_cluster_rendering())
        report['systems'] = [unreal.load_asset('/Game/GuLiStrike/FX/WM01Missiles/NS_WM01MissileCluster_' + q).get_path_name()
                             for q in ('Full', 'Lite', 'Minimal')]
        report['native_lod_class_loaded'] = native_ready
        report['native_lod_class_path'] = native_lod_class.get_path_name() if native_ready else None
        report['native_lod_python_alias_exists'] = hasattr(unreal, 'GuLiCommanderLODSubsystem')
        report['game_mode'] = world.get_world_settings().get_editor_property('default_game_mode').get_path_name()
        report['success'] = True
    except Exception:
        report['error'] = traceback.format_exc()
    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
    REPORT_PATH.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    unreal.MCPythonHelper.submit_result(json.dumps({'success': report['success'], 'saved': report['saved'],
        'actors': len(report['actors']), 'native_lod_class_loaded': report.get('native_lod_class_loaded'),
        'report': str(REPORT_PATH), 'error': report.get('error')}))


main()
