"""Measure conservative planar/segment cleanup on copied geometry, never alter references."""
import bpy, bmesh, json, math
from pathlib import Path

R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004')
O=R/'Production_B_v1'
bpy.ops.wm.open_mainfile(filepath=str(R/'References_A_v2/ControlRigMech_A_v2_ReferenceStudy.blend'))
src=next(o for o in bpy.context.scene.objects if o.type=='MESH')
data=src.data.copy()
data.transform(src.matrix_world)
base=bmesh.new(); base.from_mesh(data)
transparent={i for i,m in enumerate(data.materials) if 'RemoveOriginal' in m.name}
bmesh.ops.delete(base, geom=[f for f in base.faces if f.material_index in transparent], context='FACES')
bmesh.ops.remove_doubles(base, verts=list(base.verts), dist=.000002)
base.normal_update()
result=[]
for angle in [0.005,0.015,0.03,0.06,0.1,0.15,0.22,0.32,0.52]:
    bm=base.copy()
    bmesh.ops.dissolve_limit(bm,angle_limit=angle,verts=list(bm.verts),edges=list(bm.edges),
                            use_dissolve_boundaries=True, delimit={'MATERIAL'})
    mesh=bpy.data.meshes.new('probe'); bm.to_mesh(mesh); mesh.calc_loop_triangles()
    entry={'angle_rad':angle,'angle_deg':math.degrees(angle),'vertices':len(mesh.vertices),
           'polygons':len(mesh.polygons),'triangles':len(mesh.loop_triangles)}
    result.append(entry)
    print('TOPOLOGY_PROBE',json.dumps(entry),flush=True)
    bm.free(); bpy.data.meshes.remove(mesh)
base.free()
(O/'topology_probe.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print('TOPOLOGY_PROBE_OK',flush=True)
