"""Read actual material texture dependencies and their source dimensions."""
import unreal,json,sys
from pathlib import Path
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART,REVIEW_PACKAGE
native=json.loads((ART/'Reports/candidate_readback.json').read_text(encoding='utf8'))
rows=[]
for unit in native['units']:
 materials={};textures={}
 for item in unit['meshes']:
  mesh=unreal.load_asset(item['asset'])
  slots=mesh.get_editor_property('materials') if isinstance(mesh,unreal.SkeletalMesh) else mesh.get_editor_property('static_materials')
  for slot in slots:
   material=slot.get_editor_property('material_interface')
   if material:materials[material.get_path_name()]=material
 if unit['name'] in ['ElectromagneticMiner','ConstructionVehicle']:
  vehicles=json.loads((ART/'Reports/vehicle_instances.json').read_text(encoding='utf8'))
  for comp in next(v for v in vehicles if v['name']==unit['name'])['components']:
   for path in comp['materials']:
    if path:materials[path]=unreal.load_asset(path)
 for path,mat in materials.items():
  dependencies=[];base=mat
  while isinstance(base,unreal.MaterialInstanceConstant):
   for parameter in base.get_editor_property('texture_parameter_values'):
    tex=parameter.get_editor_property('parameter_value')
    if tex:dependencies.append(tex)
   base=base.get_editor_property('parent')
  if isinstance(base,unreal.Material):dependencies.extend(unreal.MaterialEditingLibrary.get_used_textures(base))
  for tex in dependencies:
   if not isinstance(tex,unreal.Texture2D):continue
   textures[tex.get_path_name()]=dict(asset=tex.get_path_name(),width=tex.blueprint_get_size_x(),height=tex.blueprint_get_size_y(),
    compression=str(tex.compression_settings),srgb=tex.srgb,mip_policy=str(tex.mip_gen_settings))
 rows.append(dict(name=unit['name'],material_dependencies=list(materials),textures=list(textures.values()),
  budget_note='Material dependency union including base defaults and instance overrides; source dimensions and compression. Resident GPU bytes depend on cooked formats and streaming.'))
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world,'Editor.AsyncTextureCompilationFinishAll')
for row in rows:
 for item in row['textures']:
  texture=unreal.load_asset(item['asset'])
  item.update(width=texture.blueprint_get_size_x(),height=texture.blueprint_get_size_y(),editor_resource_bytes=texture.blueprint_get_memory_size())
(ART/'Reports/texture_budget.json').write_text(json.dumps(dict(success=True,units=rows),ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,units=[dict(name=r['name'],textures=len(r['textures'])) for r in rows])))
