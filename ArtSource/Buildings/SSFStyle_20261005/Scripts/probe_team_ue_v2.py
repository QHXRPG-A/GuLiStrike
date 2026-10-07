"""Read-only probe for the new team directories and previously verified dependencies."""
import unreal,json,traceback
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');D=R/'UE_Delivery_Team_v2'
report={'success':False,'saved_assets':[],'maps_saved':[]}
try:
    assert '-SSFTeamProbeWorker' in unreal.SystemLibrary.get_command_line()
    reg=unreal.AssetRegistryHelpers.get_asset_registry();reg.wait_for_completion()
    base='/Game/GuLiStrike/Buildings/SSFStylized'
    report['engine']=unreal.SystemLibrary.get_engine_version()
    report['team_assets']={team:[str(a.package_name) for a in reg.get_assets_by_path(base+'/'+team,recursive=True)] for team in ['Blue','Red']}
    report['previous_release']=[]
    previous=json.loads((R/'UE_Delivery_v1/ue_import.json').read_text(encoding='utf8'))
    assert previous['success']
    for a in previous['assets']:
        if a['key'] not in ['AirBase','CloningCenter','CommandCenter','MilitaryFactory','Reactor','StrategyCenter']:continue
        mesh=unreal.load_asset(a['path']);assert mesh
        assert unreal.EditorAssetLibrary.get_metadata_tag(mesh,'GuLi.SourceVersion')=='SSF_Production_B_v1'
        assert unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem).get_lod_count(mesh)==3
        report['previous_release'].append({'key':a['key'],'path':a['path'],'skeleton':mesh.skeleton.get_path_name(),
            'physics':mesh.physics_asset.get_path_name() if mesh.physics_asset else None,'bones':len(unreal.SkeletonService.list_bones(a['path']))})
    report['shared_materials']={name:bool(unreal.load_asset(base+'/Shared/Materials/'+name)) for name in ['M_SSF_ThreeToneLine','M_SSF_Outline','M_SSF_SourceDisplay']}
    assert all(report['shared_materials'].values())
    report['success']=True
except Exception:report['error']=traceback.format_exc();unreal.log_error(report['error'])
finally:
    (D/'ue_probe.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    unreal.SystemLibrary.quit_editor()
