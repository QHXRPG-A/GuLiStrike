"""Standard data-import hook: sync gait samples/camera defaults and network manifests.

Does not rebuild the AnimGraph, start gameplay, evaluate poses, or inspect images.
"""
import json
import re
from pathlib import Path
import unreal

def sync_mobility_assets(export_manifests=True):
    root=Path(unreal.Paths.project_dir()).resolve()
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    lib=unreal.EditorAssetLibrary
    scope={'__name__':'heavy_gait_import'}
    heavy_source=root/'Scripts/GroundMech/MechaDestroyer/sync_heavy_gait_assets.py'
    exec(compile(heavy_source.read_text(encoding='utf8'),str(heavy_source),'exec'),scope)
    heavy=scope['sync_heavy_gait_assets'](root)
    ik_table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeMech_GroundIK')
    ik=next(row for row in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(ik_table)) if row['Name']=='Default')
    contact_signature=json.dumps({'version':3,'heavy':heavy['signature'],'tolerance':ik['ContactHeightTolerance'],'blend_height':ik['ContactBlendHeight']},sort_keys=True)
    contacts_rebaked=False
    prior_contacts=lib.get_metadata_tag(ik_table,'GroundContactBake')
    if prior_contacts!=contact_signature:
        mesh=unreal.load_asset('/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Meshes/SK_GroundMech_A4B2')
        prior=json.loads(prior_contacts) if prior_contacts else {}
        same_contact_rules=prior.get('version')==3 and prior.get('tolerance')==ik['ContactHeightTolerance'] and prior.get('blend_height')==ik['ContactBlendHeight']
        selected=heavy['changed_gaits'] if same_contact_rules else []
        contacts=({'success':True,'clips':[],'gait':{},'sole':{}} if same_contact_rules and not selected else
            json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.bake_ground_contacts(mesh,
                '/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Animations',ik['ContactHeightTolerance'],ik['ContactBlendHeight'],','.join(selected))))
        if not contacts['success']: raise RuntimeError(contacts)
        for clip in contacts['clips']:
            assert lib.save_loaded_asset(unreal.load_asset(clip['asset']),False)
        # Calibration is written back into the sole numeric source before the
        # final DataTable and manifest refresh; never maintain a hidden override.
        out=root/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export'
        authored=json.loads((out/'heavy_gait_authoring.json').read_text())
        for name,values in authored['gait'].items(): contacts['gait'].setdefault(name,{}).update(values)
        calibration=out/'heavy_gait_contacts.json'
        calibration.write_text(json.dumps(contacts,indent=2),encoding='utf8')
        run=scope['run_authoring']
        run(root,root/'Scripts/GroundMech/MechaDestroyer/author_mobility_tables.py','--calibration',calibration)
        run(root,root/'Tools/DataPipeline/export_data_from_excel.py')
        for name in ('Gait','GroundIK'):
            table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeMech_'+name)
            settings=unreal.CSVImportSettings()
            settings.set_editor_property('import_row_struct',table.get_editor_property('row_struct'))
            settings.set_editor_property('import_type',unreal.CSVImportType.ECSV_DATA_TABLE)
            factory=unreal.CSVImportFactory(); factory.set_editor_property('automated_import_settings',settings)
            task=unreal.AssetImportTask(); task.set_editor_property('filename',str(root/f'Data/Json/DT_GuLiStrikeMech_{name}.csv'))
            task.set_editor_property('destination_path','/Game/GuLiStrike/Data')
            task.set_editor_property('destination_name','DT_GuLiStrikeMech_'+name)
            task.set_editor_property('replace_existing',True); task.set_editor_property('automated',True)
            task.set_editor_property('save',True); task.set_editor_property('factory',factory)
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
            assert task.get_editor_property('imported_object_paths'),name
            assert lib.save_loaded_asset(table,False)
        ik_table.modify()
        lib.set_metadata_tag(ik_table,'GroundContactBake',contact_signature)
        assert lib.save_loaded_asset(ik_table,False)
        contacts_rebaked=True
    result=json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.configure_mobility_assets(True))
    if not result['success']: raise RuntimeError(result)
    result['contact_curves_rebaked']=contacts_rebaked
    result['heavy_gait']=heavy
    rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Gait')))
    for row in rows:
        if row['Name'] in ('WalkBlend','RunBlend'):
            assert lib.save_loaded_asset(unreal.load_asset(row['Asset']),False)
    cameras=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Camera')))
    aim=next(row for row in cameras if row['Name']=='Aim')
    bp=unreal.load_asset('/Game/GuLiStrike/GroundMech/BP_GroundMech_Light'); bp.modify()
    cdo=unreal.get_default_object(bp.generated_class()); cdo.modify()
    boom=cdo.get_editor_property('camera_boom'); camera=cdo.get_editor_property('camera')
    boom.modify(); camera.modify()
    boom.set_editor_property('absolute_rotation',True)
    boom.set_relative_location(unreal.Vector(0,0,0),False,False)
    boom.set_relative_rotation(unreal.Rotator(pitch=aim['PitchDegrees'],yaw=aim['YawDegrees'],roll=0),False,False)
    boom.set_editor_property('target_arm_length',aim['ArmLength'])
    boom.set_editor_property('target_offset',unreal.Vector(0,0,0))
    v=aim['SocketOffset']
    if isinstance(v,str): v={a:float(n) for a,n in re.findall(r'([XYZ])=([-+0-9.eE]+)',v)}
    boom.set_editor_property('socket_offset',unreal.Vector(v['X'],v['Y'],v['Z']))
    boom.set_editor_property('do_collision_test',True)
    camera.set_relative_location(unreal.Vector(0,0,0),False,False)
    camera.set_relative_rotation(unreal.Rotator(0,0,0),False,False)
    camera.set_editor_property('field_of_view',aim['FieldOfView'])
    assert lib.save_loaded_asset(bp,False)
    result['aim_camera']=aim
    result['manifests']=[]
    if export_manifests:
        world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        assert world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype', 'Load LVL_CommanderMassPrototype before importing Ground tuning'
        for population in (200,600):
            path=root/f'Config/Hybrid/RuntimeManifest-{population}.json'
            exported=json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population,str(path)))
            if not exported.get('success'): raise RuntimeError(exported)
            manifest=json.loads(path.read_text(encoding='utf-8-sig'))
            assert manifest['strings']['ground/mobility_configuration']==result['configuration_fingerprint']
            result['manifests'].append({'path':str(path),'config_hash':manifest['config_hash'],'population':population})
    return result

if __name__=='__main__':
    unreal.MCPythonHelper.submit_result(json.dumps(sync_mobility_assets()))
