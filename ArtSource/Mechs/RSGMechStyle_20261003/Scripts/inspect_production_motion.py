"""Check all sampled original poses, rigid part distances and six-foot motion."""
import bpy
import json
from collections import defaultdict
from pathlib import Path
from mathutils import Matrix, Vector

ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
OUT=ROOT/'Production_B_v1'
source=json.loads((ROOT/'Source/Animations/source_world_poses.json').read_text(encoding='utf-8'))
scene=bpy.context.scene
rig=bpy.data.objects['Armature']
body=bpy.data.objects['RSGMech_LOD0_Body']
source_body=bpy.data.objects['SK_FPS_Mech.001']
source_rig=bpy.data.objects['DeformationSystem']
groups={g.index:g.name for g in body.vertex_groups}
source_groups={g.index:g.name for g in source_body.vertex_groups}
foot_bones=[f'{region}Leg4_{side}' for region in ('Front','Middle','Back') for side in ('L','R')]
vertices_by_bone=defaultdict(list)
for v in body.data.vertices:
    bone=groups[max(v.groups,key=lambda g:g.weight).group]
    vertices_by_bone[bone].append(v.index)
source_vertices_by_bone=defaultdict(list)
for v in source_body.data.vertices:
    bone=source_groups[max(v.groups,key=lambda g:g.weight).group]
    if bone in foot_bones:
        source_vertices_by_bone[bone].append(source_body.matrix_world @ v.co)
rest_inverse={b.name:(source_rig.matrix_world @ b.matrix_local).inverted() for b in source_rig.data.bones}
floor_z=min(v.co.z for v in body.data.vertices)
report={'method':'all 30fps original samples; rigid foot vertices compared with original source vertices',
        'floor_z_m':floor_z,'six_foot_bones':foot_bones,'actions':[],
        'no_animation_retiming_or_IK_changes':True,
        'ground_contact_limit':'Source actions preserve authored contact; runtime ground alignment/IK is outside this art change.'}
for record in source['animations']:
    action=bpy.data.actions[record['name']]
    rig.animation_data.action=action
    if action.slots: rig.animation_data.action_slot=action.slots[0]
    samples=[]
    rigid_error=0
    max_pose_error=0
    bounds_min=Vector((1e9,1e9,1e9)); bounds_max=Vector((-1e9,-1e9,-1e9))
    foot_min_delta=0
    for index,frame in enumerate(record['frames'],1):
        scene.frame_set(index)
        bpy.context.view_layer.update()
        for name,pb in ((p.name,p) for p in rig.pose.bones):
            expected=Matrix(frame['armature_world'] if name=='DeformationSystem' else frame['bones'][name])
            max_pose_error=max(max_pose_error,((rig.matrix_world @ pb.matrix).translation-expected.translation).length)
        ev=body.evaluated_get(bpy.context.evaluated_depsgraph_get())
        data=ev.to_mesh()
        for v in data.vertices:
            co=ev.matrix_world @ v.co
            for axis in range(3):
                bounds_min[axis]=min(bounds_min[axis],co[axis])
                bounds_max[axis]=max(bounds_max[axis],co[axis])
        feet=[]
        for bone in foot_bones:
            vis=vertices_by_bone[bone]
            current=min((ev.matrix_world @ data.vertices[vi].co).z for vi in vis)
            source_matrix=Matrix(frame['bones'][bone]) @ rest_inverse[bone]
            original=min((source_matrix @ co).z for co in source_vertices_by_bone[bone])
            foot_min_delta=max(foot_min_delta,abs(current-original))
            feet.append({'bone':bone,'production_min_z_m':current,'source_min_z_m':original,
                         'distance_to_preview_floor_m':current-floor_z})
        # A fixed vertex-pair per rigid group detects unintended bending/scaling.
        for bone,vis in vertices_by_bone.items():
            if len(vis)<2: continue
            first,last=vis[0],vis[-1]
            rest=(body.data.vertices[first].co-body.data.vertices[last].co).length
            now=((ev.matrix_world @ data.vertices[first].co)-(ev.matrix_world @ data.vertices[last].co)).length
            rigid_error=max(rigid_error,abs(rest-now))
        samples.append({'frame':index,'feet':feet})
        ev.to_mesh_clear()
    item={'action':record['name'],'samples':len(samples),
          'max_bone_position_error_m':max_pose_error,'max_rigid_vertex_pair_length_error_m':rigid_error,
          'max_foot_min_z_difference_from_source_m':foot_min_delta,
          'animated_bounds_m':[list(bounds_min),list(bounds_max)],
          'all_frames_six_foot_samples':samples}
    assert max_pose_error<.0001
    assert rigid_error<.0002
    report['actions'].append(item)
    print(json.dumps({k:v for k,v in item.items() if k!='all_frames_six_foot_samples'}),flush=True)
rig.animation_data.action=None
for p in rig.pose.bones: p.matrix_basis.identity()
scene.frame_set(1)
(OUT/'motion_geometry_checks.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
