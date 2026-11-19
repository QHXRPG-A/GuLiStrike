"""Budgeted per-component QEM; no renders or image inspection."""
import bpy
import bmesh
import json
import math
import hashlib
import sys
from pathlib import Path
from mathutils.bvhtree import BVHTree
from mathutils import Matrix, Vector

OUT=Path(__file__).resolve().parent
sys.path.insert(0,str(OUT))
from regular_geometry import fit_round,build_round,rules
SOURCE=Path(bpy.data.filepath)
STATIC=next(s for s in bpy.data.scenes if s.name.startswith('00'))
bpy.context.window.scene=STATIC
bpy.context.view_layer.update()
OBJECTS=[o for o in STATIC.objects if o.type=='MESH' and not o.get('is_lineart') and 'Studio ground' not in o.name]
WORK=bpy.data.scenes.new('_reduction_work')
bpy.context.window.scene=WORK
PARTS=[]
BUDGET=9868
MAX_SURFACE_ERROR=0.035
TARGETS=[8,12,16,20,24,28,32,36,40,48,56,64,80,96,128,160,224,320,448,640,896]

def connected(mesh):
    parents=list(range(len(mesh.vertices)))
    def root(x):
        while parents[x]!=x:
            parents[x]=parents[parents[x]]
            x=parents[x]
        return x
    for e in mesh.edges:
        a,b=map(root,e.vertices)
        parents[b]=a
    groups={}
    for p in mesh.polygons:
        groups.setdefault(root(p.vertices[0]),[]).append(p)
    return list(groups.values())

def signed_volume(coords,faces):
    return sum(coords[p[0]].dot(coords[p[1]].cross(coords[p[2]]))/6 for p in faces)

