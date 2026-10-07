"""Build editable source-faithful mechanical parts, restored rig and actual source actions."""
import bpy,bmesh,json,math,hashlib
import numpy as np
from pathlib import Path
from collections import Counter,defaultdict
from mathutils import Vector,Matrix,Quaternion
from mathutils.kdtree import KDTree

R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004'); O=R/'Production_B_v1'
if (O/'production_manifest.json').exists(): raise RuntimeError('Published B-v1 is frozen')
bpy.ops.wm.open_mainfile(filepath=str(R/'References_A_v2/ControlRigMech_A_v2_ReferenceStudy.blend'))
scene=bpy.context.scene; src=next(o for o in scene.objects if o.type=='MESH')
old_rig=next(o for o in scene.objects if o.type=='ARMATURE')
parts=json.loads((R/'Baseline/source_connected_parts.json').read_text())
info=json.loads((R/'Source/source_manifest.json').read_text(encoding='utf-8'))
setup=json.loads((R/'References_A_v2/reference_setup.json').read_text(encoding='utf-8'))
approved=json.loads((R/'review_decisions.json').read_text(encoding='utf-8'))
assert approved['A']['status']=='approved' and approved['A']['submitted_version']=='A-v2'
old_names=[g.name for g in src.vertex_groups]; names=[b['name'] for b in info['bones']]
name_index={n:i for i,n in enumerate(names)}
points=[src.matrix_world@v.co for v in src.data.vertices]
nm=src.matrix_world.to_3x3().inverted().transposed()
normals=[(nm@n.vector).normalized() for n in src.data.corner_normals]
weights=[{old_names[g.group]:g.weight for g in v.groups if g.weight>1e-7} for v in src.data.vertices]
source_col=bpy.data.collections.new('SOURCE_REFERENCE_DO_NOT_EXPORT'); scene.collection.children.link(source_col)
for ob in (src,old_rig,old_rig.parent):
    for c in list(ob.users_collection): c.objects.unlink(ob)
    source_col.objects.link(ob)
source_col.hide_render=True; source_col.hide_viewport=True
col=bpy.data.collections.new('EDITABLE_MECHANICAL_PARTS'); scene.collection.children.link(col)
partners=bpy.data.collections.new('MIRROR_SOURCE_PARTNERS'); scene.collection.children.link(partners)
partners.hide_render=True; partners.hide_viewport=True
normal_col=bpy.data.collections.new('SOURCE_NORMAL_TRANSFER_HELPERS'); scene.collection.children.link(normal_col)
normal_col.hide_render=True; normal_col.hide_viewport=True

# Reconstruct every imported rest frame in meter units and add the missing UE root.
arm=bpy.data.armatures.new('ControlRigMech_152_SourceCompatible'); rig=bpy.data.objects.new('Armature',arm)
scene.collection.objects.link(rig); rig.show_in_front=True
bpy.context.view_layer.objects.active=rig; rig.select_set(True); bpy.ops.object.mode_set(mode='EDIT')
for item in info['bones']:
    b=arm.edit_bones.new(item['name'])
    if item['name']=='root': b.head=(0,0,0); b.tail=(0,.5,0)
    else:
        old=old_rig.data.bones[item['name']]
        b.head=old_rig.matrix_world@old.head_local; b.tail=old_rig.matrix_world@old.tail_local
        rot=(old_rig.matrix_world.to_3x3()@old.matrix_local.to_3x3()).normalized()
        m=rot.to_4x4(); m.translation=b.head; b.matrix=m
    if item['parent']: b.parent=arm.edit_bones[item['parent']]
    b.use_connect=False; b.use_deform=True
bpy.ops.object.mode_set(mode='OBJECT')
assert len(arm.bones)==152
for item in info['bones']:
    b=arm.bones[item['name']]
    assert (b.parent.name if b.parent else '')==item['parent']
    b['UE_source_reference_local_TQS']=json.dumps(item['local'])
rig['source_skeleton']=info['source_skeleton']; rig['source_control_rig']=info['control_rig']['path']
rig['reference_version']='A-v2'; rig['source_bone_names_hierarchy_preserved']=True
rig['export_UE_compatibility_not_yet_validated']=True

def rigid_face_bone(p):
    values=[]
    for vi in p.vertices:
        w=weights[vi]
        if len(w)!=1 or abs(next(iter(w.values()))-1)>1e-5: return None
        values.append(next(iter(w)))
    return values[0] if len(set(values))==1 else None

