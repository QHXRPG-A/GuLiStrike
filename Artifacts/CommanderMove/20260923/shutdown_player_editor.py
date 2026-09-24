import json
import os
import time
from pathlib import Path

import unreal


_shutdown_record = Path('D:/UE5.7/test1/Artifacts/CommanderMove/20260923/player-editor-shutdown.json')
_shutdown_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
_shutdown_levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
_shutdown_data = {'pid': os.getpid(), 'requested_at': time.time()}
_shutdown_callback = None
_shutdown_deadline = time.monotonic() + 45.0


def _shutdown_audit():
    world = _shutdown_editor.get_game_world()
    return {
        'game_world': world.get_path_name() if world else None,
        'dirty_maps': [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
        'dirty_content': [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
    }


def _shutdown_write():
    _shutdown_record.write_text(json.dumps(_shutdown_data, ensure_ascii=False, indent=2), encoding='utf-8')


def _shutdown_after_end(_delta):
    global _shutdown_callback
    try:
        after = _shutdown_audit()
        if after['game_world'] and time.monotonic() < _shutdown_deadline:
            return
        unreal.unregister_slate_post_tick_callback(_shutdown_callback)
        _shutdown_callback = None
        _shutdown_data['after'] = after
        if after['game_world']:
            _shutdown_data['status'] = 'end_play_timeout'
        elif after['dirty_maps'] or after['dirty_content']:
            _shutdown_data['status'] = 'stopped_unsaved_content'
        else:
            _shutdown_data['status'] = 'clean_exit_requested'
        _shutdown_write()
        if _shutdown_data['status'] == 'clean_exit_requested':
            unreal.SystemLibrary.quit_editor()
    except Exception as error:
        _shutdown_data['status'] = 'callback_error'
        _shutdown_data['error'] = repr(error)
        _shutdown_write()


assert os.getpid() == 39864, 'Refuse to close an unexpected editor process'
_shutdown_data['before'] = _shutdown_audit()
if _shutdown_data['before']['dirty_maps'] or _shutdown_data['before']['dirty_content']:
    _shutdown_data['status'] = 'unsaved_content_no_action'
    _shutdown_write()
else:
    _shutdown_data['status'] = 'end_play_requested'
    _shutdown_write()
    _shutdown_levels.editor_request_end_play()
    _shutdown_callback = unreal.register_slate_post_tick_callback(_shutdown_after_end)
