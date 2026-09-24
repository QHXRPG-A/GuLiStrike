import os
import time
from pathlib import Path

assert os.getpid() == 24508, 'Unexpected editor process'
_minimal_fix_game = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
_minimal_fix_audit = {
    'pid': os.getpid(),
    'game_world': _minimal_fix_game.get_path_name() if _minimal_fix_game else None,
    'dirty_maps': [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
    'dirty_content': [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
}
Path('D:/UE5.7/test1/Artifacts/MassStutterFix/20260923/before-build-editor.json').write_text(json.dumps(_minimal_fix_audit, indent=2), encoding='utf-8')
assert _minimal_fix_audit['game_world'] is None, 'PIE is active; no close performed'
assert not _minimal_fix_audit['dirty_maps'] and not _minimal_fix_audit['dirty_content'], 'Unsaved packages; no close performed'
_minimal_fix_exit_after = time.monotonic() + 1.0

def _minimal_fix_quit(_delta):
    if time.monotonic() < _minimal_fix_exit_after:
        return
    unreal.unregister_slate_post_tick_callback(_minimal_fix_exit_handle)
    unreal.SystemLibrary.quit_editor()

_minimal_fix_exit_handle = unreal.register_slate_post_tick_callback(_minimal_fix_quit)
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'audit': _minimal_fix_audit, 'clean_exit_scheduled': True}))