objects=[]; records=[]; skipped=0
for part in parts:
    grouped=defaultdict(list)
    for pi in part['polygon_ids']:
        p=src.data.polygons[pi]
        if p.material_index==6: skipped+=1; continue
        bone=rigid_face_bone(p)
        grouped[(bone or 'SOURCE_FLEX',p.material_index)].append(p)
    for (bone,color_group),polys in grouped.items():
        ids=sorted({i for p in polys for i in p.vertices}); idx={v:i for i,v in enumerate(ids)}
        label=f'Mech_{bone if bone!="SOURCE_FLEX" else part["dominant_bone"]}_P{part["component"]}'
        if bone=='SOURCE_FLEX': label+='_Flexible'
        label+=f'_C{color_group}'
        mesh=bpy.data.meshes.new(label)
        mesh.from_pydata([points[i] for i in ids],[],[[idx[i] for i in p.vertices] for p in polys]); mesh.update()
        for mat in src.data.materials: mesh.materials.append(mat)
        for p,s in zip(mesh.polygons,polys): p.material_index=s.material_index; p.use_smooth=s.use_smooth
        mesh.normals_split_custom_set([normals[i] for p in polys for i in p.loop_indices])
        helper=bpy.data.objects.new(label+'_SurfaceNormalSource',mesh.copy()); normal_col.objects.link(helper)
        ob=bpy.data.objects.new(label,mesh); col.objects.link(ob)
        for n in names: ob.vertex_groups.new(name=n)
        for local,old_i in enumerate(ids):
            w={bone:1.0} if bone!='SOURCE_FLEX' else weights[old_i]
            for n,value in w.items(): ob.vertex_groups[n].add([local],value,'REPLACE')
        bm=bmesh.new(); bm.from_mesh(mesh)
        bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.000002)
        bmesh.ops.dissolve_limit(bm,angle_limit=.005,verts=list(bm.verts),edges=list(bm.edges),
                                use_dissolve_boundaries=False,delimit={'MATERIAL'})
        bm.to_mesh(mesh); bm.free(); mesh.update(); mesh.calc_loop_triangles()
        # Source geometry remains editable, reduction is a modifier, and small parts never disappear.
        tris=len(mesh.loop_triangles)
        dims=part['dimensions_m']; maximum=max(dims)
        rate=.07 if maximum<.2 else .095 if maximum<.5 else .13
        dominant_color=Counter(p.material_index for p in polys).most_common(1)[0][0]
        disc=max(dims)>.18 and sorted(dims)[1]>.65*maximum and min(dims)<.65*maximum
        minimum=72 if disc and dominant_color in (2,3) else 16
        ratio=max(rate,min(1,minimum/max(tris,1)))
        if tris<=60: ratio=1.0
        if len(mesh.polygons)>3 and ratio<1:
            dec=ob.modifiers.new('LOD0_ControlledSegmentReduction','DECIMATE')
            dec.decimate_type='COLLAPSE'; dec.ratio=ratio; dec.use_collapse_triangulate=True
        transfer=ob.modifiers.new('PreserveSourceSurfaceNormals','DATA_TRANSFER')
        transfer.object=helper; transfer.use_loop_data=True; transfer.data_types_loops={'CUSTOM_NORMAL'}
        transfer.loop_mapping='POLYINTERP_NEAREST'; transfer.use_object_transform=True
        skin=ob.modifiers.new('OriginalMechanicalBinding','ARMATURE'); skin.object=rig
        marks=mesh.attributes.get('freestyle_face') or mesh.attributes.new('freestyle_face','BOOLEAN','FACE')
        for mark in marks.data: mark.value=True
        ob.parent=rig; ob.matrix_parent_inverse=Matrix.Identity(4)
        ob['source_component']=part['component']; ob['source_bone']=bone
        ob['geometry_role']='flexible source hose/connector' if bone=='SOURCE_FLEX' else 'rigid mechanical part'
        ob['source_reference_version']='A-v2'; ob['LOD0_ratio']=ratio
        ob['source_normal_helper']=helper.name
        objects.append(ob)
        records.append({'object':label,'source_component':part['component'],'bone':bone,
                        'source_triangles':len(polys),'cleaned_editable_triangles':tris,
                        'ratio':ratio,'source_materials':dict(Counter(p.material_index for p in polys)),
                        'mirrored_partner':None})
    if len(objects)%100==0: print('EDITABLE_PARTS',len(objects),flush=True)

