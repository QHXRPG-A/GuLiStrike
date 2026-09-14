import unreal,json
from pathlib import Path
out=Path('D:/UE5.7/test1/outputs/hardsurface-models-20260914');out.mkdir(parents=True,exist_ok=True)
base='/Game/GuLiStrike/Buildings/'
names=['RedOreRefinery','ShieldGenerator','HeavyDefenseCannon']
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
registry=unreal.AssetRegistryHelpers.get_asset_registry()
matches=[]
for root in ['/Game/GuLiStrike/Buildings','/Game/GuLiStrike/WarMachines']:
    for a in registry.get_assets_by_path(root,recursive=True):
        if any(n.lower() in str(a.asset_name).lower() for n in names):matches.append({'name':str(a.asset_name),'package':str(a.package_name),'class':str(a.asset_class_path)})
result={'project_dir':unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()),
        'engine':unreal.SystemLibrary.get_engine_version(),'world':world.get_path_name(),
        'targets':{n:unreal.EditorAssetLibrary.does_directory_exist(base+n) for n in names},
        'existing_matches':matches,'blueprint_service':hasattr(unreal,'BlueprintService'),
        'skeleton_service':hasattr(unreal,'SkeletonService')}
(out/'context.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,**result}))
