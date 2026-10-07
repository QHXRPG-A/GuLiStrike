"""Read-only worker probe of the asset target and APIs; no packages saved."""
import unreal,json,traceback
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');D=R/'UE_Delivery_v1'
report={'success':False,'saved_assets':[]}
try:
 assert '-SSFStyleProbeWorker' in unreal.SystemLibrary.get_command_line()
 registry=unreal.AssetRegistryHelpers.get_asset_registry();registry.wait_for_completion()
 target='/Game/GuLiStrike/Buildings/SSFStylized'
 report['target_assets']=[str(a.package_name) for a in registry.get_assets_by_path(target,recursive=True)]
 report['source_available']=[unreal.EditorAssetLibrary.does_asset_exist(a['path']) for a in json.loads((R/'Source/source_manifest.json').read_text(encoding='utf8'))['meshes']]
 report['engine']=unreal.SystemLibrary.get_engine_version()
 report['APIs']={}
 for cls,methods in [('SkeletalMeshEditorSubsystem',['import_lod','get_lod_count','get_lod_build_settings','set_lod_build_settings']),('AnimationLibrary',['get_bone_pose_for_time','get_num_frames']),('SkeletonService',['list_bones']),('SkeletalMesh',['get_lod_info']),('SkeletonModifier',['set_bone_transform']),('PhysicsAsset',['set_preview_mesh']),('Skeleton',['set_preview_mesh'])]:
  api=getattr(unreal,cls,None);report['APIs'][cls]={method:str(getattr(api,method,None).__doc__)[:3500] if getattr(api,method,None) else None for method in methods}
 assert all(report['source_available'])
 report['success']=True
except Exception:report['error']=traceback.format_exc()
finally:
 (D/'ue_probe.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
 unreal.SystemLibrary.quit_editor()
