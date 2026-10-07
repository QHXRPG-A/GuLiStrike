"""Export vertex texture source for pixel/dimension readback without binary asset reads."""
import unreal,json,sys
from pathlib import Path
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART,REVIEW_PACKAGE
out=ART/'BiZhiMao/Reports/NativeTextureSource';out.mkdir(exist_ok=True)
meta=json.loads((ART/'BiZhiMao/vertex_metadata.json').read_text(encoding='utf8'))
rows=[]
for lod in meta['lods']:
 for role in ['Position','Rotation']:
  tex=unreal.load_asset(REVIEW_PACKAGE+f'/BiZhiMao/Textures/T_BiZhiMao_Vertex{role}_LOD{lod["lod"]}')
  task=unreal.AssetExportTask();file=out/(tex.get_name()+('.exr' if role=='Position' else '.tga'))
  for key,value in dict(object=tex,filename=str(file),automated=True,replace_identical=True,prompt=False,
   exporter=unreal.TextureExporterEXR() if role=='Position' else unreal.TextureExporterTGA()).items():task.set_editor_property(key,value)
  assert unreal.Exporter.run_asset_export_task(task),file
  rows.append(dict(asset=tex.get_path_name(),file=str(file),source_id=tex.blueprint_get_texture_source_id_string(),
   resource_dimensions=[tex.blueprint_get_size_x(),tex.blueprint_get_size_y()],source_bytes=list(tex.blueprint_get_texture_source_disk_and_memory_size())))
(ART/'BiZhiMao/Reports/native_texture_exports.json').write_text(json.dumps(rows,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,textures=rows)))
