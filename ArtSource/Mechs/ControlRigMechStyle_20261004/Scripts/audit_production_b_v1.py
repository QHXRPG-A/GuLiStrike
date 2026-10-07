"""Inspect saved native production data without exporting/importing any UE asset."""
import bpy,json,hashlib,math
import numpy as np
from pathlib import Path
from mathutils.bvhtree import BVHTree
from mathutils import Vector,Matrix
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004'); O=R/'Production_B_v1'
bpy.ops.wm.open_mainfile(filepath=str(O/'ControlRigMech_B_v1_Production.blend'))
info=json.loads((R/'Source/source_manifest.json').read_text(encoding='utf-8')); rig=bpy.data.objects['Armature']
hierarchy=[{'name':b.name,'parent':b.parent.name if b.parent else ''} for b in rig.data.bones]
expected=[{'name':b['name'],'parent':b['parent']} for b in info['bones']]
assert {x['name']:x['parent'] for x in hierarchy}=={x['name']:x['parent'] for x in expected}
assert len(rig.data.bones)==152 and list(rig.scale)==[1,1,1]
def geom_hash(ob):
    h=hashlib.sha256()
    for v in ob.data.vertices:
        h.update(np.array(v.co[:],dtype=np.float32).tobytes())
        for g in v.groups: h.update(f'{g.group}:{g.weight:.9f};'.encode())
    for p in ob.data.polygons: h.update(np.array(p.vertices[:],dtype=np.int32).tobytes())
    normals=np.array([n.vector[:] for n in ob.data.corner_normals],dtype=np.float32); h.update(normals.tobytes())
    return h.hexdigest()
lods=[]
for i in range(4):
    ob=bpy.data.objects[f'ControlRigMech_LOD{i}_Body']; mesh=ob.data; mesh.calc_loop_triangles()
    weights=[sum(g.weight for g in v.groups) for v in mesh.vertices]
    vertices=np.array([v.co[:] for v in mesh.vertices]); normals=np.array([n.vector[:] for n in mesh.corner_normals])
    errors=max(abs(x-1) for x in weights); zero=sum(x<1e-7 for x in weights)
    duplicate_faces=len(mesh.polygons)-len({tuple(sorted(p.vertices)) for p in mesh.polygons})
    oriented=[]
    for p in mesh.polygons:
        ring=tuple(p.vertices); oriented.append(min(ring[k:]+ring[:k] for k in range(len(ring))))
    duplicate_oriented=len(oriented)-len(set(oriented))
    degenerate=sum(t.area<1e-12 for t in mesh.loop_triangles)
    matching=geom_hash(ob)==geom_hash(bpy.data.objects[f'B_Review_LOD{i}_Body'])
    entry={'lod':i,'vertices':len(mesh.vertices),'triangles':len(mesh.loop_triangles),'bounds_m':[vertices.min(axis=0).tolist(),vertices.max(axis=0).tolist()],
           'unweighted_vertices':zero,'max_weight_sum_error':errors,'nonfinite_vertex_count':int((~np.isfinite(vertices)).sum()),
           'nonfinite_corner_normal_count':int((~np.isfinite(normals)).sum()),'degenerate_triangles':degenerate,'duplicate_index_faces':duplicate_faces,
           'duplicate_same_winding_faces':duplicate_oriented,'opposed_index_coincident_faces':duplicate_faces-duplicate_oriented,
           'review_portable_geometry_normals_weights_hash_identical':matching,'geometry_skin_normal_sha256':geom_hash(ob),
           'body_material_sections':len(mesh.materials),'vertex_group_names_complete':set(ob.vertex_groups.keys())==set(rig.data.bones.keys())}
    assert zero==0 and errors<1e-5 and np.isfinite(vertices).all() and np.isfinite(normals).all() and matching
    lods.append(entry)
src=bpy.data.objects['SKM_Mech.001']
def stored_world(ob):
    return stored_world(ob.parent)@ob.matrix_parent_inverse@ob.matrix_basis if ob.parent else ob.matrix_basis.copy()
source_world=stored_world(src)
points=[source_world@v.co for v in src.data.vertices]
bvh=BVHTree.FromPolygons(points,[list(p.vertices) for p in src.data.polygons])
body=bpy.data.objects['ControlRigMech_LOD0_Body']; dist=[]
stride=max(1,len(body.data.vertices)//8000)
for v in list(body.data.vertices)[::stride]:
    nearest=bvh.find_nearest(v.co)
    if nearest[0] is not None: dist.append(nearest[3])
source=np.array([v[:] for v in points]); bodyxyz=np.array([v.co[:] for v in body.data.vertices])
textures=[{'file':f.name,'file_bytes':f.stat().st_size,'resolution':[2048,2048],'uncompressed_RGBA8_bytes':2048*2048*4} for f in sorted((O/'Textures').glob('*.png'))]
report={'version':'B-v1','verification':'partial','bone_count':152,'source_bone_names_hierarchy_match':True,'root_rest_at_origin_m':list(rig.data.bones['root'].head_local),
        'rig_identity_object_scale':list(rig.scale),'lods':lods,'authoring_parts':len(bpy.data.collections['EDITABLE_MECHANICAL_PARTS'].objects),
        'editable_mirror_modifiers':sum(m.type=='MIRROR' for ob in bpy.data.collections['EDITABLE_MECHANICAL_PARTS'].objects for m in ob.modifiers),
        'editable_screw_modifiers':sum(m.type=='SCREW' for ob in bpy.data.collections['EDITABLE_MECHANICAL_PARTS'].objects for m in ob.modifiers),
        'source_dims_m':(source.max(axis=0)-source.min(axis=0)).tolist(),'body_dims_m':(bodyxyz.max(axis=0)-bodyxyz.min(axis=0)).tolist(),
        'sampled_nearest_source_surface_distance_m':{'samples':len(dist),'median':float(np.median(dist)),'p95':float(np.quantile(dist,.95)),'max':max(dist),
                                                    'scope':'sampled LOD0 vertex-to-source-surface distance only; does not certify all assembly intersections'},
        'textures':textures,'total_texture_RGBA8_bytes':sum(x['uncompressed_RGBA8_bytes'] for x in textures),
        'UE_export_readback':'pending B approval','UE_formal_import':'not run','commander_UE_cameras':'not run','runtime_fps_measurement':'not run'}
(O/'native_geometry_audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('B_NATIVE_AUDIT_OK',json.dumps({k:report[k] for k in ('bone_count','authoring_parts','editable_mirror_modifiers','editable_screw_modifiers','source_dims_m','body_dims_m','sampled_nearest_source_surface_distance_m')},ensure_ascii=False),flush=True)
