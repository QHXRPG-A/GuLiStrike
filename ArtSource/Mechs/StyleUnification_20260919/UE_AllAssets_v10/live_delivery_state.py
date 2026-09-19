import unreal,json
registry=unreal.AssetRegistryHelpers.get_asset_registry()
roots=['/Game/GuLiStrike/Mechs/Mecha_01','/Game/GuLiStrike/Mechs/Mecha_02',
    '/Game/GuLiStrike/Weapons/MechModules','/Game/GuLiStrike/Weapons/MechProjectiles',
    '/Game/GuLiStrike/Mechs/StyleShowcase']
registry.scan_paths_synchronous(roots,True)
result={'world':unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name(),
    'pie':unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),
    'dirty_maps':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
    'dirty_content':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
    'new_assets':{root:len(registry.get_assets_by_path(root,True)) for root in roots},
    'showcase_exists':unreal.EditorAssetLibrary.does_asset_exist('/Game/GuLiStrike/Mechs/StyleShowcase/LVL_MechAsset_Showcase')}
unreal.MCPythonHelper.submit_result(json.dumps(result))
