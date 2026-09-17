import os,unreal,json
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert os.getpid()==43932 and 'UEDPIE_0' in world.get_path_name()
commands=[f'gs.Resources.SetTerritoryOwner {r} {c} Blue' for r in range(1,6) for c in range(1,6)]
for command in commands:unreal.SystemLibrary.execute_console_command(world,command)
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'pid':os.getpid(),'commands':commands}))
