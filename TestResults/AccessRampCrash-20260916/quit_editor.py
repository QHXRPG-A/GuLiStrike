import os,unreal,json
assert os.getpid()==43932
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
dirty=unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()+unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
assert not dirty,[p.get_path_name() for p in dirty]
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'pid':os.getpid(),'dirty_packages':[],'action':'normal_exit_for_engine_patch_build'}))
unreal.SystemLibrary.quit_editor()
