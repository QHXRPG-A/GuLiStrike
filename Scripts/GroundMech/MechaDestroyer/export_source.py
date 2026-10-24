"""Export source geometry and textures for reference design; do not resave originals."""
import unreal, json
from pathlib import Path
ART=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023')
OUT=ART/'SourceExport'
OUT.mkdir(parents=True,exist_ok=True)
options=unreal.FbxExportOption()
options.ascii=False
options.collision=False
options.level_of_detail=False
options.set_editor_property('bake_material_inputs',unreal.FbxMaterialBakeMode.DISABLED)
reports=[]
for path,name,exporter in [('/Game/MechaDestroyer/SKM/SKM_Mecha','SKM_Mecha.fbx',unreal.SkeletalMeshExporterFBX()),('/Game/MechaDestroyer/Animation/Anim_Mecha_Idle','Anim_Mecha_Idle.fbx',unreal.AnimSequenceExporterFBX()),('/Game/MechaDestroyer/TXT/TX_Mecha_D','TX_Mecha_D.tga',unreal.TextureExporterTGA())]:
    dest=OUT/name
    if not dest.exists():
        t=unreal.AssetExportTask()
        t.object=unreal.load_asset(path);t.filename=str(dest);t.automated=True;t.prompt=False;t.replace_identical=False;t.exporter=exporter
        if name.endswith('.fbx'):t.options=options
        assert unreal.Exporter.run_asset_export_task(t),name
    assert dest.exists()
    reports.append({'path':str(dest),'bytes':dest.stat().st_size})
data={'success':True,'exports':reports}
(ART/'source_exports.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(data))
