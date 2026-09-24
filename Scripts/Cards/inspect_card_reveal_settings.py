import json, unreal
from pathlib import Path
root='/Game/GuLiStrike/Cards/RevealDemo'
unreal.EditorLoadingAndSavingUtils.load_map(root+'/Maps/LVL_CardRevealDemo')
director=next(a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if a.get_class().get_name()=='BP_CardRevealDirector_C')
camera=director.get_component_by_class(unreal.CameraComponent)
pp=camera.get_editor_property('post_process_settings')
data={'camera':str(camera.get_world_transform()), 'pp':{k:str(pp.get_editor_property(k)) for k in ['override_auto_exposure_method','auto_exposure_method','override_auto_exposure_bias','auto_exposure_bias','override_auto_exposure_apply_physical_camera_exposure','auto_exposure_apply_physical_camera_exposure','override_motion_blur_amount','motion_blur_amount']}, 'blend_weight':camera.post_process_blend_weight}
Path('D:/UE5.7/test1/ArtSource/UI/CardRevealDemo/settings-inspection.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
print(json.dumps(data))
