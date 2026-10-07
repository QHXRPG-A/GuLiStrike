"""Read formal references without switching them; compare frozen source hashes."""
import unreal,json,sys,hashlib,math
from pathlib import Path
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART,units
from common import require_unapproved_candidate
require_unapproved_candidate()
before=json.loads((ART/'Reports/formal_before.json').read_text(encoding='utf8'))
static=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
skeletal=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
current={r['Name']:r for r in units()};formal=[]
for row in before['units']:
 data=current[row['name']]
 for key,old in [('PresentationScale','presentation_scale'),('PresentationClass','presentation_class'),('VATDefinition','vat_definition')]:
  assert data[key]==row[old],(row['name'],key,'formal source table changed')
 counts=[]
 for previous in row['meshes']:
  mesh=unreal.load_asset(previous['asset']);assert static.get_lod_count(mesh)==previous['lod_count']
  triangles=[mesh.get_num_triangles(i) for i in range(previous['lod_count'])]
  assert triangles==[lod['triangles'] for lod in previous['lods']]
  assert [s.material_interface.get_path_name() if s.material_interface else None for s in mesh.static_materials]==[m['path'] for m in previous['materials']]
  assert all(abs(a-b)<1e-5 for a,b in zip(static.get_lod_screen_sizes(mesh),previous['screen_sizes']))
  counts.append(dict(asset=previous['asset'],lod_count=previous['lod_count'],triangles=triangles))
 formal.append(dict(name=row['name'],unchanged=True,models=counts))
vehicles=json.loads((ART/'Reports/vehicle_instances.json').read_text(encoding='utf8'))
for row in vehicles:
 actor=actors.spawn_actor_from_class(unreal.load_class(None,current[row['name']]['PresentationClass']),unreal.Vector(0,0,-60000),transient=True)
 try:
  found=[]
  for comp in actor.get_components_by_class(unreal.MeshComponent):
   if not comp.is_visible():continue
   mesh=comp.get_editor_property('skeletal_mesh_asset' if isinstance(comp,unreal.SkeletalMeshComponent) else 'static_mesh')
   if not mesh:continue
   expected=next(p for p in row['components'] if p['name']==comp.get_name())
   assert mesh.get_path_name()==expected['mesh']
   assert [m.get_path_name() if m else None for m in comp.get_materials()]==expected['materials']
   transform=unreal.MathLibrary.make_relative_transform(comp.get_world_transform(),actor.get_actor_transform())
   assert max(abs(a-b) for a,b in zip(transform.translation.to_tuple(),expected['location']))<.01
   assert max(abs(a-b) for a,b in zip(transform.scale3d.to_tuple(),expected['scale']))<.001
   if isinstance(mesh,unreal.SkeletalMeshComponent):raise AssertionError('unexpected mesh object')
   if isinstance(mesh,unreal.SkeletalMesh):assert skeletal.get_lod_count(mesh)==1
   found.append(comp.get_name())
  assert len(found)==8
 finally:actors.destroy_actor(actor)
frozen=[]
for name,row in json.loads((ART/'Reports/blender_sources.json').read_text(encoding='utf8')).items():
 file=Path(row['path']);digest=hashlib.sha256(file.read_bytes()).hexdigest();assert digest==row['sha256']
 frozen.append(dict(name=name,path=row['path'],sha256=digest,unchanged=True))
report=dict(success=True,formal_switched=False,formal_units=formal,frozen_sources=frozen,vehicle_bindings_unchanged=True,
 native_compile='not_run',pie='not_run',network='not_run',fps='not_run',approval_B='pending')
(ART/'Reports/formal_after.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,formal_units=len(formal),frozen_sources=len(frozen),formal_switched=False)))
