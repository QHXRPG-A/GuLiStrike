import bpy,bmesh,json,math,numpy as np
from pathlib import Path
from collections import Counter
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT=Path(__file__).resolve().parent
PLAN=json.loads((ROOT.parent/'reduction_10k_v2'/'optimized-components.json').read_text(encoding='utf-8'))
scene=next(s for s in bpy.data.scenes if s.name.startswith('00'))
bpy.context.window.scene=scene;bpy.context.view_layer.update()
targets=[p for p in PLAN['parts'] if (p['object'].startswith('Leg_Front_L') and '深灰' in p['object'] and p['component'] in (1,2)) or (p['object'].startswith('Rear_Armor') and '蓝灰' in p['object'])]
report=[]
for p in targets:
    obj=scene.objects[p['object']];mesh=obj.data
    ids=p['source_vertex_ids'];lookup={v:i for i,v in enumerate(ids)}
    points=[obj.matrix_world@mesh.vertices[i].co for i in ids]
    faces=[tuple(lookup[i] for i in mesh.polygons[pi].vertices) for pi in p['source_polygon_ids']]
    row={'object':obj.name,'component':p['component'],'matrix':[list(r) for r in obj.matrix_world]}
    row['large_faces']=[{'area':float(f.area),'normal':list((obj.matrix_world.to_3x3()@f.normal).normalized()),'coords':[list(obj.matrix_world@mesh.vertices[i].co) for i in f.vertices]} for f in sorted([mesh.polygons[i] for i in p['source_polygon_ids']],key=lambda f:f.area,reverse=True)[:8]]
    row['selected']={'coords':[list(obj.matrix_world@Vector(v)) for v in p['selected']['coords']],'faces':p['selected']['faces']}
    matrix=np.asarray(points);eig,axes=np.linalg.eigh(np.cov((matrix-matrix.mean(axis=0)).T,bias=True))
    row['pca']={'eigen':eig.tolist(),'axes':axes.tolist()}
    bm=bmesh.new()
    for v in points:bm.verts.new(v)
    bm.verts.ensure_lookup_table()
    for f in faces:bm.faces.new([bm.verts[i] for i in f])
    bm.normal_update()
    row['source_boundary']=sum(e.is_boundary for e in bm.edges)
    row['dissolve']=[]
    for angle in (.05,3,6,10,15,20,30):
        probe=bm.copy()
        bmesh.ops.dissolve_limit(probe,angle_limit=math.radians(angle),verts=list(probe.verts),edges=list(probe.edges),delimit=set(),use_dissolve_boundaries=False)
        bmesh.ops.triangulate(probe,faces=list(probe.faces))
        probe.verts.index_update()
        bv=BVHTree.FromPolygons([v.co for v in probe.verts],[tuple(v.index for v in f.verts) for f in probe.faces],all_triangles=True)
        err=max(bv.find_nearest(v)[3] for v in points)
        row['dissolve'].append({'angle':angle,'triangles':len(probe.faces),'error':err})
        probe.free()
    bm.free()
    report.append(row)

def part_tree(p,low):
    o=scene.objects[p['object']]
    if low:return BVHTree.FromPolygons([o.matrix_world@Vector(v) for v in p['selected']['coords']],p['selected']['faces'],all_triangles=True)
    o.data.calc_loop_triangles();polys=set(p['source_polygon_ids'])
    return BVHTree.FromPolygons([o.matrix_world@v.co for v in o.data.vertices],[tuple(t.vertices) for t in o.data.loop_triangles if t.polygon_index in polys],all_triangles=True)

contacts=[]
for low in (False,True):
    p0=next(p for p in targets if p['object'].startswith('Leg_') and p['component']==1)
    p1=next(p for p in targets if p['object'].startswith('Leg_') and p['component']==2)
    p2=next(p for p in targets if p['object'].startswith('Rear_'))
    a,b=part_tree(p0,low),part_tree(p1,low)
    contacts.append({'model':'v2' if low else 'source','pair':'upper armor/outer plate','intersections':len(a.overlap(b))})
    blue=part_tree(p2,low)
    rear=next(p for p in PLAN['parts'] if p['object'].startswith('Rear_Armor') and '深灰' in p['object'] and p['component']==2)
    contacts.append({'model':'v2' if low else 'source','pair':'rear blue/dark','intersections':len(blue.overlap(part_tree(rear,low)))})
(ROOT/'target-diagnosis.json').write_text(json.dumps({'targets':report,'contacts':contacts},ensure_ascii=False,indent=2),encoding='utf-8')
for row in report:
    print(row['object'],row['component'],row['dissolve'],flush=True)
print('CONTACTS',contacts,flush=True)
