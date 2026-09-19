import unreal,json
from pathlib import Path
roots=['/Game/GuLiStrike/Mechs/Mecha_01','/Game/GuLiStrike/Mechs/Mecha_02','/Game/GuLiStrike/Weapons/MechModules/FireWeapon_01','/Game/GuLiStrike/Weapons/MechModules/MissileWeapon_01','/Game/GuLiStrike/Weapons/MechProjectiles/Missile_01','/Game/GuLiStrike/Mechs/StyleShowcase']
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
result={'project':unreal.Paths.project_dir(),'engine':unreal.Paths.engine_dir(),'world':editor.get_editor_world().get_path_name(),'pie':unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),'dirty_maps':[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'dirty_content':[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],'new_directories':{p:unreal.EditorAssetLibrary.does_directory_exist(p) for p in roots}}
for name in ('Mecha_01','Mecha_02','FireWeapon_01','MissileWeapon_01'):
    prefix='/Game/Assets/MechaController/Artistic/Meshes/'+('Mechas/'+name if name.startswith('Mecha_') else 'Weapons/'+('Weapon_01' if name=='FireWeapon_01' else 'Weapon_02'))
    data=unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(prefix,True)
    result[name]=[{'path':str(a.package_name),'class':str(a.asset_class_path.asset_name)} for a in data if str(a.asset_class_path.asset_name) in ('AnimSequence','AnimBlueprint','Skeleton','Blueprint')]
Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/UE_AllAssets_v10/live_preflight.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result))
