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
    ik_table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeMech_GroundIK')
    ik=next(row for row in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(ik_table)) if row['Name']=='Default')
    contact_signature=json.dumps({'version':2,'tolerance':ik['ContactHeightTolerance'],'blend_height':ik['ContactBlendHeight']},sort_keys=True)
    contacts_rebaked=False
    if lib.get_metadata_tag(ik_table,'GroundContactBake')!=contact_signature:
        mesh=unreal.load_asset('/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Meshes/SK_GroundMech_A4B2')
        contacts=json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.bake_ground_contacts(mesh,'/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Animations',ik['ContactHeightTolerance'],ik['ContactBlendHeight']))
        if not contacts['success']: raise RuntimeError(contacts)
        for clip in contacts['clips']:
            assert lib.save_loaded_asset(unreal.load_asset(clip['asset']),False)
        ik_table.modify()
        lib.set_metadata_tag(ik_table,'GroundContactBake',contact_signature)
        assert lib.save_loaded_asset(ik_table,False)
        contacts_rebaked=True
    result=json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.configure_mobility_assets(True))
    if not result['success']: raise RuntimeError(result)
    result['contact_curves_rebaked']=contacts_rebaked
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
