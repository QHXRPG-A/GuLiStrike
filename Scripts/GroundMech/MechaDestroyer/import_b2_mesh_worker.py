"""Import only the preflight-confirmed new asset root in an isolated editor."""
import unreal,json,traceback,hashlib,runpy
from pathlib import Path
ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export'
BASE='/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2'
LIB=unreal.EditorAssetLibrary
def tr(t):return {'t':list(t.translation.to_tuple()),'q':[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w],'s':list(t.scale3d.to_tuple())}
def main():
    assert '-GroundMechB2ImportWorker' in unreal.SystemLibrary.get_command_line()
    before=json.loads((OUT.parents[1]/'b2_integration_before.json').read_text())
    assert before['new_root_exists'] is False
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    cv='Interchange.FeatureFlags.Import.Enable';prev=unreal.SystemLibrary.get_console_variable_int_value(cv)
    unreal.SystemLibrary.execute_console_command(world,cv+' 0')
    try:
        target=BASE+'/Meshes/SK_GroundMech_A4B2'
        assert not LIB.does_asset_exist(target),'Do not overwrite assets from a worker while live editor may hold them'
        ui=runpy.run_path(str(ROOT/'Scripts/import_industrial_defense_models.py'))['options'](skeletal=True)
        ui.skeletal_mesh_import_data.set_editor_property('import_meshes_in_bone_hierarchy',True)
        task=unreal.AssetImportTask()
        for k,v in dict(filename=str(OUT/'SK_GroundMech_A4B2.fbx'),destination_path=BASE+'/Meshes',destination_name='SK_GroundMech_A4B2',automated=True,save=False,replace_existing=False,options=ui,factory=unreal.FbxFactory()).items():task.set_editor_property(k,v)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        mesh=unreal.load_asset(target);assert mesh, str(task.imported_object_paths)
        bones=list(unreal.SkeletonService.list_bones(target))
        weight=unreal.SkinWeightModifier();assert weight.set_skeletal_mesh(mesh)
        invalid=0;counts={str(b.bone_name):0 for b in bones}
        for i in range(weight.get_num_vertices()):
            w={str(k):v for k,v in weight.get_vertex_weights(i).items() if v>1e-5}
            if len(w)!=1 or abs(sum(w.values())-1)>1e-4:invalid+=1
            else:counts[next(iter(w))]+=1
        bounds=mesh.get_imported_bounds()
        report={'success':invalid==0,'asset':mesh.get_path_name(),'skeleton':mesh.skeleton.get_path_name(),'bones':[{'name':str(b.bone_name),'parent':str(b.parent_bone_name),'local':tr(b.local_transform),'global':tr(b.global_transform)} for b in bones],'materials':[str(m.material_slot_name) for m in mesh.materials],'dimensions_cm':list((bounds.box_extent*2).to_tuple()),'bounds_min':list((bounds.origin-bounds.box_extent).to_tuple()),'invalid_skin_weights':invalid,'rigid_vertex_counts':counts,'num_vertices':weight.get_num_vertices()}
        expected=json.loads((OUT/'export_report.json').read_text())
        assert len(bones)==17
        assert max(abs(a-b) for a,b in zip(report['dimensions_cm'],expected['dimensions_cm']))<.1
        assert all(max(abs(v-1) for v in b['local']['s'])<1e-4 for b in report['bones'])
        for o in [mesh,mesh.skeleton]:
            LIB.set_metadata_tag(o,'GuLi.ModelProduction.Owner','GuLi.GroundMech.ApprovedA4.B2')
            LIB.set_metadata_tag(o,'GuLi.ModelProduction.SourceSHA256',expected['source_sha256'])
            assert LIB.save_loaded_asset(o,False)
        (OUT/'ue_mesh_import.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    except Exception:
        (OUT/'ue_mesh_import.json').write_text(json.dumps({'success':False,'error':traceback.format_exc()},indent=2),encoding='utf8')
        unreal.log_error(traceback.format_exc())
    finally:
        unreal.SystemLibrary.execute_console_command(world,cv+' '+str(prev))
        unreal.SystemLibrary.quit_editor()
if __name__=='__main__':main()
