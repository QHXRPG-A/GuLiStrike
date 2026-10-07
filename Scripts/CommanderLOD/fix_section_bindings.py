"""Preserve selected source LOD section/material bindings after copying geometry."""
import unreal,json,sys
from pathlib import Path
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART,REVIEW_PACKAGE,selected_indices
from common import require_unapproved_candidate
require_unapproved_candidate()
native=json.loads((ART/'Reports/stage_native.json').read_text(encoding='utf8'))
sub=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem);lib=unreal.EditorAssetLibrary
rows=[]
for unit in native['units']:
 for item in unit['meshes']:
  if item['type']!='StaticMesh':continue
  mesh=unreal.load_asset(item['asset']);source=unreal.load_asset(item['source']);count=sub.get_lod_count(source)
  keep=selected_indices(count) if count>=3 else [0,0,0]
  bindings=[]
  for lod,source_lod in enumerate(keep):
   assert mesh.get_num_sections(lod)==source.get_num_sections(source_lod)
   before=[sub.get_lod_material_slot(mesh,lod,s) for s in range(mesh.get_num_sections(lod))]
   expected=[sub.get_lod_material_slot(source,source_lod,s) for s in range(source.get_num_sections(source_lod))]
   for section,index in enumerate(expected):sub.set_lod_material_slot(mesh,index,lod,section)
   actual=[sub.get_lod_material_slot(mesh,lod,s) for s in range(mesh.get_num_sections(lod))]
   assert actual==expected
   bindings.append(dict(lod=lod,previous=before,selected_source=expected,candidate=actual))
  assert lib.save_loaded_asset(mesh,False)
  rows.append(dict(name=unit['name'],mesh=mesh.get_path_name(),sections=bindings))
for kind in ['VAT','Construction']:
 mesh=unreal.load_asset(REVIEW_PACKAGE+'/BiZhiMao/Meshes/SM_BiZhiMao_'+kind)
 slots=list(mesh.static_materials);names=[s.material_interface.get_name() for s in slots];bindings=[]
 for lod in range(3):
  body=names.index('MI_BiZhiMao_LOD'+str(lod));expected=[body]
  if lod<2:expected.append(names.index('M_BiZhiMao_Outline'))
  previous=[sub.get_lod_material_slot(mesh,lod,s) for s in range(mesh.get_num_sections(lod))]
  assert len(expected)==mesh.get_num_sections(lod)
  for section,index in enumerate(expected):sub.set_lod_material_slot(mesh,index,lod,section)
  bindings.append(dict(lod=lod,previous=previous,candidate=expected))
 assert lib.save_loaded_asset(mesh,False)
 rows.append(dict(name='BiZhiMao',mesh=mesh.get_path_name(),sections=bindings))
(ART/'Reports/material_section_readback.json').write_text(json.dumps(dict(success=True,meshes=rows),indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,meshes=len(rows),corrections=[r for r in rows if any(s.get('previous')!=s.get('candidate') for s in r['sections'])])))
