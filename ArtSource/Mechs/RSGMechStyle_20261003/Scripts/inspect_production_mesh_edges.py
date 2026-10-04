import bpy,json
from collections import Counter,defaultdict
mesh=bpy.data.objects['RSGMech_LOD0_Body'].data
edgefaces=defaultdict(list)
for face in mesh.polygons:
    v=list(face.vertices)
    for i in range(len(v)):
        edgefaces[tuple(sorted((v[i],v[(i+1)%len(v)])))].append(face.index)
mesh.calc_loop_triangles()
print(json.dumps({'vertices':len(mesh.vertices),'triangles':len(mesh.loop_triangles),
                  'faces_per_edge':dict(Counter(len(v) for v in edgefaces.values())),
                  'material_faces':dict(Counter(f.material_index for f in mesh.polygons))}),flush=True)
