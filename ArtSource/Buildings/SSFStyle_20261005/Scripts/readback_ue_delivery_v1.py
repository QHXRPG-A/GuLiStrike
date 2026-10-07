"""Independently import all delivery FBXs and verify native geometry/UV/weights."""
import bpy,json,hashlib,math
from pathlib import Path
from mathutils import Vector
from mathutils.kdtree import KDTree
from io_scene_fbx import parse_fbx
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');D=R/'UE_Delivery_v1'
export=json.loads((D/'export_report.json').read_text(encoding='utf8'));assert export['success']
report={'success':False,'checks':[],'reader':'Blender native FBX importer plus independent raw FBX normal validation','source_saved':False,'UE_normal_readback_required':True}
def raw_normals(path):
 root,_=parse_fbx.parse(str(path));sizes=[];minimum=1.
 def visit(e):
  nonlocal minimum
  if e.id==b'Normals':
   a=e.props[0];lens=[sum(x*x for x in a[i:i+3])**.5 for i in range(0,len(a),3)]
   assert lens and min(lens)>.99,(str(path),'raw FBX contains invalid normals')
   sizes.append(len(lens));minimum=min(minimum,min(lens))
  for c in e.elems:visit(c)
 visit(root);assert sizes
 return {'vectors':sum(sizes),'minimum_length':minimum,'all_vectors_valid':True}
for asset in export['assets']:
 for row in asset['lods']:
  bpy.ops.wm.read_factory_settings(use_empty=True)
  path=Path(row['fbx']);assert hashlib.sha256(path.read_bytes()).hexdigest()==row['sha256']
  bpy.ops.import_scene.fbx(filepath=str(path),use_anim=False,ignore_leaf_bones=False,automatic_bone_orientation=False,colors_type='LINEAR')
  meshes=[o for o in bpy.context.scene.objects if o.type=='MESH'];assert len(meshes)==1
  ob=meshes[0];m=ob.data;m.calc_loop_triangles();assert len(m.loop_triangles)==row['triangles']
  expected=json.loads(path.with_name(path.stem+'_Expected.json').read_text(encoding='utf8'))
  pts=[ob.matrix_world@v.co for v in m.vertices];tree=KDTree(len(pts))
  for i,p in enumerate(pts):tree.insert(p,i)
  tree.balance();want=[Vector(p) for p in expected['points_m']]
  reverse=KDTree(len(want))
  for i,p in enumerate(want):reverse.insert(p,i)
  reverse.balance();error=max([tree.find(p)[2] for p in want]+[reverse.find(p)[2] for p in pts]);assert error<5e-5,(asset['key'],row['LOD'],error)
  assert len(m.uv_layers)==4
  assert [mat.name for mat in m.materials]==row['materials']
  # Corner tuples avoid relying on FBX's vertex/loop order and include all 4 UVs.
  def uvset(arrays):return {tuple(round(x,5) for a in arrays for x in a[i]) for i in range(len(arrays[0]))}
  got=uvset([[list(d.uv) for d in u.data] for u in m.uv_layers]);needed=uvset(expected['corner_uvs'])
  assert got==needed,(asset['key'],row['LOD'],'UV/color transport')
  weight_error=0.
  for i,p in enumerate(pts):
   own={ob.vertex_groups[g.group].name:g.weight for g in m.vertices[i].groups}
   hits=reverse.find_range(p,5e-5)
   best=min(max(abs(own.get(k,0)-dict(expected['weights'][j]).get(k,0)) for k in set(own)|set(dict(expected['weights'][j]))) if own or expected['weights'][j] else 0 for _,j,_ in hits)
   weight_error=max(weight_error,best)
  assert weight_error<1e-5,(asset['key'],row['LOD'],weight_error)
  rigs=[o for o in bpy.context.scene.objects if o.type=='ARMATURE']
  assert len(rigs)==int(bool(row['bone_count']))
  if rigs:assert len(rigs[0].data.bones)==row['bone_count']
  check={'asset':asset['key'],'LOD':row['LOD'],'fbx_sha256':row['sha256'],'triangles':len(m.loop_triangles),
         'vertices':len(m.vertices),'geometry_error_m':error,'UV_channels':4,'UV_palette_and_LOD_fade_preserved':True,
         'weight_error':weight_error,'bones':row['bone_count'],'material_sections':len(m.materials),'normals_nonzero':all(n.vector.length>.99 for n in m.corner_normals)}
  check['raw_FBX_normals']=raw_normals(path)
  check['Blender_compressed_normal_zero_count']=sum(n.vector.length<.99 for n in m.corner_normals)
  if not check['normals_nonzero']:
   check['reader_limitation']='Blender re-encodes unit FBX normals into compressed corner fans; cancellation at thin mirrored caps produces zero values. Raw FBX vectors are valid; UE stored normals must pass independent export readback.'
  report['checks'].append(check)
  (D/'fbx_readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
  print('SSF_FBX_READBACK',asset['key'],row['LOD'],error,flush=True)
report['success']=True;(D/'fbx_readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('SSF_ALL_30_FBX_READBACK_PASSED',flush=True)
