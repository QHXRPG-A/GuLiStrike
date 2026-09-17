import os,unreal,json
assert os.getpid()==39244
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
x,y=40000,-155000
hit=unreal.SystemLibrary.line_trace_single_for_objects(w,unreal.Vector(x,y,50000),unreal.Vector(x,y,-50000),[unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1],False,[],unreal.DrawDebugTrace.NONE,True)
p=hit.to_tuple()[5]
command=f'gs.GM.Skill.Spawn Blue 1 {x} {y} {p.z}'
unreal.SystemLibrary.execute_console_command(w,command)
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'pid':os.getpid(),'command':command}))
