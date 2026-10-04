"""B-v1 exact source-derived editable mechanical parts, mirror and LOD0."""
import bpy
import bmesh
import json
import math
from collections import defaultdict
from pathlib import Path
from mathutils import Vector, Matrix

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
OUT = ROOT/'Production_B_v1'
assert json.loads((ROOT/'approval_A_v3_20261003.json').read_text(encoding='utf-8'))['status']=='approved'
assert not (OUT/'review_manifest.json').exists(), 'Published B version is frozen'
scene=bpy.context.scene
source=next(o for o in scene.objects if o.type=='MESH')
source_rig=next(o for o in scene.objects if o.type=='ARMATURE')
parts=json.loads((ROOT/'Baseline/source_connected_parts.json').read_text(encoding='utf-8'))
source_manifest=json.loads((ROOT/'Source/source_manifest.json').read_text(encoding='utf-8'))
setup=json.loads((ROOT/'References_A_v3/reference_setup.json').read_text(encoding='utf-8'))
colors={p['component']:p['base_color'] for p in setup['component_color_assignments']}
part_map={p['component']:p for p in parts}

def collection(name):
    c=bpy.data.collections.new(name)
    scene.collection.children.link(c)
    return c

reference=collection('00_ReadOnly_SourceReference')
for obj in (source,source_rig):
    for c in list(obj.users_collection): c.objects.unlink(obj)
    reference.objects.link(obj)
    obj.hide_render=True
    obj.hide_set(True)
reference.hide_render=True
editing=collection('01_Editable_Mirrored_Parts')
rig_collection=collection('02_Production_Rig_44Bones')
lod_collection=collection('03_LOD0_Review')
rig=source_rig.copy()
rig.data=source_rig.data.copy()
rig.name='Armature'
rig.data.name='RSGMech_SourceCompatible44'
rig_collection.objects.link(rig)
rig.hide_set(False)
rig.hide_render=False
rig.show_in_front=True
bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True)
bpy.context.view_layer.objects.active=rig
bpy.ops.object.mode_set(mode='EDIT')
root=rig.data.edit_bones.new('DeformationSystem')
root.head=(0,0,0)
root.tail=(0,10,0)
root.roll=0
for bone in rig.data.edit_bones:
    if bone != root and bone.parent is None:
        bone.parent=root
        bone.use_connect=False
bpy.ops.object.mode_set(mode='OBJECT')
expected={b['name']:b['parent'] or None for b in source_manifest['bones']}
actual={b.name:b.parent.name if b.parent else None for b in rig.data.bones}
assert actual==expected, (set(actual)^set(expected))
rig['SourceSkeleton']='/Game/Assets/RSG_UnderWater_Pack/FPS/Models/Mech/SK_FPS_Mech_Skeleton'
rig['OriginalBoneCount']=44

pairs={}
for p in parts:
    if p['center_m'][0] <= .01: continue
    x,y,z=p['center_m']
    bone=(p['dominant_bone'] or '').replace('_L','_R')
    peers=[q for q in parts if q['center_m'][0]<-.01 and q['dominant_bone']==bone
           and max(abs(a-b) for a,b in zip(q['center_m'],(-x,y,z)))<.002
           and max(abs(a-b) for a,b in zip(q['dimensions_m'],p['dimensions_m']))<.002]
    if len(peers)==1:
        assert colors[p['component']]==colors[peers[0]['component']]
        pairs[p['component']]=peers[0]['component']
skipped=set(pairs.values())
masters=[]
records=[]
for part in parts:
    cid=part['component']
    if cid in skipped: continue
    ids=part['vertex_ids']
    remap={vi:i for i,vi in enumerate(ids)}
    mesh=bpy.data.meshes.new(f'RSG_EditMesh_{cid:05d}')
    mesh.from_pydata([source.matrix_world @ source.data.vertices[vi].co for vi in ids],[],
                     [[remap[vi] for vi in source.data.polygons[pi].vertices] for pi in part['polygon_ids']])
    for material in source.data.materials: mesh.materials.append(material)
    for face,pi in zip(mesh.polygons,part['polygon_ids']):
        face.material_index=source.data.polygons[pi].material_index
    bm=bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.000005)
    bmesh.ops.dissolve_limit(bm,angle_limit=math.radians(.15),use_dissolve_boundaries=False,
                           verts=list(bm.verts),edges=list(bm.edges),delimit={'MATERIAL'})
    bm.to_mesh(mesh)
    bm.free()
    mesh.calc_loop_triangles()
    original=len(mesh.loop_triangles)
    bone=part['dominant_bone'] or 'Root_M'
    obj=bpy.data.objects.new(f'Edit_{bone}_{colors[cid]}_{cid:05d}',mesh)
    editing.objects.link(obj)
    obj['SourceComponent']=cid
    obj['RigidBone']=bone
    obj['MirrorComponent']=pairs.get(cid,-1)
    obj['ApprovedColor']=colors[cid]
    obj['SourceTriangles']=original
    group=obj.vertex_groups.new(name=bone)
    group.add(list(range(len(mesh.vertices))),1,'REPLACE')
    if cid in pairs:
        peer_bone=part_map[pairs[cid]]['dominant_bone']
        if peer_bone!=bone: obj.vertex_groups.new(name=peer_bone)
    dec=obj.modifiers.new('Editable_LOD0_Reduction','DECIMATE')
    dec.decimate_type='COLLAPSE'
    dec.use_collapse_triangulate=True
    if abs(part['center_m'][0])<.01:
        dec.use_symmetry=True
        dec.symmetry_axis='X'
    if cid in pairs:
        mirror=obj.modifiers.new('Exact_Left_Right_Symmetry','MIRROR')
        mirror.use_axis=(True,False,False)
        mirror.use_clip=False
        mirror.use_mirror_merge=False
        mirror.use_mirror_vertex_groups=True
    masters.append(obj)
    records.append({'component':cid,'mirror_component':pairs.get(cid),'name':obj.name,
                    'bone':bone,'color':colors[cid],'cleaned_triangles_one_side':original})

