"""Read-only import preflight; quit only this explicitly marked worker."""
import unreal, json, traceback
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004/UE_Delivery_v1')
BASE='/Game/GuLiStrike/Mechs/ControlRigMech'
try:
    assert '-ControlRigMechPreflightWorker' in unreal.SystemLibrary.get_command_line()
    ar=unreal.AssetRegistryHelpers.get_asset_registry()
    src='/Game/Assets/ControlRig/Characters/Mech'
    mesh=unreal.load_asset(src+'/Meshes/SKM_Mech')
    sk=mesh.get_editor_property('skeleton')
    cr=unreal.load_asset(src+'/Rigs/CR_Mech')
    classes=['SkeletalMeshEditorSubsystem','SkeletonService','AnimSequenceService','SkeletonModifier','MaterialEditingLibrary','ControlRigBlueprint','RigVMBlueprint','EditorAssetLibrary','SkeletalMeshLODInfo','SkeletalMeshBuildSettings']
    docs={}
    for name in classes:
        c=getattr(unreal,name,None)
        if c:
            methods=[x for x in dir(c) if any(t in x for t in ('lod','bone','skeletal','preview','skeleton','triangle','vert','reference','replace','consolidate','socket','pose','section','graph','model'))]
            docs[name]={k:getattr(c,k).__doc__ for k in methods if callable(getattr(c,k))}
    docs['SkeletalMeshLODInfo']=unreal.SkeletalMeshLODInfo.__doc__
    docs['SkeletalMeshBuildSettings']=unreal.SkeletalMeshBuildSettings.__doc__
    docs['SkeletalMesh']=unreal.SkeletalMesh.__doc__
    docs['AnimSequence']=unreal.AnimSequence.__doc__
    report={'success':True,'engine':unreal.SystemLibrary.get_engine_version(),'target_assets':[str(a.package_name) for a in ar.get_assets_by_path(BASE,recursive=True)],'source_mesh':mesh.get_path_name(),'source_skeleton':sk.get_path_name(),'source_cr':cr.get_path_name(),'cr_methods':{k:getattr(cr,k).__doc__ for k in dir(cr) if any(t in k for t in ('preview','skeleton','compile','controller','model')) and callable(getattr(cr,k))},'source_root':unreal.SkeletonService.list_bones(src+'/Meshes/SKM_Mech')[0].local_transform.export_text(),'docs':docs}
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'preflight_ue.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
except Exception:
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'preflight_ue.json').write_text(json.dumps({'success':False,'error':traceback.format_exc()}),encoding='utf-8')
finally:
    if '-ControlRigMechPreflightWorker' in unreal.SystemLibrary.get_command_line():unreal.SystemLibrary.quit_editor()
