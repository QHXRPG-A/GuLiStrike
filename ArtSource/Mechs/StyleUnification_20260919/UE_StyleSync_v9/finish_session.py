import unreal,json
for name in ('world','w','pc','p','sp','spider','light','camera','cc','shots','previous_selection','floor','hit','values','existing','ground_network_check','rm_world','rm_pc','rm_pawn','rm_sub','w0','pc0','p0','an','fresh','cleanup_style_capture','capture_style_tick','style_capture_handle','style_begin_root_check','style_begin_handle'):
    globals().pop(name,None)
unreal.MCPythonHelper.submit_result(json.dumps({'finished':True,'map':'/Game/Maps/LVL_GroundMech_Demo','play_tests_running':False}))
