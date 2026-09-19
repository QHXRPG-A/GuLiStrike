import unreal,json
from pathlib import Path
path='/Game/GuLiStrike/GroundMech/Animations/ABP_GroundMech'
assert unreal.BlueprintService.set_property(path,'RootMotionMode','IgnoreRootMotion')
asset=unreal.load_asset(path)
assert unreal.BlueprintService.compile_blueprint(path)
mode=unreal.get_default_object(asset.generated_class()).get_editor_property('root_motion_mode')
assert mode==unreal.RootMotionMode.IGNORE_ROOT_MOTION
assert unreal.EditorAssetLibrary.save_loaded_asset(asset,False)
(Path(unreal.Paths.project_dir())/'TestResults/GroundMech/root-motion-saved.json').write_text(json.dumps({'success':True,'root_motion_mode':str(mode)}),encoding='utf-8')
