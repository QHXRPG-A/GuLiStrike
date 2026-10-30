"""Update the existing WM01 preview instructions without running the preview."""
import json
import traceback
from pathlib import Path

import unreal

MAP = '/Game/Maps/LVL_CommanderMassPrototype'
PREVIEW = 'WarMachineHover_PlayablePreview'
NOTE = 'WarMachineHover_PreviewInstructions'
CAMERA = 'WarMachineTurn_ReviewCamera'
MARKER = '\n\n[WM01 Idle 20261026]'


def main():
    out = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / 'Artifacts/WarMachineIdle20261026/Tuning_0p8m_5s_3p2deg'
    out.mkdir(parents=True, exist_ok=True)
    report = dict(success=False, runtime_validation='not_run', visual_validation='player_pending')
    try:
        level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        assert not level.is_in_play_in_editor(), 'Preserve the current play session'
        assert world.get_path_name().split('.')[0] == MAP
        assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map edits'
        assert not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved content edits'
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
        related = {}
        for label in (PREVIEW, NOTE, CAMERA):
            matches = [a for a in actors if a.get_actor_label() == label]
            assert len(matches) == 1, 'Expected one existing entity: ' + label
            related[label] = matches[0]
        preview, note, camera = related[PREVIEW], related[NOTE], related[CAMERA]
        assert isinstance(note, unreal.Note)
        assert note.get_editor_property('is_editor_only_actor')
        assert 'WarMachineHoverPreview20260929' in [str(t) for t in preview.tags]
        models = preview.get_component_by_class(unreal.InstancedStaticMeshComponent)
        assert models and models.get_instance_count() == 13
        assert models.get_editor_property('num_custom_data_floats') == 51
        assert models.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION
        location = list(preview.get_actor_location().to_tuple())
        instructions = (
            '重防号待机：上下各0.8米，总行程1.6米；浮动、俯仰和侧倾均5秒循环。\n'
            '俯仰/侧倾各3.2度、相差90度，整机四角依次起伏；不同单位起始相位不同。\n'
            '前置：编译并加载本次机械动画C++及DefaultGame.ini后，在本图选择指挥官席位进入。\n'
            f'复用预览Actor {PREVIEW}，当前位置厘米 {location}，观察相机 {CAMERA}。\n'
            '现有13例保留：第1例Idle连续观察至少11秒；第6例启停观察0.35秒平滑过渡。\n'
            '预览通过现有StepHover/BuildVisualFrame/ResolveVisualNozzle驱动，缩放0.2；正式部队当前缩放0.192，浮动均为世界厘米。\n'
            '实战选本方重防号WM01（不是玩家地面机甲），停止、移动再停止，离屏再返回；检查喷口和枪口跟随。\n'
            '保持30米低空逻辑高度、现有120厘米表现基准、模型尺寸和寻路碰撞规则。\n'
            'gs.Commander.HoverPreview 0关闭现有预览，1恢复；不使用仅适用于500兵旧场景的HoverQA.Hold。\n'
            '助手仅核对实体和源码，不启动PIE、不读图；循环观感及实战跟随由玩家确认。')
        previous = str(note.get_editor_property('text')).split(MARKER, 1)[0]
        expected = previous + MARKER + '\n' + instructions
        if str(note.get_editor_property('text')) != expected:
            note.modify()
            note.set_editor_property('text', expected)
        report['saved'] = bool(level.save_current_level())
        assert report['saved']
        report.update(map=MAP, preview=dict(label=preview.get_actor_label(), path=preview.get_path_name(),
            location_cm=location, instances=models.get_instance_count(),
            instance_scale=list(models.get_instance_transform(0, world_space=True).scale3d.to_tuple()),
            mesh=models.get_editor_property('static_mesh').get_path_name(),
            custom_floats=models.get_editor_property('num_custom_data_floats'),
            collision=str(models.get_collision_enabled()), tags=[str(t) for t in preview.tags]),
            note=dict(label=note.get_actor_label(), path=note.get_path_name(),
                location_cm=list(note.get_actor_location().to_tuple()),
                editor_only=note.get_editor_property('is_editor_only_actor'), text=str(note.get_editor_property('text'))),
            camera=dict(label=camera.get_actor_label(), location_cm=list(camera.get_actor_location().to_tuple()),
                editor_only=camera.get_editor_property('is_editor_only_actor')))
        assert report['note']['text'] == expected and report['note']['editor_only']
        report['success'] = True
    except Exception:
        report['error'] = traceback.format_exc()
    path = out / 'scene-delivery.json'
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(dict(success=report['success'], saved=report.get('saved'),
        error=report.get('error'), report=str(path)), ensure_ascii=False))


main()
