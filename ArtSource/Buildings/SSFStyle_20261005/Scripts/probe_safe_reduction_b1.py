"""Measure conservative per-component simplification without hiding any component."""
import bpy,bmesh,json,math
from pathlib import Path
from collections import defaultdict
from mathutils import Matrix
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1'
bpy.ops.wm.open_mainfile(filepath=str(R/'References_A_v7/SSF_ReferenceDesign_v7.blend'))
base=json.loads((R/'Baseline/blender_source_manifest.json').read_text())
rows=[]
for a in base['assets']:
 s=bpy.data.scenes[a['scene']];bpy.context.window.scene=s
 totals=defaultdict(int);parts=[]
 for part in a['parts']:
  src=bpy.data.objects[part['object']];polys=[src.data.polygons[i] for i in part['polygons']]
  ids=sorted({v for p in polys for v in p.vertices});mp={v:i for i,v in enumerate(ids)}
  me=bpy.data.meshes.new('Probe');me.from_pydata([src.matrix_world@src.data.vertices[i].co for i in ids],[],[[mp[v] for v in p.vertices] for p in polys]);me.update()
  for m in src.data.materials:me.materials.append(m)
  for p,q in zip(me.polygons,polys):p.material_index=q.material_index
  orig=len(polys);totals['source']+=orig
  for angle in (.001,.01,.04):
   dest=me.copy();bm=bmesh.new();bm.from_mesh(dest)
   bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.000002)
   bmesh.ops.dissolve_limit(bm,angle_limit=angle,verts=list(bm.verts),edges=list(bm.edges),use_dissolve_boundaries=False,delimit={'MATERIAL'})
   bm.to_mesh(dest);bm.free();dest.update();dest.calc_loop_triangles();totals[str(angle)]+=len(dest.loop_triangles)
   if angle==.001:
    ob=bpy.data.objects.new('Probe',dest);s.collection.objects.link(ob)
    for ratio in (.6,.35,.15):
     mod=ob.modifiers.new('Reduction','DECIMATE');mod.ratio=ratio;mod.use_collapse_triangulate=True
     ev=ob.evaluated_get(bpy.context.evaluated_depsgraph_get());em=ev.to_mesh();em.calc_loop_triangles()
     tri=len(em.loop_triangles);totals[str(ratio)]+=tri;totals['lost'+str(ratio)]+=int(tri==0);ev.to_mesh_clear();ob.modifiers.remove(mod)
    bpy.data.objects.remove(ob,do_unlink=True)
   bpy.data.meshes.remove(dest)
  bpy.data.meshes.remove(me)
 rows.append({'asset':a['key'],'parts':len(a['parts']),**totals})
 print('SAFE_REDUCTION_PROBE',rows[-1],flush=True)
(O/'reduction_probe.json').write_text(json.dumps(rows,indent=2))
