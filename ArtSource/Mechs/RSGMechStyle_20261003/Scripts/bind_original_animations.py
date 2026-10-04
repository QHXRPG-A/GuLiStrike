"""Bake the seven original world-pose streams onto the restored 44-bone rig."""
import bpy
import json
import math
from pathlib import Path
from mathutils import Matrix, Vector

ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
OUT=ROOT/'Production_B_v1'
source=json.loads((ROOT/'Source/Animations/source_world_poses.json').read_text(encoding='utf-8'))
manifest=json.loads((ROOT/'Source/source_manifest.json').read_text(encoding='utf-8'))
scene=bpy.context.scene
rig=bpy.data.objects['Armature']
body=bpy.data.objects['RSGMech_LOD0_Body']
bone_names=[b['name'] for b in manifest['bones']]
inverse=rig.matrix_world.inverted()
rig.animation_data_create()
scene.render.fps=30
scene.render.fps_base=1
reports=[]
for record in source['animations']:
    action=bpy.data.actions.new(record['name'])
    action.use_fake_user=True
    rig.animation_data.action=action
    rig.animation_data.action_slot=None
    previous={}
    for frame_index,frame in enumerate(record['frames'],1):
        matrices={name:inverse @ Matrix(frame['bones'][name]) for name in frame['bones']}
        matrices['DeformationSystem']=inverse @ Matrix(frame['armature_world'])
        for name in bone_names:
            pb=rig.pose.bones[name]
            parent=pb.parent
            if parent:
                basis=pb.bone.convert_local_to_pose(matrices[name],pb.bone.matrix_local,
                       parent_matrix=matrices[parent.name],parent_matrix_local=parent.bone.matrix_local,invert=True)
            else:
                basis=pb.bone.convert_local_to_pose(matrices[name],pb.bone.matrix_local,invert=True)
            location,rotation,scale=basis.decompose()
            if name in previous and rotation.dot(previous[name])<0: rotation.negate()
            previous[name]=rotation.copy()
            pb.rotation_mode='QUATERNION'
            pb.location=location
            pb.rotation_quaternion=rotation
            pb.scale=scale
            for prop in ('location','rotation_quaternion','scale'):
                pb.keyframe_insert(data_path=prop,frame=frame_index,group=name)
    if action.slots:
        rig.animation_data.action_slot=action.slots[0]
    # Preserve original seconds and interpolate only between sampled poses.
    for layer in action.layers:
        for strip in layer.strips:
            for slot in action.slots:
                try: bag=strip.channelbag(slot)
                except Exception: continue
                for curve in bag.fcurves:
                    for key in curve.keyframe_points: key.interpolation='LINEAR'
    action['SourceAnimation']=record['source']
    action['OriginalDurationSeconds']=record['duration_s']
    action['TargetBoneCount']=44
    action['PoseSource']='original UE FBX sampled at original seconds'
    errors=[]
    for index in sorted({0,len(record['frames'])//2,len(record['frames'])-1}):
        scene.frame_set(index+1)
        bpy.context.view_layer.update()
        sample=record['frames'][index]
        translation=rotation_error=0
        for name in bone_names:
            expected=Matrix(sample['armature_world'] if name=='DeformationSystem' else sample['bones'][name])
            actual=rig.matrix_world @ rig.pose.bones[name].matrix
            translation=max(translation,(actual.translation-expected.translation).length)
            qa=actual.to_quaternion().normalized()
            qe=expected.to_quaternion().normalized()
            angle=qa.rotation_difference(qe).angle
            rotation_error=max(rotation_error,min(angle,2*math.pi-angle))
        errors.append({'frame':index+1,'max_bone_translation_error_m':translation,
                       'max_bone_rotation_error_degrees':math.degrees(rotation_error)})
    max_position=max(r['max_bone_translation_error_m'] for r in errors)
    max_angle=max(r['max_bone_rotation_error_degrees'] for r in errors)
    assert max_position<.0001, (record['name'],max_position)
    assert max_angle<.1, (record['name'],max_angle)
    track=rig.animation_data.nla_tracks.new()
    track.name=record['name'].replace('A_FPS_Mech_','')
    track.mute=True
    strip=track.strips.new(record['name'],1,action)
    strip.action_frame_start=1
    strip.action_frame_end=len(record['frames'])
    reports.append({'action':action.name,'source':record['source'],'duration_s':record['duration_s'],
                    'frames':len(record['frames']),'fps':30,'bones':44,'pose_checks':errors,
                    'max_position_error_m':max_position,'max_rotation_error_degrees':max_angle})
    print(json.dumps({k:reports[-1][k] for k in ('action','frames','bones','max_position_error_m','max_rotation_error_degrees')}),flush=True)

rig.animation_data.action=None
for bone in rig.pose.bones: bone.matrix_basis=Matrix.Identity(4)
scene.frame_start=1; scene.frame_end=76
scene.frame_set(1)
bpy.context.view_layer.update()
weight_report=[]
for obj in [o for o in scene.objects if o.name.startswith('RSGMech_LOD') and o.type=='MESH']:
    names={g.index:g.name for g in obj.vertex_groups}
    invalid=[]
    for vertex in obj.data.vertices:
        weights=[(names[g.group],g.weight) for g in vertex.groups if names[g.group] in bone_names and g.weight>1e-5]
        if len(weights)!=1 or abs(weights[0][1]-1)>1e-5: invalid.append(vertex.index)
    assert not invalid,(obj.name,invalid[:10])
    weight_report.append({'mesh':obj.name,'vertices':len(obj.data.vertices),'single_bone_weight_1':True})
report={'animations':reports,'original_animations':7,'rig_bones':44,'single_bone_weights':weight_report,
        'compatibility_scope':'Blender original pose transfer and rigid binding; UE readback deferred until delivery.',
        'B_approval':'pending'}
(OUT/'animation_binding_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
scene['RSG_AnimationNames']=json.dumps([r['action'] for r in reports])
scene['RSG_AnimationInstructions']='Select Armature; Action Editor choose one of seven A_FPS_Mech actions; all NLA tracks are muted by default.'
scene['RSG_ApprovedReference']='A-v3'
scene['RSG_ProductionVersion']='B-v1'
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.shading.type='MATERIAL'
            area.spaces.active.region_3d.view_perspective='CAMERA'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'RSGMech_Production_B_v1.blend'))
print(json.dumps({'animation_count':len(reports),'weights_all_rigid':True,'production_file':bpy.data.filepath}),flush=True)
