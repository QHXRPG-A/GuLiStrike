import unreal,json,math,traceback
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');D=R/'UE_Delivery_v1';out={'success':False}
assert '-SSFAnimProbeWorker' in unreal.SystemLibrary.get_command_line()
try:
 im=json.loads((D/'ue_import.json').read_text(encoding='utf8'))
 for asset in im['assets']:
  if asset['type']!='SkeletalMesh':continue
  entry={};out[asset['key']]=entry
  for label,path in (('source_mesh',asset['source']),('formal_mesh',asset['path']),('source_skeleton',asset['skeleton_source']),('formal_skeleton',asset['skeleton'])):
   entry[label]=[{'bone':str(b.bone_name),'parent':str(b.parent_bone_name),'pose':b.local_transform.export_text(),'mode':b.retargeting_mode} for b in unreal.SkeletonService.list_bones(path)]
 for label,path in (('source','/Game/Assets/SSF_Buildings/Buildings/MilitaryFactory/Animations/TB1_MilitaryFactory_Destroy1_Anim'),('copy','/Game/GuLiStrike/Buildings/SSFStylized/MilitaryFactory/Animations/TB1_MilitaryFactory_Destroy1_Anim')):
  anim=unreal.load_asset(path);entry={};out[label+'_animation']=entry
  for prop in ('RetargetSourceAssetReferencePose','RetargetSource','RetargetSourceAsset','Skeleton'):
   try:entry[prop]=str(anim.get_editor_property(prop))
   except Exception as e:entry[prop]=str(e)
  for retarget in (False,True):
   options=unreal.AnimPoseEvaluationOptions();options.evaluation_type=unreal.AnimDataEvalType.COMPRESSED;options.should_retarget=retarget
   pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(anim,0,options)
   entry['compressed_retarget_'+str(retarget)]={str(n):pose.get_bone_pose(n,unreal.AnimPoseSpaces.LOCAL).export_text() for n in ('Root','CivilBlock_L','Container_R')}
 out['success']=True
except Exception:out['error']=traceback.format_exc()
(D/'skeleton_pose_probe.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf8');unreal.SystemLibrary.quit_editor()