# Mirror only pairs proven to match their approved source geometry to within 0.1 mm.
mirrors=[]; used=set()
for a in objects:
    bone=a['source_bone']
    if not bone.endswith('_l') or a.name in used: continue
    cen=sum((v.co for v in a.data.vertices),Vector())/max(len(a.data.vertices),1)
    if cen.x<.01: continue
    otherbone=bone[:-2]+'_r'
    for b in objects:
        if b.name in used or b['source_bone']!=otherbone or len(a.data.vertices)!=len(b.data.vertices): continue
        cb=sum((v.co for v in b.data.vertices),Vector())/max(len(b.data.vertices),1)
        if (Vector((-cen.x,cen.y,cen.z))-cb).length>.001: continue
        if Counter(p.material_index for p in a.data.polygons)!=Counter(p.material_index for p in b.data.polygons): continue
        kd=KDTree(len(b.data.vertices))
        for v in b.data.vertices: kd.insert(v.co,v.index)
        kd.balance()
        error=max(kd.find(Vector((-v.co.x,v.co.y,v.co.z)))[2] for v in a.data.vertices)
        if error>.0001: continue
        mir=a.modifiers.new('Verified_SourceSymmetry','MIRROR'); mir.use_axis=(True,False,False)
        mir.use_clip=False; mir.use_mirror_merge=False; mir.use_mirror_vertex_groups=True
        a.modifiers.move(len(a.modifiers)-1,0)
        helper=bpy.data.objects[a['source_normal_helper']]
        hmir=helper.modifiers.new('Verified_SourceSymmetry','MIRROR')
        hmir.use_axis=(True,False,False); hmir.use_mirror_merge=False
        dec=next((m for m in a.modifiers if m.type=='DECIMATE'),None)
        if dec: dec.use_symmetry=True; dec.symmetry_axis='X'
        col.objects.unlink(b); partners.objects.link(b); used.update((a.name,b.name))
        mirrors.append({'left':a.name,'source_right':b.name,'maximum_source_mirror_error_m':error})
        for rec in records:
            if rec['object']==a.name: rec['mirrored_partner']=b.name
        break
print('RIG_AND_PARTS_READY',len(objects),'mirrored_pairs',len(mirrors),flush=True)

S=Matrix.Diagonal((1.,-1.,1.,1.))
def ue_matrix(tr):
    if isinstance(tr,dict): t,q,s=tr['translation_cm'],tr['rotation_xyzw'],tr['scale']
    else: t,q,s=tr[:3],tr[3:7],tr[7:10]
    return Matrix.LocRotScale(Vector(t)*.01,Quaternion((q[3],q[0],q[1],q[2])),Vector(s))
