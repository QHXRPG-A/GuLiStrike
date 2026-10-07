import json
from pathlib import Path
import unreal
ART = Path('D:/UE5.7/test1/ArtSource/CommanderLOD_20261005')
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert editor.get_game_world() is None, 'Do not interrupt gameplay.'
dirty = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
assert not dirty, ('Unsaved content must be handled before the permitted restart.', dirty)
permit = dict(authorized=True, user_message='现在编译并重开 UE', date='2026-10-05',
    target='GuLiStrikeEditor Win64 Development', engine='D:/UnrealEngine-5.7',
    restart_authorized=True, scope='Load the completed native unit/VAT fields for the approved six-group LOD migration.',
    game_world=None, dirty_packages=dirty, state='ready_to_build')
(ART / 'Reports/native_build.json').write_text(json.dumps(permit, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True, native_build_authorized=True, editor_world=editor.get_editor_world().get_path_name(), dirty_packages=dirty)))
