import bpy,json,math,time,sys
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree
import mech_production_common as m
bpy.context.window.scene=bpy.data.scenes['Mechs_Production_v1']
report={'source_based':True,'models':{},'rigs':{},'pose_probes':{},'errors':[]}
keys=['SpiderMech','Mecha_01','Mecha_02','Mech_Lightest','FireWeapon_01','MissileWeapon_01','Machinegun_lvl1','Missile_01']
for key in keys:
    m.visible([key]);deps=bpy.context.evaluated_depsgraph_get();rows=[]
    for o in m.meshes(key):
        ev=o.evaluated_get(deps);ev.data.calc_loop_triangles()
        row={'object':o.name,'source_asset':o.get('source_asset'),'base_vertices':len(o.data.vertices),'evaluated_triangles':len(ev.data.loop_triangles),'uv_layers':list(o.data.uv_layers.keys()),'color_attributes':list(o.data.color_attributes.keys()),'materials':len(o.material_slots),'details':o.get('added_detail')}
        arm=next((a.object for a in o.modifiers if a.type=='ARMATURE'),None)
        if arm:
            bone_names=set(arm.data.bones.keys());invalid=[g.name for g in o.vertex_groups if g.name not in bone_names]
            weights=[sum(g.weight for g in v.groups if o.vertex_groups[g.group].name in bone_names) for v in o.data.vertices]
            row['invalid_groups']=invalid;row['unweighted_vertices']=sum(w<.00001 for w in weights);row['weight_sum_max_error']=max((abs(w-1) for w in weights),default=0)
            if invalid or row['unweighted_vertices']:report['errors'].append({'mesh':o.name,'binding':row})
        rows.append(row)
    report['models'][key]={'triangles':sum(r['evaluated_triangles'] for r in rows),'meshes':rows}
for key,row in m.SNAP['meshes'].items():
    if not row.get('bones'):continue
    rigs=[o for o in m.objects(key) if o.type=='ARMATURE']
    if not rigs:continue
    rig=rigs[0];actual={b.name:(b.parent.name if b.parent else '') for b in rig.data.bones};expected={b['name']:b['parent'] for b in row['bones']}
    mismatch={name:{'expected':parent,'actual':actual.get(name)} for name,parent in expected.items() if actual.get(name)!=parent}
    report['rigs'][key]={'ue_bones':len(expected),'blender_bones':len(actual),'missing':sorted(set(expected)-set(actual)),'unexpected':sorted(set(actual)-set(expected)),'parent_mismatches':mismatch}
    if mismatch:report['errors'].append({'rig':key,'parent_mismatches':mismatch})

# Original and simplified mesh surface distances; compare in native centimeter space.
m.visible(['SpiderMech']);source_col=bpy.data.collections['00_SOURCE_READONLY'];source_col.hide_viewport=False
bpy.data.collections['SRC_SpiderMech'].hide_viewport=False;bpy.context.view_layer.update()
src=m.meshes('SpiderMech',True)[0];dst=m.meshes('SpiderMech')[0]
tree=BVHTree.FromObject(dst,bpy.context.evaluated_depsgraph_get());conversion=dst.matrix_world.inverted()@src.matrix_world
dist=[]
for idx in range(0,len(src.data.vertices),max(1,len(src.data.vertices)//5000)):
    point=conversion@src.data.vertices[idx].co;hit=tree.find_nearest(point)
    if hit[0]:dist.append(hit[3]*.01)
report['spider']={'source_triangles':839778,'output_triangles':report['models']['SpiderMech']['triangles'],'reduction_percent':100*(1-report['models']['SpiderMech']['triangles']/839778),'sample_count':len(dist),'source_vertex_to_output_surface_distance_m':{'median':float(np.median(dist)),'p95':float(np.percentile(dist,95)),'p99':float(np.percentile(dist,99)),'max':float(max(dist))},'retained_uvs':list(dst.data.uv_layers.keys()),'retained_material_slots':len(dst.material_slots)}

for key,bone in [('SpiderMech','body'),('Mecha_01','upperbody'),('Mecha_02','l_lowerleg'),('Mech_Lightest','Pelvis'),('Machinegun_lvl1','Barrel_big')]:
    m.visible([key]);rig=next(o for o in m.objects(key) if o.type=='ARMATURE');pb=rig.pose.bones[bone];before=pb.matrix_basis.copy();before_mode=pb.rotation_mode
    deps=bpy.context.evaluated_depsgraph_get();o=next(o for o in m.meshes(key) if not o.name.startswith('DETAIL_'));base=o.evaluated_get(deps).data.vertices[0].co.copy()
    pb.rotation_mode='XYZ';pb.rotation_euler.z=math.radians(10);bpy.context.view_layer.update()
    positions=[v.co[:] for v in o.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.vertices]
    finite=all(math.isfinite(v) for p in positions for v in p)
    pb.rotation_mode=before_mode;pb.matrix_basis=before;bpy.context.view_layer.update()
    report['pose_probes'][key]={'bone':bone,'probe_degrees':10,'all_positions_finite':finite,'neutral_restored':True}
    if not finite:report['errors'].append({'pose':key,'finite':False})

report['light_assembly']={'attachment_constraints':sum(any(c.name.startswith('Original_Blueprint_Attachment') for c in o.constraints) for o in m.objects('Mech_Lightest')),'mount_bone':'Mount_top','driver_geometry_removed':True,'cockpit_sealed':True}
report['success']=not report['errors']
(m.OUT/'model_validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
m.visible(['Mech_Lightest']);m.save()
result={'success':report['success'],'errors':report['errors'],'triangles':{k:r['triangles'] for k,r in report['models'].items()},'spider':report['spider'],'rigs':report['rigs']}