ue_reference={b['name']:S@ue_matrix(b['global'])@S for b in info['bones']}
inverse_reference={n:m.inverted() for n,m in ue_reference.items()}
rest={b.name:b.matrix_local.copy() for b in arm.bones}
rest_relative_inverse={b.name:(rest[b.parent.name].inverted()@rest[b.name]).inverted() if b.parent else rest[b.name].inverted() for b in arm.bones}
parent_names={b.name:b.parent.name if b.parent else None for b in arm.bones}
for pb in rig.pose.bones: pb.rotation_mode='QUATERNION'
animation_reports=[]; rig.animation_data_create()
for filename in ('Mech_Deploy','Mech_Idle','Mech_Walk'):
    data=json.loads((O/'AnimationSource'/f'{filename}_FullPose.json').read_text(encoding='utf-8'))
    assert set(data['bone_names'])==set(names) and len(data['bone_names'])==len(names)
    source_track_index={n:i for i,n in enumerate(data['bone_names'])}
    channels={n:[] for n in names}; quaternion_previous={}
    for frame in data['frames']:
        global_ue={}; desired={}
        for i,n in enumerate(names):
            local=ue_matrix(frame['local_tqs'][source_track_index[n]]); parent=parent_names[n]
            global_ue[n]=global_ue[parent]@local if parent else local
            desired[n]=(S@global_ue[n]@S)@inverse_reference[n]@rest[n]
            relative=desired[parent].inverted()@desired[n] if parent else desired[n]
            basis=rest_relative_inverse[n]@relative
            loc,quat,scale=basis.decompose()
            if n in quaternion_previous and quat.dot(quaternion_previous[n])<0: quat.negate()
            quaternion_previous[n]=quat.copy()
            channels[n].append([*loc,quat.w,quat.x,quat.y,quat.z,*scale])
    action=bpy.data.actions.new('ControlRigMech_'+filename+'_Source30fps'); action.use_fake_user=True
    slot=action.slots.new('OBJECT',rig.name); layer=action.layers.new('SourceMechanicalMotion')
    strip=layer.strips.new(type='KEYFRAME'); bag=strip.channelbag(slot,ensure=True)
    frame_numbers=np.array([f['time_s']*30+1 for f in data['frames']],dtype=np.float32)
    for n in names:
        values=np.array(channels[n],dtype=np.float32)
        for attr,start,length in [('location',0,3),('rotation_quaternion',3,4),('scale',7,3)]:
            for axis in range(length):
                fc=bag.fcurves.new(data_path=f'pose.bones["{n}"].{attr}',index=axis,group_name=n)
                indices=np.arange(len(frame_numbers)) if np.ptp(values[:,start+axis])>1e-7 else np.array([0,len(frame_numbers)-1])
                fc.keyframe_points.add(len(indices))
                coordinates=np.column_stack((frame_numbers[indices],values[indices,start+axis])).ravel()
                fc.keyframe_points.foreach_set('co',coordinates)
                for k in fc.keyframe_points: k.interpolation='LINEAR'
                fc.update()
    rig.animation_data.action=action; rig.animation_data.action_slot=slot
    errors=[]
    for frame_i in (0,len(data['frames'])//2,len(data['frames'])-1):
        raw=data['frames'][frame_i]; scene.frame_set(round(raw['time_s']*30)+1)
        bpy.context.view_layer.update(); global_ue={}
        for i,n in enumerate(names):
            local=ue_matrix(raw['local_tqs'][source_track_index[n]]); parent=parent_names[n]
            global_ue[n]=global_ue[parent]@local if parent else local
            expected=(S@global_ue[n]@S)@inverse_reference[n]@rest[n]
            actual=rig.pose.bones[n].matrix
            errors.append(max(abs(actual[r][c]-expected[r][c]) for r in range(4) for c in range(4)))
    animation_reports.append({'name':filename,'action':action.name,'duration_s':data['duration_s'],'fps':30,
                              'source_frames':data['frame_count'],'bone_count':152,'curve_count':len(bag.fcurves),
                              'sampled_pose_matrix_max_error':max(errors),'original_translation_and_scale_preserved':True})
    print('ANIMATION_READY',json.dumps(animation_reports[-1]),flush=True)

rig.animation_data.action=None
for pb in rig.pose.bones: pb.matrix_basis=Matrix.Identity(4)
scene.frame_set(1); bpy.context.view_layer.update()
body_stats=[]; total=0
for ob in list(col.objects):
    ev=ob.evaluated_get(bpy.context.evaluated_depsgraph_get()); m=ev.to_mesh(); m.calc_loop_triangles()
    if not m.loop_triangles:
        ev.to_mesh_clear()
        dec=next((x for x in ob.modifiers if x.type=='DECIMATE'),None)
        if dec: dec.ratio=1; ob['LOD0_ratio']=1.0
        bpy.context.view_layer.update()
        ev=ob.evaluated_get(bpy.context.evaluated_depsgraph_get()); m=ev.to_mesh(); m.calc_loop_triangles()
        print('PROTECTED_THIN_COMPONENT',ob.name,flush=True)
    tri=len(m.loop_triangles); total+=tri
    body_stats.append({'object':ob.name,'triangles':tri,'vertices':len(m.vertices),'bounds_m':[list(Vector([min(v.co[i] for v in m.vertices) for i in range(3)])),list(Vector([max(v.co[i] for v in m.vertices) for i in range(3)]))] if m.vertices else None})
    ev.to_mesh_clear()
assert all(s['triangles']>0 for s in body_stats),'A structural component disappeared'
report={'version':'B-v1-working','approved_reference':'A-v2','stage':'production_geometry_and_source_animation',
        'unit_system':'meters, production object and rig scales one','source_reference_unchanged':True,
        'source_visible_component_count':737,'editable_parts':records,'active_editable_objects':len(body_stats),
        'verified_mirror_pairs':mirrors,'source_decals_suppressed_faces':skipped,
        'LOD0_body_triangles':total,'LOD0_body_cap':32000,'LOD0_body_difference':max(total-32000,0),
        'body_component_stats':body_stats,'rig_bones':152,'source_bone_names':names,'animations':animation_reports,
        'production_outline_atlas_lods_pending':True,'UE_export_readback_pending_B':True}
(O/'construction_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
scene.render.resolution_x=scene.render.resolution_y=2048
scene.render.resolution_percentage=100; scene.render.use_freestyle=True
hero=setup['cameras']['Hero']; scene.camera.location=hero['location_m']; scene.camera.rotation_euler=hero['rotation_radians']
scene.camera.data.ortho_scale=hero['ortho_scale_m']
scene.render.filepath=str(O/'GeometryCandidate_Hero.png')
bpy.ops.wm.save_as_mainfile(filepath=str(O/'ControlRigMech_B_v1_EditableParts.blend'))
bpy.ops.render.render(write_still=True)
print('PRODUCTION_GEOMETRY_B_V1_OK',total,'triangles',flush=True)
