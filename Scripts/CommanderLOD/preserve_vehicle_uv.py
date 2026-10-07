"""Freeze the existing render UVs before generating lower vehicle LODs."""
import json,sys
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART
from common import require_unapproved_candidate
require_unapproved_candidate()
lib=unreal.EditorAssetLibrary;sub=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
report=json.loads((ART/'Reports/stage_native.json').read_text(encoding='utf8'))
before=json.loads((ART/'Reports/formal_before.json').read_text(encoding='utf8'))
changed=[]
for unit in report['units']:
    if not unit['components']:continue
    original_unit=next(row for row in before['units'] if row['name']==unit['name'])
    for item in unit['meshes']:
        if item['type']!='StaticMesh':continue
        mesh=unreal.load_asset(item['asset'])
        original=next(row for row in original_unit['meshes'] if row['asset']==item['source'])
        slots=list(mesh.static_materials)
        ui=unreal.FbxImportUI()
        for key,value in dict(import_materials=False,import_textures=False,import_mesh=True,import_as_skeletal=False,
            import_animations=False,automated_import_should_detect_type=False,
            mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH,original_import_type=unreal.FBXImportType.FBXIT_STATIC_MESH).items():ui.set_editor_property(key,value)
        for key,value in dict(combine_meshes=True,import_mesh_lo_ds=False,auto_generate_collision=False,generate_lightmap_u_vs=False,
            build_nanite=False,remove_degenerates=False,normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS,
            vertex_color_import_option=unreal.VertexColorImportOption.REPLACE).items():ui.static_mesh_import_data.set_editor_property(key,value)
        task=unreal.AssetImportTask()
        folder,name=mesh.get_path_name().split('.')[0].rsplit('/',1)
        folder=folder.replace('/Meshes','/Sources');name+='_RenderLOD0'
        temp=unreal.load_asset(folder+'/'+name) if lib.does_asset_exist(folder+'/'+name) else None
        if temp is None:
            for key,value in dict(filename=original['fbx'],destination_path=folder,destination_name=name,automated=True,
                replace_existing=False,save=False,options=ui,factory=unreal.FbxFactory()).items():task.set_editor_property(key,value)
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
            temp=unreal.load_asset(folder+'/'+name)
        assert temp
        assert sub.set_lod_from_static_mesh(mesh,0,temp,0,True)==0
        mesh.static_materials=slots
        source=unreal.load_asset(item['source'])
        for lod in range(3):
            for section in range(mesh.get_num_sections(lod)):
                sub.set_lod_material_slot(mesh,sub.get_lod_material_slot(source,0,min(section,source.get_num_sections(0)-1)),lod,section)
        assert sub.get_lod_count(mesh)==3
        assert mesh.get_num_triangles(0)==original['lods'][0]['triangles']
        for lod in range(3):
            build=sub.get_lod_build_settings(mesh,lod)
            build.generate_lightmap_u_vs=False
            sub.set_lod_build_settings(mesh,lod,build)
        assert lib.save_loaded_asset(mesh,False)
        options=unreal.FbxExportOption();options.level_of_detail=True;options.collision=False
        task=unreal.AssetExportTask()
        for key,value in dict(object=mesh,filename=item['fbx'],options=options,automated=True,replace_identical=True,
            prompt=False,exporter=unreal.StaticMeshExporterFBX()).items():task.set_editor_property(key,value)
        assert unreal.Exporter.run_asset_export_task(task)
        item['triangles']=[mesh.get_num_triangles(i) for i in range(3)]
        changed.append(item['asset'])
(ART/'Reports/stage_native.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,changed=changed)))
