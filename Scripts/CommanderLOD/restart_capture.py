"""Replace this task's active art-render callback after fixing sample visibility."""
import unreal,gc,types,json,sys
from pathlib import Path
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART
active=[]
for obj in gc.get_objects():
 if not isinstance(obj,types.FunctionType) or obj.__name__!='finish':continue
 if not obj.__code__.co_filename.replace('\\','/').endswith('Scripts/CommanderLOD/capture_preview.py'):continue
 state=obj.__globals__.get('state',{})
 if state.get('handle') and state.get('actors') and unreal.SystemLibrary.is_valid(state['actors'][0]):active.append(obj)
assert len(active)<=1,len(active)
for finish in active:finish('Capture restarted after correcting editor-only visibility.')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for actor in actors.get_all_level_actors():
 if actor.get_actor_label().startswith('CommanderLOD_'):actor.set_editor_property('is_editor_only_actor',True)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
previous=unreal.SystemLibrary.get_console_variable_int_value('t.IdleWhenNotForeground')
unreal.SystemLibrary.execute_console_command(world,'t.IdleWhenNotForeground 0')
(ART/'Reports/capture_editor_settings.json').write_text(json.dumps(dict(idle_when_not_foreground=previous)),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,replaced_callbacks=len(active))))
