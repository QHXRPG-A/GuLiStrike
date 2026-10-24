"""One-time graph installation, following calibrated standard DataTable import."""
import json
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=ROOT/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export'
BASE='/Game/GuLiStrike/GroundMech'
ANIMS=BASE+'/MechaDestroyer_A4B2/Animations'
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
abp=unreal.load_asset(ANIMS+'/ABP_GroundMech_A4B2')
report=json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.build_animation_graph(abp,ANIMS))
assert report['success'],report
unreal.get_default_object(abp.generated_class()).set_editor_property('root_motion_mode',unreal.RootMotionMode.IGNORE_ROOT_MOTION)
assert unreal.EditorAssetLibrary.save_loaded_asset(abp,False)
bp=unreal.load_asset(BASE+'/BP_GroundMech_Light'); bp.modify()
cdo=unreal.get_default_object(bp.generated_class())
cdo.get_editor_property('mesh').set_anim_instance_class(abp.generated_class())
assert unreal.BlueprintService.compile_blueprint(bp.get_path_name())
assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
report['scope']='Native compile and asset authoring only; no PIE/pose tests/images'
(OUT/'ground_mobility_graph_install.json').write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report))
