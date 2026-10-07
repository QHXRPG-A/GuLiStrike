import json
import unreal
mesh = unreal.load_asset('/Game/GuLiStrike/Commander/Units/WM01/LOD_3Tier_v1/Meshes/SM_WarMachine_Rigid')
desc = mesh.get_static_mesh_description(0)
source = unreal.load_asset('/Game/GuLiStrike/Robots/RSGMech/VAT/DA_Pioneer_VAT')
formal = unreal.load_asset('/Game/GuLiStrike/Commander/Units/DefaultSoldier/LOD_3Tier_v1/VAT/DA_Pioneer_VAT')
result = {'box_api':unreal.Box.__doc__, 'boxes':{
    role:{field:str(asset.get_editor_property(field)) for field in ('gameplay_bounds','runtime_render_bounds')}
    for role,asset in [('source',source),('formal',formal)]}}
unreal.MCPythonHelper.submit_result(json.dumps(result))
