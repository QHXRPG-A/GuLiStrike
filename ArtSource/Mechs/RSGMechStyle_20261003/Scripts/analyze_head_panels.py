import bpy
import bmesh
import json
import math
from pathlib import Path
from mathutils import Vector
ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
parts=json.loads((ROOT/'Baseline/source_connected_parts.json').read_text(encoding='utf-8'))
part=next(p for p in parts if p['component']==28551)
source=bpy.data.objects['SK_FPS_Mech.001']
ids=part['vertex_ids']; index={vi:i for i,vi in enumerate(ids)}
data=bpy.data.meshes.new('AnalyzeHead')
data.from_pydata([source.matrix_world @ source.data.vertices[vi].co for vi in ids],[],
                 [[index[vi] for vi in source.data.polygons[pi].vertices] for pi in part['polygon_ids']])
bm=bmesh.new(); bm.from_mesh(data)
bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.000005)
bm.normal_update()
result=[]
for degrees in (10,15,20,25,30,40):
    visited=set(); groups=[]
    for seed in bm.faces:
        if seed in visited: continue
        stack=[seed]; patch=[]; visited.add(seed)
        while stack:
            face=stack.pop(); patch.append(face)
            for edge in face.edges:
                if len(edge.link_faces)!=2: continue
                if edge.calc_face_angle()>math.radians(degrees): continue
                for other in edge.link_faces:
                    if other not in visited:
                        visited.add(other); stack.append(other)
        if len(patch)<10: continue
        vertices={v for f in patch for v in f.verts}
        bounds=[[min(v.co[a] for v in vertices) for a in range(3)],
                [max(v.co[a] for v in vertices) for a in range(3)]]
        center=sum((f.calc_center_median() for f in patch),Vector())/len(patch)
        groups.append({'faces':len(patch),'center':list(center),'bounds':bounds,'area':sum(f.calc_area() for f in patch)})
    groups.sort(key=lambda p:p['faces'],reverse=True)
    result.append({'crease_degrees':degrees,'patches':groups})
print(json.dumps(result,indent=2),flush=True)
bm.free()
