"""Compare actual reduced foot assemblies against unchanged source skin at key poses."""
import bpy,json,numpy as np
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004'); O=R/'Production_B_v2'
bpy.ops.wm.open_mainfile(filepath=str(O/'ControlRigMech_B_v2_Production.blend'))
s=bpy.data.scenes['REVIEW_B_ReferenceMatched']; bpy.context.window.scene=s
rig=bpy.data.objects['Armature']; rig.data.pose_position='POSE'; body=bpy.data.objects['B_Review_LOD0_Body']; src=bpy.data.objects['SKM_Mech.001']
def stored_world(ob): return stored_world(ob.parent)@ob.matrix_parent_inverse@ob.matrix_basis if ob.parent else ob.matrix_basis.copy()
xyz=np.array([(stored_world(src)@v.co)[:] for v in src.data.vertices]); xyzw=np.c_[xyz,np.ones(len(xyz))]
source_groups={g.index:g.name for g in src.vertex_groups}; production_groups={g.index:g.name for g in body.vertex_groups}
def foot_tag(name):
    if not name.startswith(('foot_','toe_','toecord_')): return None
    for region in ('fr','bk'):
        if f'_{region}_' in name:
            if name.endswith('_l'): return region+'_l'
            if name.endswith('_r'): return region+'_r'
source_indices={k:[] for k in ('fr_l','fr_r','bk_l','bk_r')}; production_indices={k:[] for k in source_indices}
weighted={}
for v in src.data.vertices:
    tags=set()
    for g in v.groups:
        name=source_groups[g.group]; weighted.setdefault(name,[[],[]]); weighted[name][0].append(v.index); weighted[name][1].append(g.weight)
        if g.weight>.1 and foot_tag(name): tags.add(foot_tag(name))
    for tag in tags: source_indices[tag].append(v.index)
for v in body.data.vertices:
    tags={foot_tag(production_groups[g.group]) for g in v.groups if g.weight>.1 and foot_tag(production_groups[g.group])}
    for tag in tags: production_indices[tag].append(v.index)
weighted={n:(np.array(ids),np.array(w)[:,None]) for n,(ids,w) in weighted.items()}
rests={n:rig.data.bones[n].matrix_local.inverted() for n in weighted}
rows=[]
for clip in ('Deploy','Idle','Walk'):
    data=json.loads((O/'AnimationSource'/f'Mech_{clip}_FullPose.json').read_text())
    action=bpy.data.actions[f'ControlRigMech_Mech_{clip}_Source30fps']; rig.animation_data.action=action; rig.animation_data.action_slot=action.slots[0]
    for t in (0,data['duration_s']/2,data['duration_s']):
        frame=1+t*30; s.frame_set(int(frame),subframe=frame-int(frame)); bpy.context.view_layer.update()
        source_posed=np.zeros((len(xyz),3))
        for n,(ids,w) in weighted.items():
            deform=np.array(rig.pose.bones[n].matrix@rests[n]); source_posed[ids]+=((xyzw[ids]@deform.T)[:,:3])*w
        evaluated=body.evaluated_get(bpy.context.evaluated_depsgraph_get()); production_posed=np.array([(evaluated.matrix_world@v.co)[:] for v in evaluated.data.vertices])
        feet={}
        for tag,ids in source_indices.items():
            src_z=float(source_posed[ids,2].min()); prod_z=float(production_posed[production_indices[tag],2].min())
            feet[tag]={'source_lowest_z_m':src_z,'production_lowest_z_m':prod_z,'difference_m':prod_z-src_z}
        rows.append({'clip':clip,'time_s':t,'feet':feet})
report={'version':'B-v2','poses':rows,'method':'original source vertex weights deformed by the same preserved bone pose; includes foot, toes and toe cords',
        'max_foot_lowest_z_difference_m':max(abs(v['difference_m']) for row in rows for v in row['feet'].values()),
        'scope':'9 key poses, source-relative height comparison; no animation edits; does not certify all-frame foot contact or all intersections'}
(O/'motion_contact_audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('B_CONTACT_AUDIT_OK',report['max_foot_lowest_z_difference_m'],flush=True)
