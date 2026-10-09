"""Background Blender: map approved B paint onto unchanged original triangle surfaces.

Alternative triangulation of an unchanged flat polygon is accepted only when
all interior samples lie on the same B surface and have the same paint role.
This script exports paint only. It never edits or saves a Blender mesh.
"""
import json,re,hashlib
from pathlib import Path
from collections import Counter
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(r'D:/UE5.7/test1')
OUT=ROOT/'ArtSource/ModelInterface_B_20261008'
DIR=OUT/'ResolvedMasks'
DIR.mkdir(exist_ok=True)
contract=json.loads((OUT/'region-contract.json').read_text(encoding='utf8'))
index=json.loads((OUT/'paint-geometry-index.json').read_text(encoding='utf8'))
report=[]
for entry in index:
    model=next(m for m in contract['models'] if m['name']==('BiZhiMao' if entry['model']=='BiZhiMaoConstruction' else entry['model']))
    by_lod={}
    for mask in model['meshes']:
        match=re.search('LOD([012])',mask['object'])
        lod=int(match.group(1)) if match else 0
        if entry['part']!='Root':
            suffix={'Body':'SM_RPF_Body','Door':'SK_RPF_Door','AccessRamp':'AccessRamp'}[entry['part']]
            if not mask['object'].endswith('_'+suffix):continue
        by_lod[lod]=mask
    for f in entry['files']:
        if not f['result'].get('success'):continue
        if entry['part']=='AccessRamp':continue
        geometry=json.loads((OUT/f['file']).read_text(encoding='utf8'))
        # Generated skeletal source LODs repeat LOD0. Source paint uses the
        # original LOD0; display paint uses its corresponding B display LOD.
        mask=by_lod.get(f['lod']) or (by_lod.get(0) if entry['model']=='ResourceFactory' else None)
        if not mask:continue
        source=json.loads((OUT/mask['mask']).read_text(encoding='utf8'))
        paints=np.asarray(source['triangles'],dtype=np.float64)
        sp=paints[:,:9].reshape(-1,3,3)
        target=np.asarray(geometry['triangles'],dtype=np.float64)
        tp=target[:,:9].reshape(-1,3,3)
        sl,sh=sp.min((0,1)),sp.max((0,1));tl,th=tp.min((0,1)),tp.max((0,1))
        axes=[-2,-1,3] if entry['model'].startswith('BiZhiMao') else [1,-2,3]
        # These axes follow the original UE FBX export/import frame; the
        # saved source hash and maximum surface error are recorded below.
        normalized=(sp-sl)/(sh-sl)
        aligned=normalized[:,:,[abs(a)-1 for a in axes]]
        for i,a in enumerate(axes):
            if a<0:aligned[:,:,i]=1-aligned[:,:,i]
        reference_low,reference_high=tl,th
        if entry['model']=='MissileTurret' and entry.get('nanite_source_preserved'):
            # The B preview was exported from the original fallback. Its
            # bounding box must not be stretched to the denser Nanite source.
            fallback=json.loads((OUT/'MissileTurret__Root__LOD0__resource__render.json').read_text(encoding='utf8'))
            reference=np.asarray(fallback['triangles'],dtype=np.float64)[:,:9].reshape(-1,3,3)/12.0
            reference_low,reference_high=reference.min((0,1)),reference.max((0,1))
        aligned=aligned*(reference_high-reference_low)+reference_low
        # Uniform scaling to unit-sized coordinates preserves surface distances.
        unit=float((th-tl).max())
        aligned=(aligned-tl)/unit
        points=[Vector(p) for p in aligned.reshape(-1,3)]
        tree=BVHTree.FromPolygons(points,[(i*3,i*3+1,i*3+2) for i in range(len(aligned))],all_triangles=True,epsilon=1e-7)
        from mathutils.kdtree import KDTree
        vertex_paints={}
        source_points=[]
        for n,tri in enumerate(aligned):
            for point in tri:
                key=tuple(np.round(point,7))
                vertex_paints.setdefault(key,set()).add(tuple(paints[n,9:13]))
        kd=KDTree(len(vertex_paints))
        for i,(point,values) in enumerate(vertex_paints.items()):source_points.append((point,values));kd.insert(Vector(point),i)
        kd.balance()
        triangle_paints={}
        def triangle_key(tri):return tuple(sorted(tuple(np.round(p,5)) for p in tri))
        for n,tri in enumerate(aligned):triangle_paints.setdefault(triangle_key(tri),set()).add(tuple(paints[n,9:13]))
        errors=[];rows=[];ids=[];distances=[];sample_role_conflicts=[];vertex_rows={};vertex_conflicts={}
        for i,(triangle,slot,tid,vis) in enumerate(zip(tp,geometry['triangle_slots'],geometry['triangle_ids'],geometry['vertex_ids'])):
            if slot in entry['protected_slots']:continue
            tri=(triangle-tl)/unit;center=tri.mean(0)
            samples=[center] if entry.get('nanite_source_preserved') else [center,*[center*.6+p*.4 for p in tri]]
            hits=[tree.find_nearest(Vector(p)) for p in samples]
            if any(h is None or h[2] is None for h in hits):errors.append(dict(triangle=int(tid),reason='surface missing'));continue
            d=max(h[3] for h in hits);distances.append(float(d))
            roles=[int(paints[h[2],9]) for h in hits]
            exact=triangle_paints.get(triangle_key(tri),set())
            points=[kd.find(Vector(p)) for p in tri]
            common=set.intersection(*[source_points[p[1]][1] for p in points]) if max(p[2] for p in points)<2e-5 else set()
            matched=exact if len(exact)==1 else common if len(common)==1 else set()
            if len(set(roles))!=1 and not matched:sample_role_conflicts.append(int(tid))
            normal=np.cross(tri[1]-tri[0],tri[2]-tri[0]);length=np.linalg.norm(normal)
            ndot=abs(float(np.dot(normal/length,np.array(hits[0][1])))) if length>1e-12 else 1.0
            # A true surface change is never silently accepted. Reduction-only
            # differences are reported separately for exact display preservation.
            if (d>2e-5 or (ndot<.999 and length>1e-12)) and not matched:errors.append(dict(triangle=int(tid),distance=float(d),normal_dot=ndot,roles=roles))
            color=list(next(iter(matched))) if matched else paints[hits[0][2],9:13].tolist()
            rows.append([*triangle.reshape(-1).tolist(),*color]);ids.append(int(tid))
            for vi,p in zip(vis,triangle):
                value=[int(vi),*p.tolist(),*color]
                if vi in vertex_rows and vertex_rows[vi][4:]!=value[4:]:vertex_conflicts[int(vi)]=[vertex_rows[vi][4],value[4]]
                else:vertex_rows[int(vi)]=value
        file=DIR/(entry['model']+'__'+entry['part']+'__LOD'+str(f['lod'])+('__render' if f['rendered'] else '__source')+'.json')
        payload=dict(model=entry['model'],part=entry['part'],lod=f['lod'],rendered=f['rendered'],ue_axes=[1,2,3],
            triangles=rows,triangle_ids=ids,render_vertex_colors=list(vertex_rows.values()),
            original_geometry_file=f['file'],original_geometry_sha256=hashlib.sha256((OUT/f['file']).read_bytes()).hexdigest(),
            approved_mask=mask['mask'],approved_mask_sha256=mask['mask_sha256'])
        file.write_text(json.dumps(payload,separators=(',',':')),encoding='utf8')
        report.append(dict(model=entry['model'],part=entry['part'],lod=f['lod'],rendered=f['rendered'],file=str(file.relative_to(OUT)),
            axes=axes,triangles=len(rows),surface_errors=len(errors),surface_error_examples=errors[:12],max_surface_distance=max(distances,default=0),
            sample_role_conflicts=len(sample_role_conflicts),role_conflict_examples=sample_role_conflicts[:12],render_vertex_conflicts=len(vertex_conflicts),
            render_vertex_conflict_examples=list(vertex_conflicts.items())[:12],roles=dict(Counter(r[9] for r in rows))))
(OUT/'surface-paint-correspondence.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'surfaces':len(report),'unmatched_surfaces':sum(r['surface_errors'] for r in report),'report':str(OUT/'surface-paint-correspondence.json')}))
