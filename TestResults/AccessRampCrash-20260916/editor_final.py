import os,unreal,json
assert os.getpid()==39244
globals().pop('comp',None)
editor_world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
dirty=unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()+unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'pid':os.getpid(),'world':editor_world.get_path_name(),'playing':unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),'dirty_packages':[p.get_path_name() for p in dirty]}))
