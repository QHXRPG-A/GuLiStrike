import unreal,json
for name in ('w','pc','p','sp','worlds','ground_network_check','rm_world','rm_pc','rm_pawn','rm_sub','w0','pc0','p0','an','fresh','spider','world'):
    globals().pop(name,None)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
unreal.MCPythonHelper.submit_result(json.dumps({'end_play_requested':True}))
