"""Apply the approved Ground animation/camera repair after loading the matching native Editor build.

Numerical asset inspection only. Does not start PIE or capture/read images.
"""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export'
BASE = '/Game/GuLiStrike/GroundMech'
ANIMS = BASE+'/MechaDestroyer_A4B2/Animations'
LIB = unreal.EditorAssetLibrary
HELPER = unreal.GuLiGroundMechAnimationEditorLibrary
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
mesh = unreal.load_asset(BASE+'/MechaDestroyer_A4B2/Meshes/SK_GroundMech_A4B2')
report = {'success': False, 'before': [], 'after': [], 'visual_review': 'User; no PIE or image checks'}
directions = [(0,0),(0,1),(0,-1),(-1,0),(1,0),(-.7,.7),(.7,.7),(-.7,-.7),(.7,-.7)]
for suffix in ['Walk','Run']:
    bs = unreal.load_asset(ANIMS+'/BlendSpace_Loco_'+suffix)
    report['before'].append(json.loads(HELPER.probe_blend_space(mesh,bs,unreal.Vector(0,1,0))))
    source = unreal.load_asset('/Game/MechaDestroyer/Animation/BlendSpace_Loco_'+suffix)
    assert HELPER.rebuild_blend_space(bs,source)
    assert LIB.save_loaded_asset(bs,False)
    for x,y in directions:
        row=json.loads(HELPER.probe_blend_space(mesh,bs,unreal.Vector(x,y,0)))
        report['after'].append(row)
        (OUT/'animation_camera_fix_20261024.json').write_text(json.dumps(report,indent=2),encoding='utf8')
        assert row['success'] and row['max_leg_rotation_degrees'] > .1, row

abp=unreal.load_asset(ANIMS+'/ABP_GroundMech_A4B2')
report['graph']=json.loads(HELPER.build_animation_graph(abp,ANIMS))
assert report['graph']['success'],report['graph']
cdo=unreal.get_default_object(abp.generated_class())
cdo.set_editor_property('root_motion_mode',unreal.RootMotionMode.IGNORE_ROOT_MOTION)
cdo.set_editor_property('recoil_distance',5.0)
cdo.set_editor_property('jump_start_duration',unreal.load_asset(ANIMS+'/Anim_Mecha_Jump_Start').sequence_length)
cdo.set_editor_property('shoot_duration',unreal.load_asset(ANIMS+'/Anim_Mecha_Shoot').sequence_length)
assert LIB.save_loaded_asset(abp,False)

bp=unreal.load_asset(BASE+'/BP_GroundMech_Light')
bp.modify(); pawn=unreal.get_default_object(bp.generated_class()); pawn.modify()
boom=pawn.get_editor_property('camera_boom'); camera=pawn.get_editor_property('camera')
for obj in [boom,camera]: obj.modify()
boom.set_editor_property('absolute_rotation',True)
boom.set_relative_location(unreal.Vector(0,0,0),False,False)
boom.set_relative_rotation(unreal.Rotator(pitch=-14.85,yaw=-3.90,roll=0),False,False)
boom.set_editor_property('target_offset',unreal.Vector(0,0,0))
boom.set_editor_property('socket_offset',unreal.Vector(0,338.24235197,135.74471350))
boom.set_editor_property('target_arm_length',1084.42362610)
boom.set_editor_property('do_collision_test',True)
camera.set_relative_location(unreal.Vector(0,0,0),False,False)
camera.set_relative_rotation(unreal.Rotator(0,0,0),False,False)
camera.set_editor_property('field_of_view',90.0)
pawn.set_editor_property('initial_camera_distance',1084.42362610)
pawn.get_editor_property('character_movement').set_editor_property('orient_rotation_to_movement',False)
body=pawn.get_editor_property('mesh'); body.set_anim_instance_class(abp.generated_class())

look_path=BASE+'/Input/IA_GroundMech_Look'
look=unreal.load_asset(look_path) if LIB.does_asset_exist(look_path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset('IA_GroundMech_Look',BASE+'/Input',unreal.InputAction,unreal.InputAction_Factory())
assert look
look.set_editor_property('value_type',unreal.InputActionValueType.AXIS2D)
context=pawn.get_editor_property('mapping_context'); context.modify()
context.unmap_all_keys_from_action(look)
key=unreal.Key(); key.set_editor_property('key_name','Mouse2D')
context.map_key(look,key)
pawn.set_editor_property('look_action',look)
assert unreal.BlueprintService.compile_blueprint(bp.get_path_name())
for asset in [look,context,bp]: assert LIB.save_loaded_asset(asset,False)
report['camera']={'arm':1084.42362610,'socket_offset':[0,338.24235197,135.74471350],'rotation':[-14.85,-3.90,0],'camera_local_zero':True,'fov':90,'collision_test':True}
report['player']=bp.get_path_name(); report['anim_instance']=abp.generated_class().get_path_name()
report['success']=True
(OUT/'animation_camera_fix_20261024.json').write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'graph':report['graph'],'before':[{'asset':r['blendspace'],'motion_degrees':r['max_leg_rotation_degrees']} for r in report['before']],'after_min_motion_degrees':min(r['max_leg_rotation_degrees'] for r in report['after']),'report':str(OUT/'animation_camera_fix_20261024.json')}))
