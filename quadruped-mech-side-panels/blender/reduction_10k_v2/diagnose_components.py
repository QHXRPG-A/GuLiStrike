import bpy,bmesh,json,math,numpy as np
from pathlib import Path
from mathutils.bvhtree import BVHTree

ROOT=Path(__file__).resolve().parent
PLAN=json.loads((ROOT.parent/'reduction_10k'/'optimized-components.json').read_text(encoding='utf-8'))
s=next(s for s in bpy.data.scenes if s.name.startswith('00'))
bpy.context.window.scene=s
bpy.context.view_layer.update()
rows=[]
for part in PLAN['parts']:
    obj=s.objects[part['object']]
    ids=part['source_vertex_ids']
    points=np.array([obj.matrix_world @ obj.data.vertices[i].co for i in ids])
    center=points.mean(axis=0)
    eig,axes=np.linalg.eigh(np.cov((points-center).T,bias=True))
    q=(points-center) @ axes
    radial=np.linalg.norm(q[:,1:],axis=1)
    row={'object':obj.name,'component':part['component'],'source_vertices':len(ids),'old_triangles':part['selected']['triangles'],'center':center.tolist(),'min':points.min(axis=0).tolist(),'max':points.max(axis=0).tolist(),'pca_eigenvalues':eig.tolist(),'axis':axes[:,0].tolist(),'axial_extent':float(np.ptp(q[:,0])),'radial_range':[float(radial.min()),float(radial.max())]}
    thin_round=eig[0]<eig[1]*.2 and abs(eig[2]-eig[1])<eig[2]*.01 and radial.min()>radial.max()*.5
    row['thin_round_ring']=bool(thin_round)
    if thin_round:
        R=(radial.max()+radial.min())*.5
        minor=np.sqrt((radial-R)**2+q[:,0]**2)
        row['major_radius']=float(R)
        row['minor_radius_mean']=float(minor.mean())
        row['minor_radius_spread']=float(np.ptp(minor))
    if obj.name.startswith(('Side_Panel','Front_Armor','Dorsal_Armor','Rear_Armor')) or (obj.name.startswith('Body_Chassis') and '黑色' in obj.name):
        lookup={j:i for i,j in enumerate(ids)}
        mesh=bpy.data.meshes.new('_component')
        mesh.from_pydata([obj.data.vertices[i].co for i in ids],[],[tuple(lookup[v] for v in obj.data.polygons[pi].vertices) for pi in part['source_polygon_ids']])
        bvh=BVHTree.FromPolygons([v.co for v in mesh.vertices],[tuple(p.vertices) for p in mesh.polygons])
        variants=[]
        for angle in (0.05,5,10,15,20,25,30,40):
            bm=bmesh.new();bm.from_mesh(mesh)
            bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-7)
            bmesh.ops.dissolve_degenerate(bm,dist=1e-8,edges=list(bm.edges))
            bmesh.ops.dissolve_limit(bm,angle_limit=math.radians(angle),verts=list(bm.verts),edges=list(bm.edges),delimit=set(),use_dissolve_boundaries=False)
            bmesh.ops.triangulate(bm,faces=list(bm.faces))
            bm.verts.index_update()
            coords=[v.co.copy() for v in bm.verts]
            faces=[tuple(v.index for v in f.verts) for f in bm.faces]
            low_bvh=BVHTree.FromPolygons(coords,faces,all_triangles=True)
            err=max(low_bvh.find_nearest(v.co)[3] for v in mesh.vertices)
            variants.append({'angle':angle,'triangles':len(faces),'sample_error':err})
            bm.free()
        row['planar_variants']=variants
        bpy.data.meshes.remove(mesh)
    rows.append(row)
(ROOT/'component-diagnosis.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
for r in rows:
    if r['thin_round_ring'] or (r['object'].startswith(('Side_Panel','Front_Armor','Dorsal_Armor','Rear_Armor')) and ('蓝灰' in r['object'] or 'Side_Panel' in r['object'])):
        print(json.dumps(r,ensure_ascii=False),flush=True)
print('DIAGNOSIS_COMPLETE',len(rows),flush=True)