for obj in OBJECTS:
    src=obj.data
    src.calc_loop_triangles()
    has_outline=any(o.get('is_lineart') and o.parent==obj for o in STATIC.objects)
    for ci,polys in enumerate(connected(src)):
        ids=sorted({i for p in polys for i in p.vertices})
        lookup={j:i for i,j in enumerate(ids)}
        coords=[src.vertices[i].co.copy() for i in ids]
        poly_indices={p.index for p in polys}
        faces=[tuple(lookup[i] for i in t.vertices) for t in src.loop_triangles if t.polygon_index in poly_indices]
        mesh=bpy.data.meshes.new('_part')
        mesh.from_pydata(coords,[],faces)
        mesh.calc_loop_triangles()
        tmp=bpy.data.objects.new('_part',mesh)
        WORK.collection.objects.link(tmp)
        tris=len(mesh.loop_triangles)
        src_triangles=[tuple(t.vertices) for t in mesh.loop_triangles]
        src_bvh=BVHTree.FromPolygons(coords,src_triangles,all_triangles=True)
        src_volume=abs(signed_volume(coords,src_triangles))
        extent=[max(v[a] for v in coords)-min(v[a] for v in coords) for a in range(3)]
        fit=fit_round(obj,ids)
        world=[obj.matrix_world@v for v in coords]
        world_extent=[max(v[a] for v in world)-min(v[a] for v in world) for a in range(3)]
        constraints=rules(obj,ci,fit,world_extent,None)
        area=sum(p.area for p in mesh.polygons)
        samples=coords+[(coords[p[0]]+coords[p[1]]+coords[p[2]])/3 for p in src_triangles]
        md=tmp.modifiers.new('Reduce','DECIMATE')
        md.decimate_type='COLLAPSE'
        md.use_collapse_triangulate=True
        candidates=[]
        seen=set()
        targets=([constraints['round_segments']] if constraints['round_segments'] else sorted(set(min(tris,t) for t in TARGETS+[tris] if t>=constraints['minimum'])))
        for target in targets:
            md.ratio=min(1,(target+0.75)/tris)
            bpy.context.view_layer.update()
            dg=bpy.context.evaluated_depsgraph_get()
            ev=tmp.evaluated_get(dg)
            em=ev.to_mesh()
            clean=bmesh.new()
            clean.from_mesh(em)
            bmesh.ops.remove_doubles(clean,verts=list(clean.verts),dist=1e-7)
            bmesh.ops.dissolve_degenerate(clean,dist=1e-8,edges=list(clean.edges))
            wires=[e for e in clean.edges if e.is_wire]
            if wires:bmesh.ops.delete(clean,geom=wires,context='EDGES')
            loose=[v for v in clean.verts if not v.link_faces]
            if loose:bmesh.ops.delete(clean,geom=loose,context='VERTS')
            bmesh.ops.triangulate(clean,faces=list(clean.faces))
            clean.verts.ensure_lookup_table()
            clean.verts.index_update()
            n=len(clean.faces)
            if n in seen or n<4:
                clean.free()
                ev.to_mesh_clear()
                continue
            seen.add(n)
            if any(f.calc_area()<1e-12 for f in clean.faces) or any(len(e.link_faces)>2 for e in clean.edges):
                clean.free()
                ev.to_mesh_clear()
                continue
            new_coords=[v.co.copy() for v in clean.verts]
            new_faces=[tuple(v.index for v in f.verts) for f in clean.faces]
            clean.free()
            analytic_normals=None
            if constraints['round_segments']:
                new_coords,new_faces,analytic_normals=build_round(obj,fit,target)
                n=len(new_faces)
            bvh=BVHTree.FromPolygons(new_coords,new_faces,all_triangles=True)
            dists=[bvh.find_nearest(p)[3] for p in samples]
            new_samples=new_coords+[(new_coords[f[0]]+new_coords[f[1]]+new_coords[f[2]])/3 for f in new_faces]
            reverse=[src_bvh.find_nearest(p)[3] for p in new_samples]
            max_error=max(max(dists),max(reverse))
            rms=math.sqrt(sum(x*x for x in dists)/len(dists))
            vol=abs(signed_volume(new_coords,new_faces))
            ratio=vol/max(src_volume,1e-15)
            # Exclude nearly flattened/inside-out parts; keep every mechanical component.
            if ratio<0.30 or ratio>2.0 or (max_error>constraints['error'] and not constraints['round_segments']):
                ev.to_mesh_clear()
                continue
            # Large exposed surfaces and the worst local silhouette displacement both matter.
            loss=area*(0.65*max_error**2+0.35*rms**2)
            candidate={'triangles':n,'max_error':max_error,'rms_error':rms,'volume_ratio':ratio,'loss':loss,'coords':[list(v) for v in new_coords],'faces':new_faces}
            if analytic_normals is not None:candidate.update(analytic_normals=[list(n) for n in analytic_normals],primitive=fit['kind'])
            candidates.append(candidate)
            ev.to_mesh_clear()
        candidates.sort(key=lambda c:c['triangles'])
        # Keep the Pareto frontier: an added triangle must improve the measured approximation.
        frontier=[]
        for c in candidates:
            if not frontier or c['loss']<frontier[-1]['loss']-1e-15:
                frontier.append(c)
        assert frontier,(obj.name,ci,constraints)
        PARTS.append({'object':obj.name,'component':ci,'source_vertex_ids':ids,'source_polygon_ids':[p.index for p in polys],'source_triangles':tris,'source_area':area,'extent':extent,'cost_multiplier':2 if has_outline and constraints['outline'] else 1,'outline':bool(has_outline and constraints['outline']),'constraints':constraints,'round_fit':fit,'candidates':frontier,'choice':0})
        bpy.data.objects.remove(tmp,do_unlink=True)
        bpy.data.meshes.remove(mesh)
    print('CANDIDATES',obj.name,flush=True)

