import unreal,json
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export')
def tr(t):return {'t':list(t.translation.to_tuple()),'q':[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w],'s':list(t.scale3d.to_tuple())}
bones=list(unreal.SkeletonService.list_bones('/Game/MechaDestroyer/SKM/SKM_Mecha'))
names=[str(b.bone_name) for b in bones]
data={'bones':[{'name':str(b.bone_name),'parent':str(b.parent_bone_name),'local':tr(b.local_transform),'global':tr(b.global_transform)} for b in bones],'clips':{}}
for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path('/Game/MechaDestroyer/Animation',False):
    if str(a.asset_class_path.asset_name)!='AnimSequence':continue
    o=a.get_asset();n=unreal.AnimationLibrary.get_num_keys(o)
    frames=[[tr(t) for t in unreal.AnimationLibrary.get_bone_poses_for_frame(o,names,i,False)] for i in range(n)]
    data['clips'][o.get_name()]={'path':o.get_path_name(),'seconds':o.sequence_length,'frames':frames,'num_keys':n,'tracks':[str(n) for n in unreal.AnimationLibrary.get_animation_track_names(o)]}
(OUT/'source_animation_data.json').write_text(json.dumps(data),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'bones':names,'clips':{n:{'keys':v['num_keys'],'seconds':v['seconds']} for n,v in data['clips'].items()}}))
