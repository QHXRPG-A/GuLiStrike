import bpy,json,math
from pathlib import Path
from mathutils.kdtree import KDTree
from collections import Counter
O=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004/UE_Delivery_v1')
def read(f):
    bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(f),use_anim=False)
    meshes=[o for o in bpy.data.objects if o.type=='MESH'];ob=max(meshes,key=lambda o:len(o.data.polygons));norm=ob.matrix_world.to_3x3().inverted().transposed()
    rows=[]
    for p in ob.data.polygons:
        if 'Outline' in ob.data.materials[p.material_index].name:continue
        for l in p.loop_indices:
            rows.append((ob.matrix_world@ob.data.vertices[ob.data.loops[l].vertex_index].co,(norm@ob.data.corner_normals[l].vector).normalized()))
    return rows
source=read(O/'FBX/SKM_ControlRigMech_B_v4_LOD0.fbx');target=read(O/'FBX/UE_Readback_AllLODs.fbx')
tree=KDTree(len(source))
for i,(p,n) in enumerate(source):tree.insert(p,i)
tree.balance();angles=[];errors=[]
for p,n in target:
    hits=tree.find_n(p,16);near=[i for q,i,d in hits if d<.00002]
    assert near,(p,hits[0])
    angles.append(min(math.degrees(math.acos(max(-1.,min(1.,n.dot(source[i][1]))))) for i in near));errors.append(hits[0][2])
angles.sort();data={'source_corner_normals':len(source),'native_corner_normals':len(target),'max_nearest_vertex_error_m':max(errors),'normal_direction_min_matching_corner_max_angle_degrees':max(angles),'P99_angle_degrees':angles[int(len(angles)*.99)],'over_1_degree':sum(a>1 for a in angles)}
(O/'native_normal_comparison.json').write_text(json.dumps(data,indent=2),encoding='utf-8');print(json.dumps(data),flush=True)
