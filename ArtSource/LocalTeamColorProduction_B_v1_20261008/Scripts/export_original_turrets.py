import unreal, json, hashlib
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/LocalTeamColorProduction_B_v1_20261008')
report={'purpose':'read-only original geometry export for color-only Blender review','formal_assets_modified':False,'assets':[]}
sources={
 'MissileTurret':'/Game/Assets/Props/Buildings/Missile_Turret/missile_turret/StaticMeshes/missile_turret.missile_turret',
 'SentryTurret':'/Game/Assets/Props/Buildings/Stylized_Turrets_Tower_Defense/Stylized_Turrets_A_a.Stylized_Turrets_A_a',
 'ExistingFactoryAccessRampCube':'/Engine/BasicShapes/Cube.Cube'}
for key,path in sources.items():
    mesh=unreal.load_asset(path)
    assert isinstance(mesh,unreal.StaticMesh),path
    filename=OUT/'References'/(key+'_Original_RenderLODs.fbx')
    options=unreal.FbxExportOption()
    options.ascii=False;options.collision=False;options.level_of_detail=True
    task=unreal.AssetExportTask()
    task.object=mesh;task.filename=str(filename);task.options=options
    task.automated=True;task.replace_identical=True;task.prompt=False
    task.exporter=unreal.StaticMeshExporterFBX()
    assert unreal.Exporter.run_asset_export_task(task),path
    mats=[]
    for slot in mesh.get_editor_property('static_materials'):
        m=slot.material_interface
        rec={'slot':str(slot.material_slot_name),'path':m.get_path_name() if m else None}
        if isinstance(m,unreal.MaterialInstanceConstant):
            for prop in ('scalar_parameter_values','vector_parameter_values','texture_parameter_values'):
                rec[prop]=[str(p) for p in m.get_editor_property(prop)]
        mats.append(rec)
    report['assets'].append({'key':key,'asset':path,'file':str(filename),'sha256':hashlib.sha256(filename.read_bytes()).hexdigest(),'materials':mats,'lod_count':mesh.get_num_lods(),'bounds':str(mesh.get_bounds())})
report['success']=True
(OUT/'Reports/ue-source-export.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'exported':[a['key'] for a in report['assets']],'formal_assets_modified':False}))
