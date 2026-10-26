"""Read saved Ground assets after editor cold load; no graph rebuild, pose tick or PIE."""
import json
import math
import re
from collections import Counter
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=ROOT/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export'
BASE='/Game/GuLiStrike/GroundMech'
ANIMS=BASE+'/MechaDestroyer_A4B2/Animations'
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
bp=unreal.load_asset(BASE+'/BP_GroundMech_Light')
abp=unreal.load_asset(ANIMS+'/ABP_GroundMech_A4B2')
mesh=unreal.load_asset(BASE+'/MechaDestroyer_A4B2/Meshes/SK_GroundMech_A4B2')
nodes=unreal.BlueprintService.get_nodes_in_graph(abp.get_path_name(),'AnimGraph')
counts=dict(Counter(n.node_type for n in nodes))
assert len(nodes)==73,counts
required={
 'AnimGraphNode_Root':['Result'],
 'AnimGraphNode_LocalToComponentSpace':['LocalPose','ComponentPose'],
 'AnimGraphNode_ComponentToLocalSpace':['ComponentPose','Pose'],
 'AnimGraphNode_GuLiGroundMechGrounding':['ComponentPose','Pose','Ground'],
 'AnimGraphNode_GuLiGroundMechAim':['ComponentPose','Pose','AimPointComponent','bAimValid','bForwardAim','ReturnAlpha','RightReturnStart','LeftReturnStart','RightMuzzleLocal','LeftMuzzleLocal','RightRecoil','LeftRecoil','RecoilDistance','RightArmRecoil','LeftArmRecoil','ArmRecoilDegrees','BlinkPose','GunYawLimit','GunPitchUpLimit','GunPitchDownLimit'],
 'AnimGraphNode_ApplyAdditive':['Base','Additive','Alpha','Pose'],
 'AnimGraphNode_BlendSpacePlayer':['X','Y','PlayRate','BlendSpace'],
 'AnimGraphNode_SequenceEvaluator':['Sequence','ExplicitTime'],
 'AnimGraphNode_SequencePlayer':['Sequence','PlayRate'],
 'AnimGraphNode_Slot':['Source','Pose'],
 'AnimGraphNode_LayeredBoneBlend':['BasePose','BlendPoses_0','BlendWeights_0','Pose'],
}
connections=[]
for n in nodes:
 if n.node_type not in required: continue
 pins={p.pin_name:p.is_connected for p in unreal.BlueprintService.get_node_pins(abp.get_path_name(),'AnimGraph',n.node_id)}
 assert all(pins.get(k,False) for k in required[n.node_type]),(n.node_type,pins)
 connections.append({'type':n.node_type,'pins':pins})
for kind,count in {'AnimGraphNode_GuLiGroundMechGrounding':1,'AnimGraphNode_GuLiGroundMechAim':1,'AnimGraphNode_BlendSpacePlayer':2,'AnimGraphNode_SequenceEvaluator':3,'AnimGraphNode_SequencePlayer':3,'AnimGraphNode_ApplyAdditive':2,'AnimGraphNode_LayeredBoneBlend':2,'AnimGraphNode_Slot':1}.items():
 assert counts.get(kind)==count,(kind,counts)
assets=json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.configure_mobility_assets(False))
assert assets['success'],assets
pawn=unreal.get_default_object(bp.generated_class())
component=pawn.get_editor_property('mesh')
assert component.anim_class==abp.generated_class()
assert unreal.get_default_object(abp.generated_class()).get_editor_property('root_motion_mode')==unreal.RootMotionMode.IGNORE_ROOT_MOTION
boom=pawn.get_editor_property('camera_boom'); camera=pawn.get_editor_property('camera')
camera_data={'arm_cm':boom.get_editor_property('target_arm_length'),'socket_offset':list(boom.get_editor_property('socket_offset').to_tuple()),'boom_rotation_roll_pitch_yaw':list(boom.get_relative_transform().rotation.rotator().to_tuple()),'camera_location':list(camera.get_relative_transform().translation.to_tuple()),'camera_rotation':list(camera.get_relative_transform().rotation.rotator().to_tuple()),'fov':camera.get_editor_property('field_of_view'),'collision_test':boom.get_editor_property('do_collision_test')}
assert abs(camera_data['arm_cm']-362.95970144)<.01
assert camera_data['fov']==90 and camera_data['collision_test']
assert max(abs(x) for x in camera_data['camera_location']+camera_data['camera_rotation'])<.001
manifests=[]
for population in (200,600):
 manifest=json.loads((ROOT/f'Config/Hybrid/RuntimeManifest-{population}.json').read_text(encoding='utf-8-sig'))
 assert manifest['strings']['ground/mobility_configuration']==assets['configuration_fingerprint']
 manifests.append({'population':population,'hash':manifest['config_hash']})