def set_ratio(factor):
    for obj in masters:
        cid=obj['SourceComponent']
        p=part_map[cid]
        ratio=max(factor, min(1,12/max(obj['SourceTriangles'],1)))
        if cid in (77,28551): ratio=max(ratio,.82)
        if p['center_m'][2]>5.5 and p['dimensions_m'][2]>1: ratio=1
        obj.modifiers['Editable_LOD0_Reduction'].ratio=min(1,ratio)
    bpy.context.view_layer.update()

def count():
    dg=bpy.context.evaluated_depsgraph_get()
    total=0
    for obj in masters:
        evaluated=obj.evaluated_get(dg)
        mesh=evaluated.to_mesh()
        mesh.calc_loop_triangles()
        total+=len(mesh.loop_triangles)
        evaluated.to_mesh_clear()
    return total

factor=.43
for attempt in range(6):
    set_ratio(factor)
    triangles=count()
    print(json.dumps({'phase':'LOD0_budget','attempt':attempt,'ratio':factor,'triangles':triangles}),flush=True)
    if 29700<=triangles<=31700: break
    factor=max(.1,min(.8,factor*31000/max(triangles,1)))
assert triangles<=32000, 'Preserved source structure exceeds LOD0 budget; report before changing major parts'

vertices=[]
faces=[]
material_ids=[]
component_ids=[]
weights=[]
dg=bpy.context.evaluated_depsgraph_get()
for obj in masters:
    cid=obj['SourceComponent']
    peer=obj['MirrorComponent']
    ev=obj.evaluated_get(dg)
    mesh=ev.to_mesh()
    offset=len(vertices)
    names={g.index:g.name for g in obj.vertex_groups}
    for v in mesh.vertices:
        vertices.append(tuple(v.co))
        strongest=max(v.groups,key=lambda g:g.weight)
        weights.append(names[strongest.group])
    for poly in mesh.polygons:
        faces.append([offset+vi for vi in poly.vertices])
        material_ids.append(poly.material_index)
        x=sum(mesh.vertices[vi].co.x for vi in poly.vertices)/len(poly.vertices)
        component_ids.append(peer if peer>=0 and x<0 else cid)
    ev.to_mesh_clear()
data=bpy.data.meshes.new('RSGMech_LOD0_BodyGeometry')
data.from_pydata(vertices,[],faces)
for mat in source.data.materials: data.materials.append(mat)
for face,mi in zip(data.polygons,material_ids): face.material_index=mi
part_attribute=data.attributes.new('RSG_Component','INT','FACE')
part_attribute.data.foreach_set('value',component_ids)
body=bpy.data.objects.new('RSGMech_LOD0_Body',data)
lod_collection.objects.link(body)
groups={name:body.vertex_groups.new(name=name) for name in sorted(set(weights))}
indices=defaultdict(list)
for i,bone in enumerate(weights): indices[bone].append(i)
for bone,ids in indices.items(): groups[bone].add(ids,1,'REPLACE')
arm=body.modifiers.new('Original_44_Bone_Rigid_Binding','ARMATURE')
arm.object=rig
body['ApprovedReference']='A-v3'
body['LOD']=0
body['MaterialStage']='palette_source_before_atlas'

for obj in masters:
    bind=obj.modifiers.new('Original_Rigid_Binding','ARMATURE')
    bind.object=rig
    obj.hide_render=True
    obj.hide_set(True)
editing.hide_render=True
bpy.ops.object.select_all(action='DESELECT')
body.select_set(True)
bpy.context.view_layer.objects.active=body
bpy.context.view_layer.update()
data.calc_loop_triangles()
limits=[min(v.co[a] for v in data.vertices) for a in range(3)], [max(v.co[a] for v in data.vertices) for a in range(3)]
baseline=json.loads((ROOT/'Baseline/blender_source_inspection.json').read_text(encoding='utf-8'))['bounds_m']
dimensions=[limits[1][a]-limits[0][a] for a in range(3)]
report={'version':'B-v1','approved_reference':'A-v3','source_triangles':68124,
        'editable_master_parts':len(masters),'mirrored_pairs':len(pairs),'physical_components':len(parts),
        'LOD0_body_triangles':len(data.loop_triangles),'LOD0_body_vertices':len(data.vertices),
        'global_decimation_factor':factor,'important_curves_min_ratio':.82,'antennae_kept_full':True,
        'rig_bones':44,'rig_hierarchy_matches_source':True,'rigid_vertex_groups':len(groups),
        'bounds_m':limits,'dimensions_m':dimensions,
        'dimension_delta_m':[dimensions[a]-baseline['dimensions'][a] for a in range(3)],
        'editable_parts':records,'B_approval':'pending','ue_assets_saved':False}
assert all(abs(d)<.015 for d in report['dimension_delta_m']), report['dimension_delta_m']
(OUT/'geometry_build_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
scene['RSG_ApprovedReference']='A-v3'
scene['RSG_ProductionVersion']='B-v1'
scene['RSG_Stage']='Blender production; review B pending'
scene.render.use_freestyle=True
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'RSGMech_Editable_Geometry.blend'))
print(json.dumps({k:report[k] for k in ('editable_master_parts','mirrored_pairs','LOD0_body_triangles','rig_bones','dimension_delta_m')},ensure_ascii=False),flush=True)
