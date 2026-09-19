import unreal,json,sys,importlib
from pathlib import Path
sys.path.insert(0,'D:/UE5.7/test1/Scripts/GroundMech')
import import_remaining_mech_assets_v10
preserve_sockets=importlib.reload(import_remaining_mech_assets_v10).preserve_sockets
out=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/UE_AllAssets_v10')
rows=json.loads((out/'ue_import.json').read_text(encoding='utf-8'))['parts']
registry=unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(['/Game/GuLiStrike/Mechs/Mecha_01','/Game/GuLiStrike/Mechs/Mecha_02',
    '/Game/GuLiStrike/Weapons/MechModules','/Game/GuLiStrike/Weapons/MechProjectiles'],True)
result={}
for name,row in rows.items():
    mesh=unreal.load_asset(row['path']);bp=unreal.load_asset(row['blueprint'])
    comp=unreal.get_default_object(bp.generated_class()).get_component_by_class(
        unreal.SkeletalMeshComponent if 'bones' in row else unreal.StaticMeshComponent)
    actual=comp.get_skinned_asset() if 'bones' in row else comp.static_mesh
    assert actual==mesh
    sockets=preserve_sockets(unreal.load_asset(row['source']),mesh) if 'bones' in row else None
    if sockets and sockets['copied_mesh_sockets']:assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)
    assert unreal.BlueprintService.compile_blueprint(row['blueprint'])
    result[name]={'mesh':mesh.get_path_name(),'sockets':sockets,'blueprint_compiled':True}
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'assets':result}))
