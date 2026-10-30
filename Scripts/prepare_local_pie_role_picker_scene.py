"""Save the original map's role-picker handoff note; never opens or starts PIE."""
import json
import traceback
from pathlib import Path

import unreal

MAP = '/Game/Maps/LVL_CommanderMassPrototype'
LABEL = 'LocalPIERolePicker_Instructions'


def main():
    root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    out = root / 'Artifacts/LocalPIERolePicker20261026'
    out.mkdir(parents=True, exist_ok=True)
    report = dict(success=False, runtime_validation='not_run', visual_validation='player_pending')
    try:
        level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        assert not level.is_in_play_in_editor(), 'Preserve the current play session'
        assert world.get_path_name().split('.')[0] == MAP
        assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Preserve unsaved map edits'
        assert not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Preserve unsaved content edits'
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        matches = [a for a in actors.get_all_level_actors() if a.get_actor_label() == LABEL]
        assert len(matches) <= 1
        location = unreal.Vector(-18900, 70440, 1473.779083)
        note = matches[0] if matches else actors.spawn_actor_from_class(unreal.Note, location)
        assert isinstance(note, unreal.Note)
        note.set_actor_label(LABEL)
        note.set_actor_location(location, False, True)
        note.set_folder_path('Review/LocalPIERolePicker')
        note.set_editor_property('is_editor_only_actor', True)
        note.set_editor_property('tags', [unreal.Name('LocalPIERolePickerReview20261026')])
        instructions = (
            '开局席位选择：打开本图，点击播放。\n'
            '前置：加载含开局UI的新编译；GuLi Local PIE启用，开局选择席位开启。\n'
            '选择指挥官、地面机甲或飞船，再点进入战场；应分别进入已有的对应操控方式。\n'
            '地面机甲为玩家Ground席位，不是指挥官兵种重防号WM01。\n'
            '取消、Esc或关闭窗口应返回编辑器，且不启动对局后台进程。\n'
            '结束对局后再次播放，应重新显示选择界面；本编辑器会话记住上次确认的选项。\n'
            'Tab可切换焦点，Enter/空格操作已聚焦按钮；缩小窗口时选项可滚动。\n'
            '本入口仅用于原型图的交互式本地PIE，不改地图出生点、飞船或僚机的移动行为。\n'
            '助手不运行PIE、不读取截图；玩家确认三个席位、取消/重开及实际布局。')
        note.set_editor_property('text', instructions)
        report['saved'] = bool(level.save_current_level())
        assert report['saved']
        report['entity'] = dict(label=note.get_actor_label(), cls=note.get_class().get_name(),
                                map=world.get_path_name(), location=list(note.get_actor_location().to_tuple()),
                                editor_only=note.get_editor_property('is_editor_only_actor'),
                                folder=str(note.get_folder_path()), text=str(note.get_editor_property('text')))
        assert report['entity']['editor_only'] and report['entity']['text'] == instructions
        settings_class = unreal.load_class(None, '/Script/GuLiStrikeEditor.GuLiLocalPIESettings')
        assert settings_class is not None
        settings = unreal.get_default_object(settings_class)
        report['local_pie_enabled'] = bool(settings.get_editor_property('bEnabled'))
        report['default_role'] = str(settings.get_editor_property('MainRole'))
        try:
            report['prompt_for_role_loaded'] = bool(settings.get_editor_property('bPromptForRole'))
        except Exception:
            report['prompt_for_role_loaded'] = None
            report['code_load_status'] = 'new native property unavailable; build and reload required'
        report['success'] = True
    except Exception:
        report['error'] = traceback.format_exc()
    path = out / 'scene-delivery.json'
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(dict(success=report['success'], saved=report.get('saved'),
        prompt_for_role_loaded=report.get('prompt_for_role_loaded'), error=report.get('error'), report=str(path))))


main()
