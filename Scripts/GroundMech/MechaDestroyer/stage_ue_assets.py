"""Preserve vendor assets; author only the project-owned compatibility copy."""
import unreal,json
from pathlib import Path
ART=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023')
BASE='/Game/GuLiStrike/GroundMech/MechaDestroyer_v1'
lib=unreal.EditorAssetLibrary
source='/Game/MechaDestroyer/SKM/SKM_Mecha'
dest=BASE+'/Meshes/SK_MechaDestroyer'
mesh=unreal.load_asset(dest) if lib.does_asset_exist(dest) else lib.duplicate_asset(source,dest)
assert mesh
def socket(name,bone,location,rotation):
    s=mesh.find_socket(name)
    if not s:
        assert unreal.SkeletonService.add_socket(dest,name,str(bone),location,rotation,unreal.Vector(1,1,1),False)
    else:
        assert unreal.SkeletonService.set_socket_bone(dest,name,str(bone))
        assert unreal.SkeletonService.set_socket_transform(dest,name,location,rotation,unreal.Vector(1,1,1))
for name,original in [('Muzzle','Gun_RSocket'),('Muzzle_L','Gun_LSocket')]:
    s=mesh.find_socket(original)
    assert s
    socket(name,s.get_editor_property('bone_name'),s.get_editor_property('relative_location'),s.get_editor_property('relative_rotation'))
bones={str(b.bone_name):b for b in unreal.SkeletonService.list_bones(source)}
cockpit=bones['cockpit'].global_transform
for name,p in [('Jet_Outlet_L',unreal.Vector(86,-105,345)),('Jet_Outlet_R',unreal.Vector(-63,-105,345))]:
    socket(name,'cockpit',cockpit.inverse_transform_location(p),cockpit.rotation.inversed().rotator())
assert lib.save_loaded_asset(mesh,False)
compiled=[]
for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path('/Game/MechaDestroyer/Blueprints',True):
    p=str(a.package_name)
    bp=unreal.load_asset(p)
    ok=bool(unreal.BlueprintService.compile_blueprint(p))
    compiled.append({'path':p,'compile_success':ok})
report={'success':all(c['compile_success'] for c in compiled),'project_mesh':mesh.get_path_name(),'skeleton':mesh.skeleton.get_path_name(),'vendor_blueprints':compiled,'sockets':[]}
for name in ['Muzzle','Muzzle_L','Jet_Outlet_L','Jet_Outlet_R']:
    s=mesh.find_socket(name)
    report['sockets'].append({k:str(s.get_editor_property(k)) for k in ['socket_name','bone_name','relative_location','relative_rotation']})
(ART/'staged_asset_contract.json').write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report))
