"""Refresh candidate interchange files after preserving section bindings; no formal asset edits."""
import unreal,json,sys
from pathlib import Path
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'));from common import ART
from common import require_unapproved_candidate
require_unapproved_candidate()
native=json.loads((ART/'Reports/stage_native.json').read_text(encoding='utf8'))
for unit in native['units']:
 for item in unit['meshes']:
  asset=unreal.load_asset(item['asset']);options=unreal.FbxExportOption();options.level_of_detail=True;options.collision=False
  task=unreal.AssetExportTask()
  for key,value in dict(object=asset,filename=item['fbx'],options=options,automated=True,replace_identical=True,prompt=False,
   exporter=unreal.SkeletalMeshExporterFBX() if item['type']=='SkeletalMesh' else unreal.StaticMeshExporterFBX()).items():task.set_editor_property(key,value)
  assert unreal.Exporter.run_asset_export_task(task)
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,units=len(native['units']))))
