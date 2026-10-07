"""Build isolated three-tier review assets; never update formal unit references."""
import json
import sys
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART,DEFAULT_SCREEN_SIZES,REVIEW_PACKAGE,selected_indices,unit_dir
from common import require_unapproved_candidate
require_unapproved_candidate()
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
lib=unreal.EditorAssetLibrary
static=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
skeletal=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
before=json.loads((ART/'Reports/formal_before.json').read_text(encoding='utf8'))
vehicles={row['name']:row for row in json.loads((ART/'Reports/vehicle_instances.json').read_text(encoding='utf8'))}
report=dict(success=False,approval='pending',formal_assets_changed=False,units=[])


def copy_asset(source,path):
    asset=lib.load_asset(path) if lib.does_asset_exist(path) else lib.duplicate_asset(source,path)
    assert asset,path
    lib.set_metadata_tag(asset,'GuLi.CommanderLOD','3Tier.v1; candidate; B pending')
    lib.set_metadata_tag(asset,'GuLi.SourceAsset',source)
    return asset


def export(asset,path,skin=False):
    options=unreal.FbxExportOption();options.ascii=False;options.collision=False;options.level_of_detail=True
    task=unreal.AssetExportTask()
    for key,value in dict(object=asset,filename=str(path),options=options,automated=True,replace_identical=True,
        prompt=False,exporter=unreal.SkeletalMeshExporterFBX() if skin else unreal.StaticMeshExporterFBX()).items():task.set_editor_property(key,value)
    assert unreal.Exporter.run_asset_export_task(task),path


for unit in before['units']:
    if unit['name']=='BiZhiMao':continue
    out=unit_dir(unit['name']);candidate=dict(id=unit['id'],name=unit['name'],display_name=unit['display_name'],
        presentation_scale=unit['presentation_scale'],components=[],meshes=[])
    if unit['name'] in vehicles:
        candidate['components']=vehicles[unit['name']]['components']
        paths=sorted({component['mesh'] for component in candidate['components']})
    else:paths=[data['asset'] for data in unit['meshes']]
    # Aggregate budget includes repeated mounted parts. The chassis is measured below from FBX.
    static_near=sum(unreal.load_asset(component['mesh']).get_num_triangles(0) for component in candidate['components']
        if component['mesh_type']=='StaticMesh')
    for source_path in paths:
        source=unreal.load_asset(source_path);skin=isinstance(source,unreal.SkeletalMesh)
        path=REVIEW_PACKAGE+'/'+unit['name']+'/Meshes/'+source.get_name()
        mesh=copy_asset(source_path,path)
        if skin:
            assert skeletal.get_lod_count(source)==1
            assert skeletal.regenerate_lod(mesh,3,False,False)
            lod_info=list(mesh.get_editor_property('source_models'))
            for index,ratio in [(1,0.40),(2,0.09)]:
                setting=lod_info[index].get_editor_property('reduction_settings')
                setting.set_editor_property('num_of_triangles_percentage',ratio)
                lod_info[index].set_editor_property('reduction_settings',setting)
                screen=lod_info[index].get_editor_property('screen_size')
                screen.set_editor_property('default',DEFAULT_SCREEN_SIZES[index])
                lod_info[index].set_editor_property('screen_size',screen)
            mesh.set_editor_property('source_models',lod_info)
            assert skeletal.regenerate_lod(mesh,3,True,False)
            assert mesh.get_editor_property('skeleton')==source.get_editor_property('skeleton')
            filename=out/'Sources'/(source.get_name()+'_Original.fbx');export(source,filename,True)
            counts=None
        else:
            count=static.get_lod_count(mesh)
            if count>3:
                keep=selected_indices(count)
                screens=[static.get_lod_screen_sizes(mesh)[index] for index in keep]
                assert static.remove_lods(mesh)
                for target_index,source_index in enumerate(keep[1:],1):
                    assert static.set_lod_from_static_mesh(mesh,target_index,source,source_index,True)==target_index
                    assert mesh.get_num_triangles(target_index)==source.get_num_triangles(source_index)
                assert static.set_lod_screen_sizes(mesh,screens)
            elif count<3:
                # Use conservative part ratios; aggregate results are checked before B review.
                ratios=[1.0,0.40,0.09]
                options=unreal.StaticMeshReductionOptions(auto_compute_lod_screen_size=False,
                    reduction_settings=[unreal.StaticMeshReductionSettings(percent_triangles=ratio,screen_size=screen)
                        for ratio,screen in zip(ratios,DEFAULT_SCREEN_SIZES)])
                assert static.set_lods(mesh,options)==3
                assert static.set_lod_screen_sizes(mesh,list(DEFAULT_SCREEN_SIZES))
            assert static.get_lod_count(mesh)==3
            assert mesh.get_num_triangles(0)==source.get_num_triangles(0)
            source_count=static.get_lod_count(source)
            selected=selected_indices(source_count) if source_count>=3 else [0,0,0]
            for target,source_lod in enumerate(selected):
                for section in range(mesh.get_num_sections(target)):
                    material_index=static.get_lod_material_slot(source,source_lod,section)
                    assert material_index>=0
                    static.set_lod_material_slot(mesh,material_index,target,section)
            counts=[mesh.get_num_triangles(index) for index in range(3)]
        assert lib.save_loaded_asset(mesh,False)
        filename=out/'Sources'/(source.get_name()+'_Candidate.fbx');export(mesh,filename,skin)
        candidate['meshes'].append(dict(source=source_path,asset=mesh.get_path_name(),type=mesh.get_class().get_name(),
            fbx=str(filename),triangles=counts,lod_count=3,lod0_preserved=True,
            skeleton=mesh.get_editor_property('skeleton').get_path_name() if skin else None))
    (out/'Reports/candidate_native.json').write_text(json.dumps(candidate,ensure_ascii=False,indent=2),encoding='utf8')
    report['units'].append(candidate)
report['success']=True
(ART/'Reports/stage_native.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,units=[dict(name=row['name'],meshes=len(row['meshes'])) for row in report['units']])))
