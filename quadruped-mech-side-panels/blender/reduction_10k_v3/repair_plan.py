"""Repair overlapping armor as closed rigid shells, then budget hidden hull faces."""
import bpy,bmesh,json,math,numpy as np
from pathlib import Path
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree

ROOT=Path(__file__).resolve().parent
PLAN=json.loads((ROOT.parent/'reduction_10k_v2'/'optimized-components.json').read_text(encoding='utf-8'))
PARTS=PLAN['parts']
STATIC=next(s for s in bpy.data.scenes if s.name.startswith('00'))
bpy.context.window.scene=STATIC;bpy.context.view_layer.update()
MATRICES={o.name:o.matrix_world.copy() for o in STATIC.objects}
WORK=bpy.data.scenes.new('_targeted_repair_work');bpy.context.window.scene=WORK
REPORT={'upper_shells':[],'rear_badge':{},'hidden_outline_faces_removed':0,'new_team_light_triangle_budget':160,'internal_duplicate_outlines_removed':0}

def make_object(name,coords,faces):
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(coords,[],faces);mesh.update()
    obj=bpy.data.objects.new(name,mesh);WORK.collection.objects.link(obj)
    return obj

def cleaned(mesh):
    bm=bmesh.new();bm.from_mesh(mesh)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=2e-6)
    bmesh.ops.dissolve_degenerate(bm,dist=1e-7,edges=list(bm.edges))
    loose=[v for v in bm.verts if not v.link_faces]
    if loose:bmesh.ops.delete(bm,geom=loose,context='VERTS')
    bm.normal_update()
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bmesh.ops.dissolve_limit(bm,angle_limit=math.radians(.05),verts=list(bm.verts),edges=list(bm.edges),delimit=set())
    bmesh.ops.triangulate(bm,faces=list(bm.faces))
    bm.verts.index_update()
    coords=[v.co.copy() for v in bm.verts];faces=[tuple(v.index for v in f.verts) for f in bm.faces]
    valid=all(e.is_manifold for e in bm.edges) and all(f.calc_area()>1e-11 for f in bm.faces)
    if not valid:print('CLEAN_ISSUE',len(bm.faces),'nonmanifold',sum(not e.is_manifold for e in bm.edges),'tiny',sum(f.calc_area()<=1e-11 for f in bm.faces),flush=True)
    bm.free();return coords,faces,valid

def source_geometry(p):
    obj=STATIC.objects[p['object']];mesh=obj.data;mesh.calc_loop_triangles()
    ids=p['source_vertex_ids'];lookup={v:i for i,v in enumerate(ids)};polys=set(p['source_polygon_ids'])
    return [obj.matrix_world@mesh.vertices[i].co for i in ids],[tuple(lookup[v] for v in t.vertices) for t in mesh.loop_triangles if t.polygon_index in polys]

def reduce_union(a,b):
    ca,fa=source_geometry(a);cb,fb=source_geometry(b)
    first=make_object('_armor',ca,fa);second=make_object('_side_guard',cb,fb)
    md=first.modifiers.new('Join closed armor','BOOLEAN');md.operation='UNION';md.solver='EXACT';md.object=second
    direction=(sum(ca,Vector())/len(ca)-sum(cb,Vector())/len(cb)).normalized()
    for adjustment in (0.,.001,.003,.005):
        second.location=direction*adjustment
        bpy.context.view_layer.update();ev=first.evaluated_get(bpy.context.evaluated_depsgraph_get());mesh=ev.to_mesh()
        points,faces,valid=cleaned(mesh);ev.to_mesh_clear()
        if valid:break
    assert valid,(a['object'],'union not closed')
    bpy.data.objects.remove(first,do_unlink=True);bpy.data.objects.remove(second,do_unlink=True)
    source_tree=BVHTree.FromPolygons(points,faces,all_triangles=True)
    samples=points+[(points[f[0]]+points[f[1]]+points[f[2]])/3 for f in faces]
    joined=make_object('_joined_armor',points,faces)
    dec=joined.modifiers.new('Reduce continuous armor','DECIMATE');dec.use_collapse_triangulate=True
    alternatives=[]
    for target in (64,72,80,88,96,104,112,128,144,160):
        dec.ratio=min(1,(target+.75)/len(faces));bpy.context.view_layer.update()
        ev=joined.evaluated_get(bpy.context.evaluated_depsgraph_get());em=ev.to_mesh()
        c,f,ok=cleaned(em);ev.to_mesh_clear()
        if not ok:continue
        bv=BVHTree.FromPolygons(c,f,all_triangles=True)
        distances=[bv.find_nearest(v)[3] for v in samples]
        reverse=[source_tree.find_nearest(v)[3] for v in c]+[source_tree.find_nearest((c[t[0]]+c[t[1]]+c[t[2]])/3)[3] for t in f]
        error=max(distances+reverse)
        alternatives.append({'triangles':len(f),'max_error':error})
        if error<.007:
            result={'coords':[list(v) for v in c],'faces':f,'triangles':len(f),'max_error':error,'rms_error':float(np.sqrt(np.mean(np.asarray(distances)**2))),'volume_ratio':1.,'loss':0.}
            break
    else:raise RuntimeError((a['object'],alternatives))
    bpy.data.objects.remove(joined,do_unlink=True)
    return result,alternatives