tables={}
for sheet in ('Skills','Mobility','Gait','GroundIK','Camera'):
 saved=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeMech_'+sheet)))
 source={r['Name']:r for r in json.loads((ROOT/f'Data/Json/DT_GuLiStrikeMech_{sheet}.json').read_text())}
 assert set(source)=={r['Name'] for r in saved}
 for row in saved:
  for field,value in source[row['Name']].items():
   actual=row[field]
   if isinstance(value,dict):
    if isinstance(actual,str): actual={a:float(n) for a,n in re.findall(r'([XYZ])=([-+0-9.eE]+)',actual)}
    assert set(actual)==set(value),(sheet,row['Name'],field)
    assert all(math.isclose(actual[k],v,rel_tol=1.e-5,abs_tol=1.e-4) for k,v in value.items()),(sheet,row['Name'],field,actual,value)
   elif isinstance(value,(float,int)):
    assert math.isclose(actual,value,rel_tol=1.e-5,abs_tol=1.e-4),(sheet,row['Name'],field,actual,value)
   else: assert actual==value or actual=='None' and not value,(sheet,row['Name'],field)
 tables[sheet]=saved
authored=json.loads((OUT/'heavy_gait_animation_data.json').read_text())
clips=[]
for name,data in authored['clips'].items():
 sequence=unreal.load_asset(ANIMS+'/'+name)
 assert sequence.get_skeleton()==mesh.skeleton
 assert unreal.EditorAssetLibrary.get_metadata_tag(sequence,'GroundHeavyBake')==data.get('signature',authored['signature'])
 model=sequence.data_model_interface
 assert model.get_number_of_frames()==len(data['frames'])-1
 assert {str(n).lower() for n in model.get_bone_track_names()}=={n.lower() for n in authored['bones']}
 markers=[{'name':str(m.marker_name),'time':m.time} for m in unreal.AnimationLibrary.get_animation_sync_markers(sequence)]
 assert {m['name'] for m in markers}=={'GroundLeftPlant','GroundRightPlant'},(name,markers)
 curve_names=list(map(str,unreal.AnimationLibrary.get_animation_curve_names(sequence,unreal.RawCurveTrackTypes.RCT_FLOAT)))
 curves={}
 for curve in curve_names:
  times,values=unreal.AnimationLibrary.get_float_keys(sequence,curve)
  assert len(times)==len(data['frames']) and all(a<b for a,b in zip(times,times[1:]))
  assert all(0<=v<=1 for v in values) and sum(v>=.999 for v in values)>=2,(name,curve)
  curves[curve]={'keys':len(values),'full_support_keys':sum(v>=.999 for v in values),'min':min(values),'max':max(values)}
 if data.get('additive'):
  assert not curves,name
  assert sequence.get_editor_property('additive_anim_type')==unreal.AdditiveAnimationType.AAT_LOCAL_SPACE_BASE
  assert sequence.get_editor_property('ref_pose_type')==unreal.AdditiveBasePoseType.ABPT_ANIM_FRAME
  assert sequence.get_editor_property('ref_pose_seq')==unreal.load_asset(ANIMS+'/Anim_Mecha_Idle')
  assert sequence.get_editor_property('ref_frame_index')==0 and not sequence.get_editor_property('enable_root_motion')
 else: assert {n.lower() for n in curve_names}=={'groundplant_l','groundplant_r'}
 clips.append({'asset':sequence.get_path_name(),'frames':model.get_number_of_frames()+1,'curves':curves,'markers':markers,'moving_turn_additive':bool(data.get('additive'))})
report={'success':True,'scope':'Read saved asset structure and configuration only; no graph rebuild, compile, pose tick, PIE or images','graph':{'node_count':len(nodes),'node_types':counts,'connections':connections},'assets':assets,'camera':camera_data,'sockets':{n:str(mesh.find_socket(n).bone_name) for n in ['Muzzle','Muzzle_L','Jet_Outlet_R','Jet_Outlet_L']},'mesh_relative_transform':str(component.get_relative_transform()),'capsule_half_height':pawn.get_editor_property('capsule_component').get_unscaled_capsule_half_height(),'manifests':manifests,'tables':tables,'heavy_gait':{'signature':authored['signature'],'clips':clips}}
path=OUT/globals().get('GULI_MOBILITY_AUDIT_REPORT','ground_mobility_cold_load_20261024.json')
path.write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'nodes':len(nodes),'configuration_fingerprint':assets['configuration_fingerprint'],'camera':camera_data,'report':str(path)}))