by_name={o.name:o for o in OBJECTS}
symmetry_pairs=[]
paired=set()
groups=[]
for pi,p in enumerate(PARTS):
    if '_L' not in p['object']:
        continue
    right_name=p['object'].replace('_L','_R')
    if right_name not in by_name:
        continue
    left=by_name[p['object']]
    right=by_name[right_name]
    transform=right.matrix_world.inverted() @ Matrix.Diagonal((-1,1,1,1)) @ left.matrix_world
    points=[transform @ left.data.vertices[i].co for i in p['source_vertex_ids']]
    center=sum(points,Vector())/len(points)
    matches=[]
    for qi,q in enumerate(PARTS):
        if q['object']!=right_name or len(q['source_vertex_ids'])!=len(points):
            continue
        right_points=[right.data.vertices[i].co for i in q['source_vertex_ids']]
        other=sum(right_points,Vector())/len(right_points)
        matches.append(((center-other).length,qi))
    if not matches:
        continue
    distance,qi=min(matches)
    if distance>0.00005:
        continue
    q=PARTS[qi]
    right_faces=[tuple(right.data.polygons[i].vertices) for i in q['source_polygon_ids']]
    right_bvh=BVHTree.FromPolygons([v.co for v in right.data.vertices],right_faces)
    symmetry_error=max(right_bvh.find_nearest(v)[3] for v in points)
    if symmetry_error>0.00005:
        continue
    q['candidates']=[{**c,'coords':[list(transform @ Vector(v)) for v in c['coords']],'faces':[tuple(reversed(f)) for f in c['faces']]} for c in p['candidates']]
    for c,original in zip(q['candidates'],p['candidates']):
        if 'analytic_normals' in original:c['analytic_normals']=[list((transform.to_3x3() @ Vector(n)).normalized()) for n in original['analytic_normals']]
    symmetry_pairs.append({'left':pi,'right':qi,'source_max_error':symmetry_error})
    paired.update((pi,qi))
    groups.append([pi,qi])
groups.extend([i] for i in range(len(PARTS)) if i not in paired)

total=sum(p['candidates'][0]['triangles']*p['cost_multiplier'] for p in PARTS)
print('MINIMUM_BUDGET',total,'body',sum(p['candidates'][0]['triangles'] for p in PARTS),'outline_parts',sum(p['outline'] for p in PARTS),flush=True)
(OUT/'minimum-budget.json').write_text(json.dumps([{'object':p['object'],'component':p['component'],'faces':p['candidates'][0]['triangles'],'cost':p['candidates'][0]['triangles']*p['cost_multiplier'],'constraints':p['constraints']} for p in PARTS],ensure_ascii=False,indent=2),encoding='utf-8')
assert total<=BUDGET,(total,BUDGET)
while True:
    best=None
    for group in groups:
        pi=group[0]
        p=PARTS[pi]
        cur=p['candidates'][p['choice']]
        for ni,nxt in enumerate(p['candidates'][p['choice']+1:],p['choice']+1):
            cost=sum((PARTS[i]['candidates'][ni]['triangles']-PARTS[i]['candidates'][PARTS[i]['choice']]['triangles'])*PARTS[i]['cost_multiplier'] for i in group)
            if total+cost>BUDGET:
                continue
            gain=sum((PARTS[i]['candidates'][PARTS[i]['choice']]['loss']-PARTS[i]['candidates'][ni]['loss']) for i in group)/cost
            if best is None or gain>best[0]:
                best=(gain,group,ni,cost)
    if best is None:
        break
    _,group,ni,cost=best
    for pi in group:
        PARTS[pi]['choice']=ni
    total+=cost

output=[]
for p in PARTS:
    selected=p['candidates'][p['choice']]
    p['candidate_metrics']=[{k:v for k,v in c.items() if k not in {'coords','faces'}} for c in p.pop('candidates')]
    p['selected']=selected
    output.append(p)
record={'source':str(SOURCE),'source_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),'budget_including_outline':BUDGET,'selected_triangles_including_outline':total,'component_count':len(PARTS),'symmetry_pairs':symmetry_pairs,'parts':output}
(OUT/'optimized-components.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
worst=sorted(output,key=lambda p:p['selected']['max_error'],reverse=True)[:12]
print('OPTIMIZATION_COMPLETE',total,'body',sum(p['selected']['triangles'] for p in output),'max_error',worst[0]['selected']['max_error'],flush=True)
for p in worst:
    print('WORST',p['object'],p['component'],p['selected']['triangles'],p['selected']['max_error'],p['extent'],flush=True)
