import os,unreal,json
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert os.getpid()==39244
assert 'UEDPIE_0' in world.get_path_name()
commands=['guli.stronghold.CaptureSeconds 1000000000','gs.Resources.SetTerritoryOwner 3 4 Blue']
for command in commands: unreal.SystemLibrary.execute_console_command(world,command)
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'pid':os.getpid(),'world':world.get_path_name(),'commands':commands}))
