import os,unreal,json
assert os.getpid()==43932
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'pid':os.getpid(),'action':'end_play'}))
