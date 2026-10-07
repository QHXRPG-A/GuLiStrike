import unreal,json
from pathlib import Path
BASE='/Game/GuLiStrike/Mechs/ControlRigMech';LIB=unreal.EditorAssetLibrary
mat=unreal.load_asset(BASE+'/Materials/M_ControlRigMech_Toon3_SparseLines')
assert LIB.get_metadata_tag(mat,'GuLi.Owner')=='GuLiStrike.ControlRigMech.B-v4.UE-v1'
expressions=[x for x in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if x.get_outer()==mat]
assert len(expressions)==1
code=expressions[0].get_editor_property('code').replace('float line=','float internalInk=').replace('saturate(line*Strength)','saturate(internalInk*Strength)')
expressions[0].set_editor_property('code',code);unreal.MaterialEditingLibrary.recompile_material(mat)
assert LIB.save_loaded_asset(mat,False)
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'material':mat.get_path_name(),'shader_fixed_reserved_HLSL_keyword':True}))
