import json
import unreal
names=['Actor','PoseableMeshComponent']
result={}
for name in names:
    cls=getattr(unreal,name,None)
    if cls:result[name]={method:getattr(cls,method).__doc__ for method in dir(cls) if method in ['add_component_by_class','finish_add_component','set_bone_transform_by_name','get_bone_transform_by_name','set_forced_lod','set_skinned_asset_and_update','set_skeletal_mesh']}
unreal.MCPythonHelper.submit_result(json.dumps(result))
