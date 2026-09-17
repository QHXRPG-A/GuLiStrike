import os,unreal,json
assert os.getpid()==39244
unreal.SystemLibrary.execute_console_command(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(),'guli.stronghold.CaptureSeconds 20')
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
for task_name in ['guli_detour_observation','guli_observe_passive_detour','w','world','factory','lc','child','c','a','actor','manager','mesh','builder','following','spawned','items','existing','hit','p','t','point','original','start','goal','cls']:
 globals().pop(task_name,None)
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'pid':os.getpid(),'action':'end_play_and_restore_capture_seconds_20'}))
