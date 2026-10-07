"""Evaluate per-component editable reductions while keeping mechanical parts separate."""
import bpy,bmesh,json,math
from pathlib import Path
from collections import Counter
from mathutils import Vector

R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004'); O=R/'Production_B_v1'
bpy.ops.wm.open_mainfile(filepath=str(R/'References_A_v2/ControlRigMech_A_v2_ReferenceStudy.blend'))
scene=bpy.context.scene; src=next(o for o in scene.objects if o.type=='MESH')
parts=json.loads((R/'Baseline/source_connected_parts.json').read_text())
points=[src.matrix_world@v.co for v in src.data.vertices]
normals=[src.matrix_world.to_3x3().inverted().transposed()@n.vector for n in src.data.corner_normals]
records=[]; ratios=[.09,.115,.16,.24]
for part in parts:
    polys=[src.data.polygons[i] for i in part['polygon_ids'] if src.data.polygons[i].material_index!=6]
    if not polys: continue
    ids=part['vertex_ids']; idx={v:i for i,v in enumerate(ids)}
    mesh=bpy.data.meshes.new('ComponentProbe')
    mesh.from_pydata([points[i] for i in ids],[],[[idx[i] for i in p.vertices] for p in polys]); mesh.update()
    for mat in src.data.materials: mesh.materials.append(mat)
    for p,s in zip(mesh.polygons,polys): p.material_index=s.material_index; p.use_smooth=True
    # Normals are inherited from the real source; reduction transfers custom normals.
    mesh.normals_split_custom_set([normals[i] for p in polys for i in p.loop_indices])
    bm=bmesh.new(); bm.from_mesh(mesh)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.000002)
    bmesh.ops.dissolve_limit(bm,angle_limit=.005,verts=list(bm.verts),edges=list(bm.edges),use_dissolve_boundaries=True,delimit={'MATERIAL'})
    bm.to_mesh(mesh); bm.free(); mesh.update(); mesh.calc_loop_triangles()
    obj=bpy.data.objects.new('ComponentProbe',mesh); scene.collection.objects.link(obj)
    mod=obj.modifiers.new('ConservativeSegmentReduction','DECIMATE'); mod.decimate_type='COLLAPSE'; mod.use_collapse_triangulate=True
    result={'component':part['component'],'bone':part['dominant_bone'],'dimensions_m':part['dimensions_m'],
            'source_triangles':len(polys),'conservative_triangles':len(mesh.loop_triangles),'options':{}}
    for ratio in ratios:
        mod.ratio=ratio
        bpy.context.view_layer.update()
        evaluated=obj.evaluated_get(bpy.context.evaluated_depsgraph_get()); m=evaluated.to_mesh(); m.calc_loop_triangles()
        result['options'][str(ratio)]={'triangles':len(m.loop_triangles),'vertices':len(m.vertices),
                                    'minimum_area':min((p.area for p in m.polygons),default=0)}
        evaluated.to_mesh_clear()
    records.append(result)
    bpy.data.objects.remove(obj,do_unlink=True); bpy.data.meshes.remove(mesh)
    if len(records)%100==0: print('COMPONENTS',len(records),flush=True)
summary={str(r):sum(p['options'][str(r)]['triangles'] for p in records) for r in ratios}
(O/'component_reduction_probe.json').write_text(json.dumps({'components':records,'summary':summary},indent=2),encoding='utf-8')
print('COMPONENT_REDUCTION_PROBE_OK',len(records),summary,flush=True)
