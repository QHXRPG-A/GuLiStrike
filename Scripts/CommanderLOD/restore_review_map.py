"""Clear only our transient capture references and reopen the saved prototype map."""
import unreal,gc,json
state=getattr(unreal,'_commander_lod_capture_state',{})
assert not state or state.get('finished'),'Wait for our art capture to finish.'
unreal._commander_lod_preview_actors=[];unreal._commander_lod_capture_state={}
gc.collect()
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem);assert not editor.get_game_world()
assert editor.get_editor_world().get_path_name().split('.')[0] in ['/Engine/Maps/Entry','/Game/Maps/LVL_CommanderMassPrototype']
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/Maps/LVL_CommanderMassPrototype')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,map='/Game/Maps/LVL_CommanderMassPrototype')))
