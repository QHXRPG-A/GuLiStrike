"""Independent proof from UE-exported stored mesh data, including all three LODs."""
import bpy,json,math,traceback
from pathlib import Path
from io_scene_fbx import parse_fbx
from mathutils import Vector
from mathutils.kdtree import KDTree
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');D=R/'UE_Delivery_v1'
im=json.loads((D/'ue_import.json').read_text(encoding='utf8'));assert im['success']
export={a['key']:a for a in json.loads((D/'export_report.json').read_text(encoding='utf8'))['assets']}
source={a['key']:a for a in json.loads((R/'Source/source_manifest.json').read_text(encoding='utf8'))['meshes']}
result={'success':False,'checks':[],'reader':'Blender native importer and independent FBX vector parser','source_modified':False}
def raw_geometry(path):
 root,_=parse_fbx.parse(str(path));objects=next(x for x in root.elems if x.id==b'Objects');rows=[]
 for g in objects.elems:
  if g.id!=b'Geometry' or g.props[-1]!=b'Mesh':continue
  lookup={e.id:e for e in g.elems};norm=next(e for e in g.elems if e.id==b'LayerElementNormal')
  vals=next(e.props[0] for e in norm.elems if e.id==b'Normals');lens=[sum(x*x for x in vals[i:i+3])**.5 for i in range(0,len(vals),3)]
  assert lens and min(lens)>.99,(str(path),'UE stored normals',min(lens))
  uv=[e for e in g.elems if e.id==b'LayerElementUV'];assert len(uv)==4,(str(path),'UV count',len(uv))
  corners=lookup[b'PolygonVertexIndex'].props[0];faces=[];cur=[]
  for i in corners:
   cur.append(i if i>=0 else -i-1)
   if i<0:faces.append(cur);cur=[]
  rows.append({'geometry':str(g.props[1]),'triangles':sum(len(f)-2 for f in faces),'raw_normal_minimum_length':min(lens),
               'raw_normal_vectors':len(lens),'UV_channels':len(uv)})
 return rows
try:
 for asset in im['assets']:
  key=asset['key'];p=Path(asset['UE_export_readback']);raw=raw_geometry(p)
  assert len(raw)==3,(key,'UE FBX must include all three actual LODs',raw)
  expected_counts=sorted(a['triangles'] for a in export[key]['lods']);assert sorted(r['triangles'] for r in raw)==expected_counts,(key,raw,expected_counts)
  # UE exports no synthetic leaf bones. Dropping leaves can remap a real,
  # weighted last joint (e.g. Anten0) to its parent during Blender import.
  bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(p),use_anim=False,ignore_leaf_bones=False,colors_type='LINEAR')
  obs=[o for o in bpy.context.scene.objects if o.type=='MESH'];assert len(obs)==3
  remaining=list(obs)
  for row in export[key]['lods']:
   candidates=[]
   expected=json.loads(Path(row['fbx']).with_name(Path(row['fbx']).stem+'_Expected.json').read_text(encoding='utf8'));want=[Vector(p) for p in expected['points_m']]
   kt=KDTree(len(want))
   for i,v in enumerate(want):kt.insert(v,i)
   kt.balance()
   for ob in remaining:
    ob.data.calc_loop_triangles()
    if len(ob.data.loop_triangles)!=row['triangles']:continue
    pts=[ob.matrix_world@v.co for v in ob.data.vertices];tree=KDTree(len(pts))
    for i,v in enumerate(pts):tree.insert(v,i)
    tree.balance();err=max([kt.find(v)[2] for v in pts]+[tree.find(v)[2] for v in want]);candidates.append((err,ob))
   assert candidates,(key,row['LOD']);error,ob=min(candidates,key=lambda x:x[0]);remaining.remove(ob)
   assert error<5e-5,(key,row['LOD'],error)
   m=ob.data
   # UE's export converts texture V back to FBX. Palette and distance data
   # must survive, including every distinct color/LOD/team ownership tuple.
   got={tuple(round(x,5) for uv in list(m.uv_layers)[2:] for x in uv.data[i].uv) for i in range(len(m.loops))}
   wantuv={tuple(round(x,5) for uv in expected['corner_uvs'][2:] for x in uv[i]) for i in range(len(expected['corner_uvs'][0]))}
   assert got==wantuv,(key,row['LOD'],'palette/fade/team UV readback',list(got-wantuv)[:3],list(wantuv-got)[:3])
   used={p.material_index for p in m.polygons};limit=3 if key not in ('Floor','Lamp','Drone','Light') else (1 if key in ('Lamp','Light') or row['LOD']==2 else 2)
   assert len(used)<=limit,(key,row['LOD'],len(used),limit)
   weights=0.;weight_examples=[]
   if asset['type']=='SkeletalMesh':
    # UE's FBX exporter uses FMeshBoneInfo.ExportName, which may retain the
    # original 'Bone' prefix. Match only exact, unique logical-name aliases;
    # the independent UE reload verifies actual names/hierarchy unchanged.
    logical_names={bone['name'] for bone in source[key]['bones']};aliases={}
    for group in ob.vertex_groups:
     name=group.name
     if name not in logical_names:
      assert name.startswith('Bone') and name[4:] in logical_names,(key,'unexpected FBX export bone name',name)
      aliases[name]=name[4:]
    assert len(set(aliases.values()))==len(aliases)
    for v in m.vertices:
     own={aliases.get(ob.vertex_groups[g.group].name,ob.vertex_groups[g.group].name):g.weight for g in v.groups};hits=kt.find_range(ob.matrix_world@v.co,5e-5)
     best=min(max(abs(own.get(k,0)-dict(expected['weights'][j]).get(k,0)) for k in set(own)|set(dict(expected['weights'][j]))) if own or expected['weights'][j] else 0 for _,j,_ in hits)
     if best>=.0001 and len(weight_examples)<4:weight_examples.append({'actual':own,'expected_candidates':[expected['weights'][j] for _,j,_ in hits][:4]})
     weights=max(weights,best)
    assert weights<.0001,(key,row['LOD'],'weights',weights,weight_examples)
    result.setdefault('FBX_export_name_aliases',{})[key]=aliases
   result['checks'].append({'key':key,'LOD':row['LOD'],'triangles':len(m.loop_triangles),'geometry_error_m':error,
    'UV_channels':4,'palette_and_LOD_team_data_preserved':True,'weight_error':weights,'actual_used_material_sections':len(used),
    'UE_stored_normals_valid':True,'Blender_reader_compression_zero_corners':sum(n.vector.length<.99 for n in m.corner_normals)})
   print('SSF_UE_READBACK_PASSED',key,row['LOD'],error,weights,flush=True)
  result.setdefault('raw_geometry',{})[key]=raw
 result['success']=True
except Exception:result['error']=traceback.format_exc();print(result['error'],flush=True)
(D/'ue_mesh_readback.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
