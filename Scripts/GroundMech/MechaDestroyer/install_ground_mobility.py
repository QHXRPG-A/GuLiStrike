"""One-time graph installation, following calibrated standard DataTable import."""
import json
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=ROOT/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export'
BASE='/Game/GuLiStrike/GroundMech'
ANIMS=BASE+'/Animations/A4B2'
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
abp=unreal.load_asset(ANIMS+'/ABP_GroundMech_A4B2')
report=json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.build_animation_graph(abp,ANIMS))
assert report['success'],report
anim=unreal.get_default_object(abp.generated_class())
anim.set_editor_property('root_motion_mode',unreal.RootMotionMode.IGNORE_ROOT_MOTION)
anim.set_editor_property('jump_start_duration',unreal.load_asset(ANIMS+'/Anim_Mecha_Jump_Start').sequence_length)
anim.set_editor_property('shoot_duration',unreal.load_asset(ANIMS+'/Anim_Mecha_Shoot').sequence_length)
assert unreal.EditorAssetLibrary.save_loaded_asset(abp,False)
bp=unreal.load_asset(BASE+'/BP_GroundMech_Light'); bp.modify()
cdo=unreal.get_default_object(bp.generated_class())
cdo.modify()
cdo.get_editor_property('character_movement').set_editor_property('orient_rotation_to_movement',False)
cdo.get_editor_property('mesh').set_anim_instance_class(abp.generated_class())
# Install the native look action in a rebuilt player Blueprint. Camera values are set by the table import.
look_path=BASE+'/Input/IA_GroundMech_Look'
lib=unreal.EditorAssetLibrary
look=unreal.load_asset(look_path) if lib.does_asset_exist(look_path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    'IA_GroundMech_Look',BASE+'/Input',unreal.InputAction,unreal.InputAction_Factory())
assert look
look.modify()
look.set_editor_property('value_type',unreal.InputActionValueType.AXIS2D)
context=cdo.get_editor_property('mapping_context')
key=unreal.Key()
key.set_editor_property('key_name','Mouse2D')
mappings=context.get_editor_property('default_key_mappings').get_editor_property('mappings')
if not any(m.get_editor_property('action')==look and m.get_editor_property('key').get_editor_property('key_name')==key.get_editor_property('key_name') for m in mappings):
    context.modify()
    context.map_key(look,key)
cdo.set_editor_property('look_action',look)
assert unreal.BlueprintService.compile_blueprint(bp.get_path_name())
for asset in (look,context,bp):
    assert lib.save_loaded_asset(asset,False)
report['scope']='Native compile and asset authoring only; no PIE/pose tests/images'
(OUT/'ground_mobility_graph_install.json').write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report))
