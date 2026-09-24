"""Recover the editor into the dedicated saved map without touching other packages."""
import json, traceback, unreal
from pathlib import Path
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
root='/Game/GuLiStrike/Cards/RevealDemo'
report={}
try:
    assets=['Blueprints/BP_ParallaxRevealCard','UI/WBP_CardRevealHUD','Blueprints/BP_CardRevealDirector','Blueprints/BP_CardRevealPlayerController','Blueprints/BP_CardRevealGameMode']
    report['compile']={}
    for asset in assets:
        result=unreal.BlueprintService.compile_blueprint(root+'/'+asset)
        report['compile'][asset]=str(result)
        assert result.success, str(result)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(root+'/Maps/LVL_CardRevealDemo')
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    director=next(a for a in actors.get_all_level_actors() if a.get_class().get_name()=='BP_CardRevealDirector_C')
    camera=director.get_component_by_class(unreal.CameraComponent)
    pp=camera.get_editor_property('post_process_settings')
    report['camera']={'location':list(camera.get_world_location().to_tuple()),'rotation':str(camera.get_world_rotation()),'fov':camera.field_of_view,'manual_exposure':str(pp.auto_exposure_method),'exposure_override':pp.override_auto_exposure_method,'axis_constraint':str(camera.aspect_ratio_axis_constraint)}
    assert pp.override_auto_exposure_method and pp.auto_exposure_method==unreal.AutoExposureMethod.AEM_MANUAL, report['camera']
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(unreal.Vector(0,140,0),unreal.Rotator(yaw=-90))
    unreal.ViewportService.set_fov(50)
    unreal.ViewportService.set_exposure_game_settings()
    report['saved']=unreal.EditorLoadingAndSavingUtils.save_map(director.get_world(),root+'/Maps/LVL_CardRevealDemo')
    report['success']=True
except Exception:
    report['success']=False
    report['error']=traceback.format_exc()
Path('D:/UE5.7/test1/ArtSource/UI/CardRevealDemo/editor-recovery.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report))
if report.get('success') and '-CardRevealFocusCheck' in unreal.SystemLibrary.get_command_line():
    source=Path('D:/UE5.7/test1/Scripts/Cards/validate_card_reveal_focus.py')
    exec(compile(source.read_text(encoding='utf-8'),str(source),'exec'),globals())
