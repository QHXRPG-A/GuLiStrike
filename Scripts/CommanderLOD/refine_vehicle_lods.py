"""Spend the available vehicle budget on retained wheel and collector structure."""
import sys,json
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART
from common import require_unapproved_candidate
require_unapproved_candidate()
lib=unreal.EditorAssetLibrary;static=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
skeletal=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
report=json.loads((ART/'Reports/stage_native.json').read_text(encoding='utf8'))
for unit in report['units']:
    if not unit['components']:continue
    for item in unit['meshes']:
        mesh=unreal.load_asset(item['asset']);skin=isinstance(mesh,unreal.SkeletalMesh)
        if skin:
            models=list(mesh.get_editor_property('source_models'))
            for lod,ratio in [(1,.40),(2,.09)]:
                setting=models[lod].get_editor_property('reduction_settings')
                setting.set_editor_property('num_of_triangles_percentage',ratio)
                models[lod].set_editor_property('reduction_settings',setting)
            mesh.set_editor_property('source_models',models)
            assert skeletal.regenerate_lod(mesh,3,True,False)
        else:
            for lod,ratio in [(1,.40),(2,.09)]:
                setting=static.get_lod_reduction_settings(mesh,lod)
                setting.percent_triangles=ratio;setting.base_lod_model=0
                static.set_lod_reduction_settings(mesh,lod,setting)
            item['triangles']=[mesh.get_num_triangles(i) for i in range(3)]
        assert lib.save_loaded_asset(mesh,False)
        options=unreal.FbxExportOption();options.level_of_detail=True;options.collision=False
        task=unreal.AssetExportTask()
        for key,value in dict(object=mesh,filename=item['fbx'],options=options,automated=True,replace_identical=True,prompt=False,
            exporter=unreal.SkeletalMeshExporterFBX() if skin else unreal.StaticMeshExporterFBX()).items():task.set_editor_property(key,value)
        assert unreal.Exporter.run_asset_export_task(task)
(ART/'Reports/stage_native.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,ratios=[1,.4,.09])))