for leg in ('Leg_Front_L','Leg_Rear_L'):
    a=next(p for p in PARTS if p['object'].startswith(leg) and '深灰' in p['object'] and p['component']==1)
    b=next(p for p in PARTS if p['object']==a['object'] and p['component']==2)
    selected,alternatives=reduce_union(a,b)
    for name,mirror in ((a['object'],False),(a['object'].replace('_L','_R'),True)):
        main=next(p for p in PARTS if p['object']==name and p['component']==1)
        guard=next(p for p in PARTS if p['object']==name and p['component']==2)
        main['source_groups']=[{k:main[k] for k in ('source_vertex_ids','source_polygon_ids','component')},{k:guard[k] for k in ('source_vertex_ids','source_polygon_ids','component')}]
        main['source_vertex_ids']=sorted(main['source_vertex_ids']+guard['source_vertex_ids'])
        main['source_polygon_ids']=sorted(main['source_polygon_ids']+guard['source_polygon_ids'])
        main['source_triangles']+=guard['source_triangles']
        transform=MATRICES[name].inverted() @ (Matrix.Diagonal((-1,1,1,1)) if mirror else Matrix.Identity(4))
        main['selected']={**selected,'coords':[list(transform@Vector(v)) for v in selected['coords']],'faces':[tuple(reversed(f)) if mirror else f for f in selected['faces']]}
        main['constraints']['rigid_armor']=True;main['constraints']['motion_group']='upper'
        main['repair']='Union of upper armor and side guard; one closed rigid shell'
        PARTS.remove(guard)
        REPORT['upper_shells'].append({'object':name,'merged_original_components':[1,2],'triangles':selected['triangles'],'maximum_surface_sample_error_m':selected['max_error'],'alternatives':alternatives})
        print('ARMOR_REPAIRED',name,selected['triangles'],selected['max_error'],flush=True)

# Replace the partly buried rear colored detail by one centered, symmetric crown plate.
blue=next(p for p in PARTS if p['object'].startswith('Rear_Armor') and '蓝灰' in p['object'])
dark=next(p for p in PARTS if p['object'].startswith('Rear_Armor') and '深灰' in p['object'] and p['component']==2)
dark_points=[MATRICES[dark['object']]@Vector(v) for v in dark['selected']['coords']]
dark_tree=BVHTree.FromPolygons(dark_points,dark['selected']['faces'],all_triangles=True)
rows=[(1.54,.28),(1.57,.30),(1.78,.27),(1.89,.17)]
fit_points=[]
for y,w in rows:
    for x in np.linspace(-w,w,21):
        p,n,idx,d=dark_tree.ray_cast(Vector((float(x),y,4)),Vector((0,0,-1)))
        assert p is not None
        fit_points.append((float(x),y,p.z))
design=np.array([[1,y-1.72,abs(x)] for x,y,z in fit_points]);height=np.array([z for x,y,z in fit_points])
coeff=np.linalg.lstsq(design,height,rcond=None)[0]
top=[]
for y,w in rows:
    for x in (-w,0.,w):top.append(Vector((x,y,float(np.dot((1,y-1.72,abs(x)),coeff)))))
top_faces=[]
for j in range(3):
    # Diagonals mirror about X=0.
    a=j*3;b=(j+1)*3
    top_faces.extend(((a,a+1,b+1),(a,b+1,b),(a+1,a+2,b+1),(a+2,b+2,b+1)))
top_tree=BVHTree.FromPolygons(top,top_faces,all_triangles=True)
max_deficit=-1e9
for y in np.linspace(rows[0][0],rows[-1][0],101):
    for j in range(3):
        if rows[j][0]-1e-6<=y<=rows[j+1][0]+1e-6:
            t=(y-rows[j][0])/(rows[j+1][0]-rows[j][0]);width=rows[j][1]*(1-t)+rows[j+1][1]*t;break
    for x in np.linspace(-width*.999,width*.999,61):
        origin=Vector((float(x),float(y),4));direction=Vector((0,0,-1))
        under=dark_tree.ray_cast(origin,direction)[0];above=top_tree.ray_cast(origin,direction)[0]
        if under is not None and above is not None:max_deficit=max(max_deficit,under.z-above.z)
