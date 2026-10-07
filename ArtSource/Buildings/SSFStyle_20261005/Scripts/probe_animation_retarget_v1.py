import unreal,json,traceback
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');D=R/'UE_Delivery_v1';out={}
assert '-SSFAnimProbeWorker' in unreal.SystemLibrary.get_command_line()
try:
 for label,path in (('source','/Game/Assets/SSF_Buildings/Buildings/MilitaryFactory/Animations/TB1_MilitaryFactory_Destroy1_Anim'),('copy','/Game/GuLiStrike/Buildings/SSFStylized/MilitaryFactory/Animations/TB1_MilitaryFactory_Destroy1_Anim')):
  anim=unreal.load_asset(path);entry={};out[label]=entry
  entry['retarget_source']=str(anim.get_editor_property('retarget_source'));entry['retarget_source_asset']=str(anim.get_retarget_source_asset())
  for typ in (unreal.AnimDataEvalType.SOURCE,unreal.AnimDataEvalType.RAW,unreal.AnimDataEvalType.COMPRESSED):
   for retarget in (False,True):
    options=unreal.AnimPoseEvaluationOptions();options.evaluation_type=typ;options.should_retarget=retarget
    pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(anim,0,options)
    entry[str(typ)+'_retarget_'+str(retarget)]={name:pose.get_bone_pose(name,unreal.AnimPoseSpaces.LOCAL).export_text() for name in ('Root','CivilBlock_L','Container_R')}
except Exception:out['error']=traceback.format_exc()
(D/'animation_retarget_probe.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf8');unreal.SystemLibrary.quit_editor()
