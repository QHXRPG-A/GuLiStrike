import bpy
import bmesh
import json
import math
from pathlib import Path
from mathutils.bvhtree import BVHTree

OUT = Path(__file__).resolve().parent
scene = next(s for s in bpy.data.scenes if s.name.startswith('00'))
source_objects = [o for o in scene.objects if o.type == 'MESH' and not o.get('is_lineart') and 'Studio ground' not in o.name]
work = bpy.data.scenes.new('_reduction_probe')
bpy.context.window.scene = work
records = []
total = 0
for obj in source_objects:
    src=obj.data
    parent=list(range(len(src.vertices)))
    def root(x):
        while parent[x]!=x:
            parent[x]=parent[parent[x]]
            x=parent[x]
        return x
    for e in src.edges:
        a,b=map(root,e.vertices)
        parent[b]=a
    groups={}
    for p in src.polygons:
        groups.setdefault(root(p.vertices[0]),[]).append(p)
    rows=[]
    for ci,polys in enumerate(groups.values()):
        ids=sorted({i for p in polys for i in p.vertices})
        lookup={j:i for i,j in enumerate(ids)}
        coords=[src.vertices[i].co.copy() for i in ids]
        faces=[tuple(lookup[i] for i in p.vertices) for p in polys]
        mesh=bpy.data.meshes.new('_part')
        mesh.from_pydata(coords,[],faces)
        tmp=bpy.data.objects.new('_part',mesh)
        work.collection.objects.link(tmp)
        tris=sum(len(p.vertices)-2 for p in mesh.polygons)
        extent=[max(v[a] for v in coords)-min(v[a] for v in coords) for a in range(3)]
        area=sum(p.area for p in mesh.polygons)
        md=tmp.modifiers.new('Reduce','DECIMATE')
        md.decimate_type='COLLAPSE'
        md.use_collapse_triangulate=True
        target=max(8,round(tris*0.04)//2*2)
        md.ratio=min(1,target/tris)
        bpy.context.view_layer.update()
        dg=bpy.context.evaluated_depsgraph_get()
        ev=tmp.evaluated_get(dg)
        em=ev.to_mesh()
        em.calc_loop_triangles()
        bvh=BVHTree.FromPolygons([v.co for v in em.vertices],[tuple(p.vertices) for p in em.polygons])
        distances=[bvh.find_nearest(v)[3] for v in coords]
        record={'object':obj.name,'component':ci,'source_triangles':tris,'new_triangles':len(em.loop_triangles),'extent':extent,'area':area,'max_error':max(distances),'rms_error':math.sqrt(sum(d*d for d in distances)/len(distances))}
        records.append(record)
        rows.append(record)
        total+=record['new_triangles']
        ev.to_mesh_clear()
        bpy.data.objects.remove(tmp,do_unlink=True)
        bpy.data.meshes.remove(mesh)
    print(obj.name,'components',len(rows),'triangles',sum(r['new_triangles'] for r in rows),'max_error',round(max(r['max_error'] for r in rows),6),flush=True)
(OUT/'probe-reduction.json').write_text(json.dumps({'body_triangles':total,'components':records},ensure_ascii=False,indent=2),encoding='utf-8')
print('PROBE_COMPLETE',total,flush=True)