raise_by=max_deficit+.012
for v in top:v.z+=raise_by
points=top+[v-Vector((0,0,.075)) for v in top]
faces=top_faces+[tuple(i+12 for i in reversed(f)) for f in top_faces]
boundary=[0,1,2,5,8,11,10,9,6,3]
for a,b in zip(boundary,boundary[1:]+boundary[:1]):faces.extend(((a,b+12,b),(a,a+12,b+12)))
badge_obj=make_object('_regular_rear_badge',points,faces)
points,faces,valid=cleaned(badge_obj.data);assert valid
bpy.data.objects.remove(badge_obj,do_unlink=True)
old_blue_tree=BVHTree.FromPolygons(*source_geometry(blue),all_triangles=True)
new_badge_tree=BVHTree.FromPolygons(points,faces,all_triangles=True)
errors=[old_blue_tree.find_nearest(v)[3] for v in points]
inverse=MATRICES[blue['object']].inverted()
blue['selected']={'coords':[list(inverse@v) for v in points],'faces':faces,'triangles':len(faces),'max_error':max(errors),'rms_error':float(np.sqrt(np.mean(np.asarray(errors)**2))),'volume_ratio':1.,'loss':0.,'flat_normals':True,'freestyle_exempt':True}
blue['repair']='Symmetric rear crown inset; positive top clearance and embedded base'
REPORT['rear_badge']={'triangles':len(faces),'width_m':.60,'length_m':.35,'minimum_sampled_top_clearance_m':.012,'symmetric_x':True,'crown_coefficients':coeff.tolist(),'height_offset_m':raise_by,'base_depth_m':.075}
print('REAR_BADGE',REPORT['rear_badge'],flush=True)

# Remove only outline triangles wholly buried at least 15 mm inside another convex
# body component. Convex half-space containment covers the entire triangle, not just
# a center ray, so exterior silhouette faces stay in the outline.
occluders=[]
world_parts=[]
for p in PARTS:
    points=np.array([MATRICES[p['object']]@Vector(v) for v in p['selected']['coords']],dtype=np.float64)
    triangles=np.array(p['selected']['faces'],dtype=np.int32)
    surface=points[triangles]
    normals=np.cross(surface[:,1]-surface[:,0],surface[:,2]-surface[:,0]);length=np.linalg.norm(normals,axis=1)
    normals/=np.maximum(length[:,None],1e-20);dist=np.einsum('ij,ij->i',normals,surface[:,0])
    world_parts.append((p,points,triangles))
    edges={}
    for f in triangles:
        for a,b in zip(f,np.roll(f,-1)):
            e=tuple(sorted((int(a),int(b))));edges[e]=edges.get(e,0)+1
    closed=all(n==2 for n in edges.values())
    convex=closed and np.max(points@normals.T-dist[None,:])<2e-5
    if convex:occluders.append((p,points.min(axis=0),points.max(axis=0),normals,dist))
for p,points,triangles in world_parts:
    mask=np.full(len(triangles),bool(p['outline']))
    if p['outline']:
        for other,lo,hi,n,d in occluders:
            if other is p:continue
            inside=np.all(points>=lo+.015,axis=1)&np.all(points<=hi-.015,axis=1)
            if not np.any(inside):continue
            indices=np.flatnonzero(inside)
            inside[indices]=np.all(points[indices]@n.T-d[None,:]<-.015,axis=1)
            mask[np.all(inside[triangles],axis=1)]=False
    p['selected']['outline_faces']=mask.tolist()
    REPORT['hidden_outline_faces_removed']+=len(mask)-int(np.count_nonzero(mask)) if p['outline'] else 0

# Keep the exterior armor hulls. Small internal support bars and leg frame struts
# retain their shaded mesh and Freestyle edges without a second geometric shell.
for p in PARTS:
    internal=(p['object'].startswith('Body_Chassis') and '关节枪铁' in p['object']) or (p['object'].startswith('Leg_') and (('黑色' in p['object'] and p['component'] in (2,3,4)) or ('关节枪铁' in p['object'] and p['component'] in (8,9))))
    if internal:
        REPORT['internal_duplicate_outlines_removed']+=sum(p['selected']['outline_faces'])
        p['selected']['outline_faces']=[False]*p['selected']['triangles']

body=sum(p['selected']['triangles'] for p in PARTS)
outline=sum(sum(p['selected']['outline_faces']) for p in PARTS)
REPORT['base_triangles']={'body':body,'outline':outline,'sum':body+outline}
REPORT['including_lights_and_laser_and_ground']=body+outline+160+120+12
print('REPAIR_BUDGET',REPORT['base_triangles'],'hidden',REPORT['hidden_outline_faces_removed'],'with_lights_laser_ground',REPORT['including_lights_and_laser_and_ground'],flush=True)
PLAN['component_count']=len(PARTS);PLAN['selected_triangles_including_outline']=body+outline
PLAN['repair_report']=REPORT
PLAN['symmetry_pairs']=[{'left':i,'right':next(j for j,q in enumerate(PARTS) if q['object']==p['object'].replace('_L','_R') and q['component']==p['component'])} for i,p in enumerate(PARTS) if '_L' in p['object'] and any(q['object']==p['object'].replace('_L','_R') and q['component']==p['component'] for q in PARTS)]
(ROOT/'optimized-components.json').write_text(json.dumps(PLAN,ensure_ascii=False,indent=2),encoding='utf-8')
(ROOT/'repair-plan-report.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
assert REPORT['including_lights_and_laser_and_ground']<=10000,REPORT['including_lights_and_laser_and_ground']
