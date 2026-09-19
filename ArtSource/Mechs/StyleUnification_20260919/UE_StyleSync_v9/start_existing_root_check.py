import unreal,json,time
from pathlib import Path
style_pie_wait_started=time.monotonic()
def style_begin_root_check(delta):
    available=[w for w in unreal.ObjectIterator(unreal.World) if 'UEDPIE_1_' in w.get_path_name()]
    if available:
        controller=unreal.GameplayStatics.get_player_controller(available[0],0)
        pawn=controller.get_controlled_pawn() if controller else None
        if isinstance(pawn,unreal.GuLiGroundMechCharacter):
            unreal.unregister_slate_post_tick_callback(style_begin_handle)
            exec(Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/UE_StyleSync_v9/run_existing_root_check.py').read_text(),globals())
    elif time.monotonic()-style_pie_wait_started>30:
        unreal.unregister_slate_post_tick_callback(style_begin_handle)
        unreal.log_error('Style root check: PIE client unavailable')
style_begin_handle=unreal.register_slate_post_tick_callback(style_begin_root_check)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
unreal.MCPythonHelper.submit_result(json.dumps({'requested':True}))
