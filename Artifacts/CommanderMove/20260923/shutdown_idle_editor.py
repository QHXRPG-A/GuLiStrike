import os
import time
from pathlib import Path

assert os.getpid() == 39884, 'Unexpected editor process'
_idle_audit = {
    'pid': os.getpid(),
    'game_world': str(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()),
    'dirty_maps': [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
    'dirty_content': [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
}
assert _idle_audit['game_world'] == 'None', _idle_audit
assert not _idle_audit['dirty_maps'] and not _idle_audit['dirty_content'], _idle_audit
Path('D:/UE5.7/test1/Artifacts/CommanderMove/20260923/idle-editor-shutdown.json').write_text(json.dumps(_idle_audit, indent=2), encoding='utf-8')
_idle_quit_after = time.monotonic() + 1.0

def _idle_quit_tick(_delta):
    if time.monotonic() < _idle_quit_after:
        return
    unreal.unregister_slate_post_tick_callback(_idle_quit_handle)
    unreal.SystemLibrary.quit_editor()

_idle_quit_handle = unreal.register_slate_post_tick_callback(_idle_quit_tick)
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'audit': _idle_audit, 'clean_exit_scheduled': True}))
