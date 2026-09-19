import unreal,json
from pathlib import Path
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
ab=unreal.load_asset('/Game/GuLiStrike/GroundMech/Animations/ABP_GroundMech')
backup='/Game/GuLiStrike/GroundMech/Style_v9/Rollback/ABP_GroundMech_BeforeStyleValidation'
if not unreal.EditorAssetLibrary.does_asset_exist(backup):
    saved=unreal.EditorAssetLibrary.duplicate_asset(ab.get_path_name(),backup);assert saved
    assert unreal.EditorAssetLibrary.save_loaded_asset(saved,False)
ab.modify(); cd=unreal.get_default_object(ab.generated_class());cd.modify()
cd.set_editor_property('root_motion_mode',unreal.RootMotionMode.IGNORE_ROOT_MOTION)
unreal.BlueprintEditorLibrary.compile_blueprint(ab)
cd=unreal.get_default_object(ab.generated_class())
bp=unreal.load_asset('/Game/GuLiStrike/GroundMech/BP_GroundMech_Light')
fresh=unreal.new_object(ab.generated_class(),outer=unreal.get_default_object(bp.generated_class()).mesh)
result={'success':fresh.get_editor_property('root_motion_mode')==unreal.RootMotionMode.IGNORE_ROOT_MOTION,'default_mode':str(cd.get_editor_property('root_motion_mode')),'fresh_mode':str(fresh.get_editor_property('root_motion_mode')),'backup':backup}
assert result['success'],result
assert unreal.EditorAssetLibrary.save_loaded_asset(ab,False)
(Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/UE_StyleSync_v9')/'root_config_refresh.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
fresh=None
unreal.MCPythonHelper.submit_result(json.dumps(result))
