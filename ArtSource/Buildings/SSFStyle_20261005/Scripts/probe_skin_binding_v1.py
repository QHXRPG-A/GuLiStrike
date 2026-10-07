import unreal,json,traceback
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');D=R/'UE_Delivery_v1';out={}
assert '-SSFAnimProbeWorker' in unreal.SystemLibrary.get_command_line()
try:
 out['API']={n:str(getattr(unreal.SkinWeightModifier,n).__doc__)[:1000] for n in dir(unreal.SkinWeightModifier) if any(s in n for s in ('weight','vertex','skeletal_mesh'))}
 mesh=unreal.load_asset('/Game/GuLiStrike/Buildings/SSFStylized/AirBase/Meshes/SK_TB1_AirBase')
 modifier=unreal.SkinWeightModifier();assert modifier.set_skeletal_mesh(mesh)
 out['weights']=[{str(k):v for k,v in modifier.get_vertex_weights(i).items()} for i in range(modifier.get_num_vertices())]
 out['bones']=[{'name':str(b.bone_name),'parent':str(b.parent_bone_name)} for b in unreal.SkeletonService.list_bones(mesh.get_path_name())]
 # Locate the actual component discrepancy before deciding a repair.
 source=unreal.load_asset('/Game/Assets/SSF_Buildings/Buildings/MilitaryFactory/Animations/TB1_MilitaryFactory_Destroy1_Anim')
 new=unreal.load_asset('/Game/GuLiStrike/Buildings/SSFStylized/MilitaryFactory/Animations/TB1_MilitaryFactory_Destroy1_Anim')
 options=unreal.AnimPoseEvaluationOptions();options.evaluation_type=unreal.AnimDataEvalType.COMPRESSED
 a=unreal.AnimPoseExtensions.get_anim_pose_at_time(source,0,options);b=unreal.AnimPoseExtensions.get_anim_pose_at_time(new,0,options)
 out['factory_compressed_rotations']=[{'name':str(n),'source':a.get_bone_pose(n,unreal.AnimPoseSpaces.WORLD).export_text(),'copy':b.get_bone_pose(n,unreal.AnimPoseSpaces.WORLD).export_text()} for n in a.get_bone_names()]
except Exception:out['error']=traceback.format_exc()
(D/'skin_binding_probe.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf8');unreal.SystemLibrary.quit_editor()
