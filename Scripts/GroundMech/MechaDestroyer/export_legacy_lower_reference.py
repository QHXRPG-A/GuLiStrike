"""Read/export the currently deployed Ground legs solely as design reference."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Reference_A_v2')
OUT.mkdir(parents=True, exist_ok=True)
path = '/Game/GuLiStrike/GroundMech/Style_v9/Meshes/SK_Legs'
mesh = unreal.load_asset(path)
assert mesh
dest = OUT / 'OriginalGround_Lower.fbx'
if not dest.exists():
    options = unreal.FbxExportOption()
    options.ascii = False
    options.collision = False
    options.level_of_detail = False
    options.set_editor_property('bake_material_inputs', unreal.FbxMaterialBakeMode.DISABLED)
    task = unreal.AssetExportTask()
    task.object = mesh
    task.filename = str(dest)
    task.automated = True
    task.prompt = False
    task.replace_identical = False
    task.exporter = unreal.SkeletalMeshExporterFBX()
    task.options = options
    assert unreal.Exporter.run_asset_export_task(task)
report = {'success': dest.exists(), 'asset': path, 'export': str(dest),
          'purpose': 'Reference only; no source geometry or gameplay changes',
          'bones': [{'name': str(b.bone_name), 'parent': str(b.parent_bone_name),
                     'location': [b.global_transform.translation.x, b.global_transform.translation.y, b.global_transform.translation.z]}
                    for b in unreal.SkeletonService.list_bones(path)]}
(OUT / 'legacy_lower_source.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report))
